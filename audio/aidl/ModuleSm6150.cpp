/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <vector>

#define LOG_TAG "AHAL_ModulePrimary"
#include <Log.h>
#include <Utils.h>

#include "core-impl/Configuration.h"
#include "ModuleSm6150.h"
#include "VoiceCall.h"
#include "core-impl/StreamMmapStub.h"
#include "core-impl/StreamOffloadStub.h"
#include "StreamSm6150.h"
#include "core-impl/StreamStub.h"
#include "TelephonySm6150.h"

using aidl::android::hardware::audio::common::areAllBitPositionFlagsSet;
using aidl::android::hardware::audio::common::hasMmapFlag;
using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::hardware::audio::core::StreamDescriptor;
using aidl::android::media::audio::common::AudioInputFlags;
using aidl::android::media::audio::common::AudioIoFlags;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioOutputFlags;
using aidl::android::media::audio::common::AudioPort;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::AudioPortExt;
using aidl::android::media::audio::common::FlushFromFrameSupport;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core::sm6150 {

ndk::ScopedAStatus ModuleSm6150::setMicMute(bool in_mute) {
    VoiceCall::getInstance().setMicMute(in_mute);
    return Module::setMicMute(in_mute);
}

ndk::ScopedAStatus ModuleSm6150::checkAudioPatchEndpointsMatch(
        const std::vector<AudioPortConfig*>& sources, const std::vector<AudioPortConfig*>& sinks) {
    // During a call the policy connects TelephonySm6150 Rx straight to the call device (a device to
    // device patch, no streams: the voice data stays between the modem and the ADSP). The sink
    // of that patch is the device the call is routed to.
    const auto isDevice = [](const AudioPortConfig* c, AudioDeviceType type) {
        return c->ext.getTag() == AudioPortExt::device &&
               c->ext.get<AudioPortExt::device>().device.type.type == type;
    };
    for (const auto* source : sources) {
        if (!isDevice(source, AudioDeviceType::IN_TELEPHONY_RX)) continue;
        for (const auto* sink : sinks) {
            if (sink->ext.getTag() == AudioPortExt::device) {
                LOG(DEBUG) << __func__ << ": call routed to " << sink->ext.toString();
                VoiceCall::getInstance().setOutputDevice(
                        sink->ext.get<AudioPortExt::device>().device);
            }
        }
    }
    return Module::checkAudioPatchEndpointsMatch(sources, sinks);
}

ndk::ScopedAStatus ModuleSm6150::getTelephony(std::shared_ptr<ITelephony>* _aidl_return) {
    if (!mTelephony) {
        mTelephony = ndk::SharedRefBase::make<TelephonySm6150>();
    }
    *_aidl_return = mTelephony.getInstance();
    LOG(DEBUG) << __func__
               << ": returning instance of ITelephony: " << _aidl_return->get()->asBinder().get();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ModuleSm6150::calculateBufferSizeFrames(
        const ::aidl::android::media::audio::common::AudioFormatDescription& format,
        const ::aidl::android::media::audio::common::AudioIoFlags& flags, int32_t latencyMs,
        int32_t sampleRateHz, int32_t* bufferSizeFrames) {
    if ((format.type != ::aidl::android::media::audio::common::AudioFormatType::PCM &&
         StreamOffloadStub::getSupportedEncodings().count(format.encoding)) ||
        common::isPcmOffload(format, flags)) {
        *bufferSizeFrames = sampleRateHz / 2;  // 1/2 of a second.
        return ndk::ScopedAStatus::ok();
    }
    return Module::calculateBufferSizeFrames(format, flags, latencyMs, sampleRateHz,
                                             bufferSizeFrames);
}

ndk::ScopedAStatus ModuleSm6150::createInputStream(StreamContext&& context,
                                                    const SinkMetadata& sinkMetadata,
                                                    const std::vector<MicrophoneInfo>& microphones,
                                                    std::shared_ptr<StreamIn>* result) {
    if (context.isMmap()) {
        // "Stub" is used because there is no support for MMAP audio I/O on CVD.
        return createStreamInstance<StreamInMmapStub>(result, std::move(context), sinkMetadata,
                                                      microphones);
    }
    return createStreamInstance<StreamInSm6150>(result, std::move(context), sinkMetadata,
                                                 microphones);
}

ndk::ScopedAStatus ModuleSm6150::createOutputStream(
        StreamContext&& context, const SourceMetadata& sourceMetadata,
        const std::optional<AudioOffloadInfo>& offloadInfo, std::shared_ptr<StreamOut>* result) {
    if (context.isMmap()) {
        // "Stub" is used because there is no support for MMAP audio I/O on CVD.
        return createStreamInstance<StreamOutMmapStub>(result, std::move(context), sourceMetadata,
                                                       offloadInfo);
    } else if (areAllBitPositionFlagsSet(
                       context.getFlags().get<AudioIoFlags::output>(),
                       {AudioOutputFlags::COMPRESS_OFFLOAD, AudioOutputFlags::NON_BLOCKING})) {
        // "Stub" is used because there is no actual decoder. The stream just
        // extracts the clip duration from the media file header and simulates
        // playback over time.
        if (context.getSampleRate() <= 0) {
            LOG(ERROR) << __func__
                       << ": Invalid sample rate for offload stream: " << context.getSampleRate();
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        }

        return createStreamInstance<StreamOutOffloadStub>(result, std::move(context),
                                                          sourceMetadata, offloadInfo);
    }

    const auto& activeConfigs = getConfig().portConfigs;
    int32_t handle = context.getMixPortHandle();
    auto it = std::find_if(activeConfigs.begin(), activeConfigs.end(), [&](const auto& config) {
        return config.ext.getTag() == AudioPortExt::Tag::mix &&
               config.ext.template get<AudioPortExt::Tag::mix>().handle == handle;
    });
    if (it != activeConfigs.end()) {
        int32_t portId = it->portId;
        auto& ports = getConfig().ports;
        auto foundPort = findById(ports, portId);
        if (foundPort != ports.end() && foundPort->name == internal::kPortNameTelephonyTx) {
            return createStreamInstance<StreamOutTelephonyStub>(result, std::move(context),
                                                                sourceMetadata, offloadInfo);
        }
    }

    return createStreamInstance<StreamOutSm6150>(result, std::move(context), sourceMetadata,
                                                  offloadInfo);
}

ndk::ScopedAStatus ModuleSm6150::createMmapBuffer(const AudioPortConfig& portConfig,
                                                   int32_t bufferSizeFrames, int32_t frameSizeBytes,
                                                   MmapBufferDescriptor* desc) {
    const size_t bufferSizeBytes = static_cast<size_t>(bufferSizeFrames) * frameSizeBytes;
    // The actual mmap buffer for I/O is created after the stream exits standby, via
    // 'IStreamCommon.createMmapBuffer'. But we must return a valid file descriptor here because
    // 'MmapBufferDescriptor' can not contain a "null" fd.
    const std::string regionName =
            std::string("mmap-sim-o-") +
            std::to_string(portConfig.ext.get<AudioPortExt::Tag::mix>().handle);
    int fd = ashmem_create_region(regionName.c_str(), bufferSizeBytes);
    if (fd < 0) {
        PLOG(ERROR) << __func__ << ": failed to create shared memory region of " << bufferSizeBytes
                    << " bytes";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    desc->sharedMemory.fd = ndk::ScopedFileDescriptor(fd);
    desc->sharedMemory.size = bufferSizeBytes;
    desc->burstSizeFrames = bufferSizeFrames / 4;
    desc->flags = 1 << MmapBufferDescriptor::FLAG_INDEX_APPLICATION_SHAREABLE;
    LOG(DEBUG) << __func__ << ": " << desc->toString();
    return ndk::ScopedAStatus::ok();
}

int32_t ModuleSm6150::getNominalLatencyMs(const AudioPortConfig& portConfig) {
    static constexpr int32_t kLowLatencyMs = 10;
    // 85 ms is chosen considering 4096 frames @ 48 kHz. This is the value which allows
    // the virtual Android device implementation to pass CTS. Hardware implementations
    // should have significantly lower latency.
    static constexpr int32_t kStandardLatencyMs = 85;
    return hasMmapFlag(portConfig.flags.value()) ? kLowLatencyMs : kStandardLatencyMs;
}

ndk::ScopedAStatus ModuleSm6150::getFlushFromFrameSupport(const AudioPortConfig& in_config,
                                                           FlushFromFrameSupport* _aidl_return) {
    LOG(DEBUG) << __func__ << ": config=" << in_config.toString();
    if (!in_config.flags.has_value() || !in_config.format.has_value()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    *_aidl_return = common::isPcmOffload(in_config.format.value(), in_config.flags.value())
                            ? FlushFromFrameSupport::SUPPORTED
                            : FlushFromFrameSupport::UNSUPPORTED;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::audio::core::sm6150
