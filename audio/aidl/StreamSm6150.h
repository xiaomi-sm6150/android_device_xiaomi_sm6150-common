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

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <android-base/thread_annotations.h>

#include "core-impl/DriverStubImpl.h"
#include "StreamAlsaMonoPipeSm6150.h"
#include "primary/PrimaryMixer.h"
#include "PrimaryRoute.h"

namespace aidl::android::hardware::audio::core::sm6150 {

class StreamSm6150 : public StreamAlsaMonoPipeSm6150 {
  public:
    StreamSm6150(StreamContext* context, const Metadata& metadata);

    // Methods of 'DriverInterface'.
    ::android::status_t init(DriverCallbackInterface* callback) override;
    ::android::status_t drain(StreamDescriptor::DrainMode mode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t standby() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;
    void shutdown() override;

    // Overridden methods of 'StreamCommonImpl', called on a Binder thread.
    ndk::ScopedAStatus setConnectedDevices(const ConnectedDevices& devices) override;

  protected:
    std::vector<alsa::DeviceProfile> getDeviceProfiles() override;
    bool isStubStream();

    const bool mIsAsynchronous;
    int64_t mStartTimeNs = 0;
    long mFramesSinceStart = 0;
    bool mSkipNextTransfer = false;

  private:
    using AlsaDeviceId = std::pair<int, int>;

    static constexpr StreamSm6150::AlsaDeviceId kDefaultCardAndDeviceId{
            primary::PrimaryMixer::kAlsaCard, primary::PrimaryMixer::kAlsaDevice};
    static constexpr StreamSm6150::AlsaDeviceId kStubDeviceId{
            primary::PrimaryMixer::kInvalidAlsaCard, primary::PrimaryMixer::kInvalidAlsaDevice};

    static AlsaDeviceId getCardAndDeviceId(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices);
    static bool useStubStream(bool isInput,
                              const ::aidl::android::media::audio::common::AudioDevice& device);
    // Name of the mixer path (see mixer_paths_violet.xml) that routes the stream to the device.
    static std::string getRoutePath(
            bool isInput, const ::aidl::android::media::audio::common::AudioDevice& device);
    void resetRoute();

    bool isStubStreamOnWorker() const { return mCurrAlsaDeviceId == kStubDeviceId; }

    DriverStubImpl mStubDriver;
    mutable std::mutex mLock;
    AlsaDeviceId mAlsaDeviceId GUARDED_BY(mLock) = kStubDeviceId;
    std::string mRoutePath GUARDED_BY(mLock);

    // Used by the worker thread only.
    AlsaDeviceId mCurrAlsaDeviceId = kStubDeviceId;
    std::string mAppliedRoutePath;
};

class StreamInSm6150 final : public StreamIn, public StreamSm6150, public StreamInHwGainHelper {
  public:
    friend class ndk::SharedRefBase;
    StreamInSm6150(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }

    ndk::ScopedAStatus getHwGain(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwGain(const std::vector<float>& in_channelGains) override;
};

class StreamOutSm6150 final : public StreamOut,
                               public StreamSm6150,
                               public StreamOutHwVolumeHelper {
  public:
    friend class ndk::SharedRefBase;
    StreamOutSm6150(StreamContext&& context,
                     const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
                     const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                             offloadInfo);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }

    ndk::ScopedAStatus getHwVolume(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwVolume(const std::vector<float>& in_channelVolumes) override;
};

}  // namespace aidl::android::hardware::audio::core::sm6150
