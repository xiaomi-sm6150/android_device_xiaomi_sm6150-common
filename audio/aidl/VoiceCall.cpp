/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "AHAL_VoiceCall"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <tinyalsa/mixer.h>
#include <tinyalsa/pcm.h>

#include "primary/PrimaryMixer.h"
#include "PrimaryRoute.h"
#include "VoiceCall.h"

using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioMode;

namespace aidl::android::hardware::audio::core::sm6150 {

namespace {
// USECASE_VOICEMMODE1_CALL / USECASE_VOICEMMODE2_CALL of the stock audio_platform_info.xml
// (msm-pcm-voice front ends). On a dual-SIM phone the modem attaches the call of SIM 1 to the
// MMode1 session and the call of SIM 2 to MMode2 (see voiceSessionMask()).
constexpr unsigned int kVoiceDevices[2] = {2, 19};
constexpr int kMaxVolumeIndex = 5;  // stock HAL default (MAX_VOL_INDEX)
constexpr unsigned int kAllSessionsVsid = 0xFFFFFFFF;
constexpr unsigned int kVolumeRampMs = 20;
// Served by the 32-bit acdb_voice_helper (libacdbloader is 32-bit only).
constexpr char kCalRequestProp[] = "vendor.audio.voice.cal";
constexpr char kCalDoneProp[] = "vendor.audio.voice.cal_done";
constexpr auto kCalTimeout = std::chrono::milliseconds(3000);

// ACDB device ids of the stock HAL (platform.c acdb_device_table / audio_platform_info.xml).
struct AcdbIds {
    int rx;
    int tx;
};
AcdbIds acdbIdsFor(const std::string& rxPath, const std::string& txPath) {
    // Override for bring-up: vendor.audio.voice.acdb.<rx path>=<rx>:<tx>
    const std::string override =
            ::android::base::GetProperty("vendor.audio.voice.acdb." + rxPath, "");
    if (AcdbIds ids; sscanf(override.c_str(), "%d:%d", &ids.rx, &ids.tx) == 2) return ids;
    // Fluence is on for voice calls (persist.vendor.audio.fluence.voicecall): the stock HAL uses
    // the dual mic devices, the only TX devices with voice tuning (echo cancellation) in the ACDB.
    if (rxPath == "voice-speaker") return {14, 43};      // VOICE_SPEAKER / VOICE_SPEAKER_DMIC
    if (rxPath == "voice-headphones") {
        return {10, txPath == "voice-mic-headset" ? 8 : 41};  // VOICE_HEADSET(_MIC) / VOICE_DMIC
    }
    return {7, 41};                                       // VOICE_HANDSET / VOICE_DMIC
}

// The voice sessions to start, as a mask (bit 0 = VoiceMMode1 / SIM 1, bit 1 = VoiceMMode2 /
// SIM 2). An idle session on the same devices takes the uplink away from the active one, so start
// only the session of the voice SIM (vendor.audio.voice.sim_mask). Without it, start both: the
// session of a call comes with the vsid vendor parameter, which this HAL does not receive.
int voiceSessionMask() {
    if (const int forced = ::android::base::GetIntProperty("vendor.audio.voice.sessions", 0);
        forced > 0) {
        return forced & 3;
    }
    // Set by init from the IMS stack's active subscription (acdb_voice_helper.rc); vendor code
    // may not read gsm.sim.state.
    const int mask = ::android::base::GetIntProperty("vendor.audio.voice.sim_mask", 3) & 3;
    LOG(INFO) << __func__ << ": sim mask " << mask;
    return (mask == 1 || mask == 2) ? mask : 3;
}

// Voice front end <-> backend routing (msm-pcm-routing). Set here rather than in the mixer paths:
// libaudioroute writes a path in control index order, which would connect the voice TX backend
// before the codec channel controls of the path are set (the AFE then rejects the port config),
// and the voice routing has to change separately from the devices, like in the stock HAL.
constexpr const char* kVoiceRxBackends[] = {"RX_CDC_DMA_RX_0", "PRI_MI2S_RX"};
constexpr const char* kVoiceTxBackend = "TX_CDC_DMA_TX_3";

void setVoiceRouting(const char* rxBackend, int sessions, bool on) {
    struct mixer* mixer = mixer_open(primary::PrimaryMixer::kAlsaCard);
    if (mixer == nullptr) {
        LOG(ERROR) << __func__ << ": mixer_open failed";
        return;
    }
    const auto set = [&](const std::string& name, int value) {
        struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, name.c_str());
        if (ctl == nullptr || mixer_ctl_set_value(ctl, 0, value) != 0) {
            LOG(ERROR) << __func__ << ": failed to set \"" << name << "\" to " << value;
        }
    };
    for (int i = 0; i < 2; ++i) {
        if (!(sessions & (1 << i))) continue;
        const std::string mode = "VoiceMMode" + std::to_string(i + 1);
        const std::string tx = mode + "_Tx Mixer " + kVoiceTxBackend + "_MMode" + std::to_string(i + 1);
        const std::string rx = std::string(rxBackend) + "_Voice Mixer " + mode;
        // Connect RX before TX and disconnect TX before RX, like the stock voice-call paths.
        if (on) {
            set(rx, 1);
            set(tx, 1);
        } else {
            set(tx, 0);
            set(rx, 0);
        }
    }
    mixer_close(mixer);
}

struct pcm* openVoicePcm(unsigned int device, unsigned int flags) {
    struct pcm_config config = {};
    config.channels = 1;
    config.rate = 8000;
    config.period_size = 160;
    config.period_count = 2;
    config.format = PCM_FORMAT_S16_LE;
    struct pcm* pcm = pcm_open(primary::PrimaryMixer::kAlsaCard, device, flags, &config);
    if (pcm == nullptr || !pcm_is_ready(pcm)) {
        LOG(ERROR) << __func__ << ": pcm_open " << primary::PrimaryMixer::kAlsaCard << "," << device
                   << ((flags & PCM_IN) ? " in" : " out") << " failed: "
                   << (pcm != nullptr ? pcm_get_error(pcm) : "null");
        if (pcm != nullptr) pcm_close(pcm);
        return nullptr;
    }
    return pcm;
}
}  // namespace

