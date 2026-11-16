/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <atomic>
#include <thread>
#include <vector>

#include <media/nbaio/MonoPipe.h>
#include <media/nbaio/MonoPipeReader.h>

#include "core-impl/StreamAlsaBase.h"

namespace aidl::android::hardware::audio::core::sm6150 {

// Extends StreamAlsaBase with a MonoPipe + dedicated I/O thread per device.
// The worker thread communicates with ALSA via a pipe, allowing the main
// transfer() call to be decoupled from the ALSA read/write latency.
// Used by StreamSm6150 (and any other implementation that needs the pipe path).
class StreamAlsaMonoPipeSm6150 : public StreamAlsaBase {
  public:
    StreamAlsaMonoPipeSm6150(StreamContext* context, const Metadata& metadata, int readWriteRetries);
    ~StreamAlsaMonoPipeSm6150();

    // Methods of 'DriverInterface'.
    ::android::status_t standby() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    void shutdown() override;

  private:
    ::android::NBAIO_Format getPipeFormat() const;
    ::android::sp<::android::MonoPipe> makeSink(bool writeCanBlock);
    ::android::sp<::android::MonoPipeReader> makeSource(::android::MonoPipe* pipe);
    void inputIoThread(size_t idx);
    void outputIoThread(size_t idx);
    void teardownIo();

    // All fields below are only used on the worker thread.
    // Only 'libnbaio_mono' is vendor-accessible, thus no access to the multi-reader Pipe.
    std::vector<::android::sp<::android::MonoPipe>> mSinks;
    std::vector<::android::sp<::android::MonoPipeReader>> mSources;
    std::vector<std::thread> mIoThreads;
    std::atomic<bool> mIoThreadIsRunning = false;  // used by all threads
};

}  // namespace aidl::android::hardware::audio::core::sm6150
