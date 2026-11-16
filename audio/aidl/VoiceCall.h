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

#pragma once

#include <mutex>
#include <string>

#include <aidl/android/media/audio/common/AudioDevice.h>
#include <aidl/android/media/audio/common/AudioMode.h>

struct pcm;

namespace aidl::android::hardware::audio::core::sm6150 {

// Circuit-switched / VoLTE call audio on QCOM: the modem and the ADSP exchange the voice data
// directly, the HAL only routes the voice front end (VoiceMMode1) to the call devices and keeps
// its hostless PCM devices open for the duration of the call.
class VoiceCall {
  public:
    static VoiceCall& getInstance();

    void setMode(::aidl::android::media::audio::common::AudioMode mode);
    // The call device: the sink of the policy's TelephonySm6150 Rx -> device patch.
    void setOutputDevice(const ::aidl::android::media::audio::common::AudioDevice& device);
    void setVolume(float volume);  // 0.0 .. 1.0
    void setMicMute(bool mute);

  private:
    VoiceCall() = default;

    void updateLocked();
    void startLocked();
    void stopLocked();
    void rerouteLocked(const std::string& rx, const std::string& tx);
    const char* rxBackendLocked() const;
    void applyVolumeLocked();
    void sendCalibrationLocked();
    void applyMicMuteLocked();
    std::string rxPathLocked() const;
    std::string txPathLocked() const;

    std::mutex mLock;
    bool mInCall = false;
    bool mRunning = false;
    ::aidl::android::media::audio::common::AudioDevice mOutputDevice;
    std::string mRxPath;
    std::string mTxPath;
    // One RX/TX pair per voice session (VoiceMMode1 for SIM 1, VoiceMMode2 for SIM 2).
    struct pcm* mPcmRx[2] = {};
    struct pcm* mPcmTx[2] = {};
    float mVolume = 1.0f;
    unsigned int mCalSeq = 0;
    int mSessions = 0;  // voice sessions started, bit 0 = VoiceMMode1, bit 1 = VoiceMMode2
    bool mMicMute = false;
};

}  // namespace aidl::android::hardware::audio::core::sm6150