// static
VoiceCall& VoiceCall::getInstance() {
    static VoiceCall gInstance;
    return gInstance;
}

void VoiceCall::setMode(AudioMode mode) {
    std::lock_guard l(mLock);
    const bool inCall = mode == AudioMode::IN_CALL;
    if (inCall && !mInCall) {
        // The primary output still points at the ringtone device (speaker); a call starts on
        // the earpiece and setOutputDevice() moves it when the policy routes elsewhere.
        mOutputDevice = AudioDevice{};
        mOutputDevice.type.type = AudioDeviceType::OUT_SPEAKER_EARPIECE;
    }
    mInCall = inCall;
    LOG(INFO) << __func__ << ": " << toString(mode) << ", in call " << mInCall;
    updateLocked();
}

void VoiceCall::setOutputDevice(const AudioDevice& device) {
    std::lock_guard l(mLock);
    mOutputDevice = device;
    if (mRunning) updateLocked();
}

void VoiceCall::setMicMute(bool mute) {
    std::lock_guard l(mLock);
    mMicMute = mute;
    LOG(INFO) << __func__ << ": " << mute;
    if (mRunning) applyMicMuteLocked();
}

void VoiceCall::setVolume(float volume) {
    std::lock_guard l(mLock);
    mVolume = volume;
    if (mRunning) applyVolumeLocked();
}

std::string VoiceCall::rxPathLocked() const {
    const auto& type = mOutputDevice.type;
    switch (type.type) {
        case AudioDeviceType::OUT_SPEAKER:
            return "voice-speaker";
        case AudioDeviceType::OUT_HEADSET:
        case AudioDeviceType::OUT_HEADPHONE:
            if (type.connection == AudioDeviceDescription::CONNECTION_ANALOG) {
                return "voice-headphones";
            }
            break;
        default:
            break;
    }
    return "voice-handset";
}

std::string VoiceCall::txPathLocked() const {
    const auto& type = mOutputDevice.type;
    // A wired headset brings its own mic; everything else uses both built-in mics (stock fluence
    // dual mic endfire devices).
    if (type.type == AudioDeviceType::OUT_HEADSET &&
        type.connection == AudioDeviceDescription::CONNECTION_ANALOG) {
        return "voice-mic-headset";
    }
    return type.type == AudioDeviceType::OUT_SPEAKER ? "voice-speaker-dmic-ef" : "voice-dmic-ef";
}

void VoiceCall::updateLocked() {
    if (!mInCall) {
        if (mRunning) stopLocked();
        return;
    }
    if (!mRunning) {
        startLocked();
        return;
    }
    const std::string rx = rxPathLocked(), tx = txPathLocked();
    if (rx == mRxPath && tx == mTxPath) return;
    rerouteLocked(rx, tx);
}

void VoiceCall::rerouteLocked(const std::string& rx, const std::string& tx) {
    LOG(INFO) << __func__ << ": " << mRxPath << "/" << mTxPath << " -> " << rx << "/" << tx;
    // Like the stock HAL's select_devices() for a voice call: keep the voice sessions open,
    // disconnect them from the old devices (both RX and TX, even if one of them stays), send the
    // calibration of the new pair, then connect them again. msm-pcm-routing turns the mixer
    // changes into a device disable / enable of the running sessions.
    auto& route = PrimaryRoute::getInstance();
    setVoiceRouting(rxBackendLocked(), mSessions, false);
    route.reset(mRxPath);
    route.reset(mTxPath);
    mRxPath = rx;
    mTxPath = tx;
    route.apply(mRxPath);
    route.apply(mTxPath);
    sendCalibrationLocked();
    setVoiceRouting(rxBackendLocked(), mSessions, true);
    applyMicMuteLocked();
    applyVolumeLocked();
}

void VoiceCall::sendCalibrationLocked() {
    const AcdbIds ids = acdbIdsFor(mRxPath, mTxPath);
    const std::string seq = std::to_string(++mCalSeq);
    if (!::android::base::SetProperty(
                kCalRequestProp, ::android::base::StringPrintf("%s:%d:%d", seq.c_str(), ids.rx,
                                                               ids.tx))) {
        LOG(ERROR) << __func__ << ": failed to request voice calibration";
        return;
    }
    if (!::android::base::WaitForProperty(kCalDoneProp, seq, kCalTimeout)) {
        LOG(WARNING) << __func__ << ": no voice calibration ack for rx " << ids.rx << " tx "
                     << ids.tx;
        return;
    }
    LOG(INFO) << __func__ << ": voice calibration rx " << ids.rx << " tx " << ids.tx << " sent";
}

const char* VoiceCall::rxBackendLocked() const {
    return mRxPath == "voice-speaker" ? kVoiceRxBackends[1] : kVoiceRxBackends[0];
}

void VoiceCall::startLocked() {
    // Clear voice routing a previous HAL instance may have left behind.
    for (const char* rxBackend : kVoiceRxBackends) setVoiceRouting(rxBackend, 3, false);
    mRxPath = rxPathLocked();
    mTxPath = txPathLocked();
    mSessions = voiceSessionMask();
    // Stock order: devices, calibration, voice routing, then the voice PCMs.
    auto& route = PrimaryRoute::getInstance();
    route.apply(mRxPath);
    route.apply(mTxPath);
    sendCalibrationLocked();
    setVoiceRouting(rxBackendLocked(), mSessions, true);
    for (int i = 0; i < 2; ++i) {
        if (!(mSessions & (1 << i))) continue;
        // Same order as the stock HAL: TX first.
        mPcmTx[i] = openVoicePcm(kVoiceDevices[i], PCM_IN);
        mPcmRx[i] = openVoicePcm(kVoiceDevices[i], PCM_OUT);
        if (mPcmTx[i] != nullptr && pcm_start(mPcmTx[i]) != 0) {
            LOG(ERROR) << __func__ << ": pcm_start tx " << kVoiceDevices[i]
                       << " failed: " << pcm_get_error(mPcmTx[i]);
        }
        if (mPcmRx[i] != nullptr && pcm_start(mPcmRx[i]) != 0) {
            LOG(ERROR) << __func__ << ": pcm_start rx " << kVoiceDevices[i]
                       << " failed: " << pcm_get_error(mPcmRx[i]);
        }
    }
    mRunning = true;
    applyMicMuteLocked();
    applyVolumeLocked();
    LOG(INFO) << __func__ << ": voice started on " << mRxPath << "/" << mTxPath << " (mmode1 "
              << (mPcmRx[0] != nullptr) << (mPcmTx[0] != nullptr) << ", mmode2 "
              << (mPcmRx[1] != nullptr) << (mPcmTx[1] != nullptr) << ")";
}

void VoiceCall::stopLocked() {
    for (int i = 0; i < 2; ++i) {
        for (struct pcm** pcm : {&mPcmTx[i], &mPcmRx[i]}) {
            if (*pcm != nullptr) {
                pcm_stop(*pcm);
                pcm_close(*pcm);
                *pcm = nullptr;
            }
        }
    }
    setVoiceRouting(rxBackendLocked(), mSessions, false);
    auto& route = PrimaryRoute::getInstance();
    route.reset(mRxPath);
    route.reset(mTxPath);
    mRxPath.clear();
    mTxPath.clear();
    mRunning = false;
    LOG(INFO) << __func__ << ": voice stopped";
}

void VoiceCall::applyMicMuteLocked() {
    struct mixer* mixer = mixer_open(primary::PrimaryMixer::kAlsaCard);
    if (mixer == nullptr) return;
    // Like the stock HAL's voice_set_mic_mute(): {mute, vsid, ramp ms}, set on every call start.
    if (struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, "Voice Tx Mute"); ctl != nullptr) {
        const long values[] = {mMicMute ? 1 : 0, static_cast<long>(kAllSessionsVsid),
                               kVolumeRampMs};
        if (mixer_ctl_set_array(ctl, values, std::size(values)) != 0) {
            LOG(WARNING) << __func__ << ": failed to set Voice Tx Mute";
        }
    }
    mixer_close(mixer);
}

void VoiceCall::applyVolumeLocked() {
    struct mixer* mixer = mixer_open(primary::PrimaryMixer::kAlsaCard);
    if (mixer == nullptr) return;
    // Like the stock HAL's voice_set_volume() / platform_set_voice_volume(): the driver's volume
    // steps are inverted (step 0 is the loudest, the max step is the quietest), and the max step
    // (Android volume 0) also mutes the RX device.
    const long step = std::lround((1.0f - std::clamp(mVolume, 0.0f, 1.0f)) * kMaxVolumeIndex);
    if (struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, "Voice Rx Gain"); ctl != nullptr) {
        const long values[] = {step, static_cast<long>(kAllSessionsVsid), kVolumeRampMs};
        if (mixer_ctl_set_array(ctl, values, std::size(values)) != 0) {
            LOG(WARNING) << __func__ << ": failed to set Voice Rx Gain";
        }
    }
    if (struct mixer_ctl* ctl = mixer_get_ctl_by_name(mixer, "Voice Rx Device Mute");
        ctl != nullptr) {
        const long values[] = {step == kMaxVolumeIndex ? 1 : 0, static_cast<long>(kAllSessionsVsid),
                               kVolumeRampMs};
        if (mixer_ctl_set_array(ctl, values, std::size(values)) != 0) {
            LOG(WARNING) << __func__ << ": failed to set Voice Rx Device Mute";
        }
    }
    mixer_close(mixer);
    LOG(INFO) << __func__ << ": volume " << mVolume << " -> step " << step;
}

}  // namespace aidl::android::hardware::audio::core::sm6150
