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

#define LOG_TAG "AHAL_StreamAlsaMonoPipe"
#include <Log.h>
#include <chrono>
#include <unistd.h>

#include <media/AidlConversionCppNdk.h>

#include "StreamAlsaMonoPipeSm6150.h"

using aidl::android::hardware::audio::common::getChannelCount;

namespace aidl::android::hardware::audio::core::sm6150 {

StreamAlsaMonoPipeSm6150::StreamAlsaMonoPipeSm6150(StreamContext* context, const Metadata& metadata,
                                       int readWriteRetries)
    : StreamAlsaBase(context, metadata, readWriteRetries) {}

StreamAlsaMonoPipeSm6150::~StreamAlsaMonoPipeSm6150() {
    cleanupWorker();
}

::android::NBAIO_Format StreamAlsaMonoPipeSm6150::getPipeFormat() const {
    const audio_format_t audioFormat = VALUE_OR_FATAL(
            aidl2legacy_AudioFormatDescription_audio_format_t(getContext().getFormat()));
    const int channelCount = getChannelCount(getContext().getChannelLayout());
    return ::android::Format_from_SR_C(getContext().getSampleRate(), channelCount, audioFormat);
}

::android::sp<::android::MonoPipe> StreamAlsaMonoPipeSm6150::makeSink(bool writeCanBlock) {
    const ::android::NBAIO_Format format = getPipeFormat();
    auto sink = ::android::sp<::android::MonoPipe>::make(mBufferSizeFrames, format, writeCanBlock);
    const ::android::NBAIO_Format offers[1] = {format};
    size_t numCounterOffers = 0;
    ssize_t index = sink->negotiate(offers, 1, nullptr, numCounterOffers);
    LOG_IF(FATAL, index != 0) << __func__ << ": Negotiation for the sink failed, index = " << index;
    return sink;
}

::android::sp<::android::MonoPipeReader> StreamAlsaMonoPipeSm6150::makeSource(::android::MonoPipe* pipe) {
    const ::android::NBAIO_Format format = getPipeFormat();
    const ::android::NBAIO_Format offers[1] = {format};
    auto source = ::android::sp<::android::MonoPipeReader>::make(pipe);
    size_t numCounterOffers = 0;
    ssize_t index = source->negotiate(offers, 1, nullptr, numCounterOffers);
    LOG_IF(FATAL, index != 0) << __func__
                              << ": Negotiation for the source failed, index = " << index;
    return source;
}

::android::status_t StreamAlsaMonoPipeSm6150::standby() {
    teardownIo();
    return ::android::OK;
}

::android::status_t StreamAlsaMonoPipeSm6150::start() {
    if (!mAlsaDeviceProxies.empty()) {
        // This is a resume after a pause.
        return ::android::OK;
    }
    decltype(mAlsaDeviceProxies) alsaDeviceProxies;
    decltype(mSources) sources;
    decltype(mSinks) sinks;
    if (::android::status_t status = openDeviceProxies(&alsaDeviceProxies);
        status != ::android::OK) {
        return status;
    }
    for (size_t i = 0; i < alsaDeviceProxies.size(); ++i) {
        auto sink = makeSink(mIsInput);  // Do not block the writer when it is on our thread.
        if (sink != nullptr) {
            sinks.push_back(sink);
        } else {
            return ::android::NO_INIT;
        }
        if (auto source = makeSource(sink.get()); source != nullptr) {
            sources.push_back(source);
        } else {
            return ::android::NO_INIT;
        }
    }
    mAlsaDeviceProxies = std::move(alsaDeviceProxies);
    mSources = std::move(sources);
    mSinks = std::move(sinks);
    mIoThreadIsRunning = true;
    for (size_t i = 0; i < mAlsaDeviceProxies.size(); ++i) {
        mIoThreads.emplace_back(
                mIsInput ? &StreamAlsaMonoPipeSm6150::inputIoThread : &StreamAlsaMonoPipeSm6150::outputIoThread,
                this, i);
    }
    return ::android::OK;
}

::android::status_t StreamAlsaMonoPipeSm6150::transfer(void* buffer, size_t frameCount,
                                                 size_t* actualFrameCount, int32_t* latencyMs) {
    if (mAlsaDeviceProxies.empty()) {
        LOG(FATAL) << __func__ << ": no opened devices";
        return ::android::NO_INIT;
    }
    const size_t bytesToTransfer = frameCount * mFrameSizeBytes;
    unsigned maxLatency = 0;
    if (mIsInput) {
        const size_t i = 0;  // For the input case, only support a single device.
        LOG(VERBOSE) << __func__ << ": reading from sink " << i;
        // MonoPipeReader::read() does not block. The ALSA reader thread fills the pipe at
        // the capture rate, so wait for it, otherwise the client is handed silence as fast
        // as it asks and the stream runs far ahead of real time. Bounded to twice the
        // buffer duration so a stuck PCM cannot block the stream forever.
        ssize_t framesRead = 0;
        const auto waitUntil = std::chrono::steady_clock::now() +
                               std::chrono::microseconds(
                                       2ull * frameCount * 1000000ull / mSampleRate + 20000);
        while (static_cast<size_t>(framesRead) < frameCount) {
            ssize_t r = mSources[i]->read(static_cast<char*>(buffer) + framesRead * mFrameSizeBytes,
                                          frameCount - framesRead);
            LOG_IF(FATAL, r < 0) << "Error reading from the pipe: " << r;
            framesRead += r;
            if (static_cast<size_t>(framesRead) >= frameCount || !mIoThreadIsRunning ||
                std::chrono::steady_clock::now() > waitUntil) {
                break;
            }
            if (r == 0) usleep(1000);
        }
        if (ssize_t framesMissing = static_cast<ssize_t>(frameCount) - framesRead;
            framesMissing > 0) {
            LOG(WARNING) << __func__ << ": incomplete data received, inserting " << framesMissing
                         << " frames of silence";
            memset(static_cast<char*>(buffer) + framesRead * mFrameSizeBytes, 0,
                   framesMissing * mFrameSizeBytes);
        }
        maxLatency = proxy_get_latency(mAlsaDeviceProxies[i].get());
    } else {
        alsa::applyGain(buffer, mGain, bytesToTransfer, mConfig.value().format, mConfig->channels);
        for (size_t i = 0; i < mAlsaDeviceProxies.size(); ++i) {
            LOG(VERBOSE) << __func__ << ": writing into sink " << i;
            ssize_t framesWritten = mSinks[i]->write(buffer, frameCount);
            LOG_IF(FATAL, framesWritten < 0) << "Error writing into the pipe: " << framesWritten;
            if (ssize_t framesLost = static_cast<ssize_t>(frameCount) - framesWritten;
                framesLost > 0) {
                LOG(WARNING) << __func__ << ": sink " << i << " incomplete data sent, dropping "
                             << framesLost << " frames";
            }
            maxLatency = std::max(maxLatency, proxy_get_latency(mAlsaDeviceProxies[i].get()));
        }
    }
    *actualFrameCount = frameCount;
    maxLatency = std::min(maxLatency, static_cast<unsigned>(std::numeric_limits<int32_t>::max()));
    *latencyMs = maxLatency;
    return ::android::OK;
}

void StreamAlsaMonoPipeSm6150::shutdown() {
    teardownIo();
}

void StreamAlsaMonoPipeSm6150::inputIoThread(size_t idx) {
#if defined(__ANDROID__)
    setWorkerThreadPriority(pthread_gettid_np(pthread_self()));
    const std::string threadName = (std::string("in_") + std::to_string(idx)).substr(0, 15);
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    const size_t bufferSize = mBufferSizeFrames * mFrameSizeBytes;
    std::vector<char> buffer(bufferSize);
    while (mIoThreadIsRunning) {
        if (int ret = proxy_read_with_retries(mAlsaDeviceProxies[idx].get(), &buffer[0], bufferSize,
                                              mReadWriteRetries);
            ret == 0) {
            size_t bufferFramesWritten = 0;
            while (bufferFramesWritten < mBufferSizeFrames) {
                if (!mIoThreadIsRunning) return;
                ssize_t framesWrittenOrError =
                        mSinks[idx]->write(&buffer[0], mBufferSizeFrames - bufferFramesWritten);
                if (framesWrittenOrError >= 0) {
                    bufferFramesWritten += framesWrittenOrError;
                    // The pipe is full: do not spin at real-time priority while the
                    // consumer drains it, that starves the kernel work completing the
                    // capture buffers on this CPU (reads then stall for seconds).
                    if (framesWrittenOrError == 0) usleep(1000);
                } else {
                    LOG(WARNING) << __func__ << "[" << idx
                                 << "]: Error while writing into the pipe: "
                                 << framesWrittenOrError;
                }
            }
        } else {
            // Errors when the stream is being stopped are expected.
            LOG_IF(WARNING, mIoThreadIsRunning)
                    << __func__ << "[" << idx << "]: Error reading from ALSA: " << ret;
        }
    }
}

void StreamAlsaMonoPipeSm6150::outputIoThread(size_t idx) {
#if defined(__ANDROID__)
    setWorkerThreadPriority(pthread_gettid_np(pthread_self()));
    const std::string threadName = (std::string("out_") + std::to_string(idx)).substr(0, 15);
    pthread_setname_np(pthread_self(), threadName.c_str());
#endif
    const size_t bufferSize = mBufferSizeFrames * mFrameSizeBytes;
    std::vector<char> buffer(bufferSize);
    while (mIoThreadIsRunning) {
        ssize_t framesReadOrError = mSources[idx]->read(&buffer[0], mBufferSizeFrames);
        if (framesReadOrError > 0) {
            int ret = proxy_write_with_retries(mAlsaDeviceProxies[idx].get(), &buffer[0],
                                               framesReadOrError * mFrameSizeBytes,
                                               mReadWriteRetries);
            // Errors when the stream is being stopped are expected.
            LOG_IF(WARNING, ret != 0 && mIoThreadIsRunning)
                    << __func__ << "[" << idx << "]: Error writing into ALSA: " << ret;
        } else if (framesReadOrError == 0) {
            // MonoPipeReader does not have a blocking read, while use of std::condition_variable
            // requires use of a mutex. For now, just do a 1ms sleep. Consider using a different
            // pipe / ring buffer mechanism.
            if (mIoThreadIsRunning) usleep(1000);
        } else {
            LOG(WARNING) << __func__ << "[" << idx
                         << "]: Error while reading from the pipe: " << framesReadOrError;
        }
    }
}

void StreamAlsaMonoPipeSm6150::teardownIo() {
    mIoThreadIsRunning = false;
    if (mIsInput) {
        LOG(DEBUG) << __func__ << ": shutting down pipes";
        for (auto& sink : mSinks) {
            sink->shutdown(true);
        }
    }
    LOG(DEBUG) << __func__ << ": stopping PCM streams";
    for (const auto& proxy : mAlsaDeviceProxies) {
        proxy_stop(proxy.get());
    }
    LOG(DEBUG) << __func__ << ": joining threads";
    for (auto& thread : mIoThreads) {
        if (thread.joinable()) thread.join();
    }
    mIoThreads.clear();
    LOG(DEBUG) << __func__ << ": closing PCM devices";
    mAlsaDeviceProxies.clear();
    mSources.clear();
    mSinks.clear();
}

}  // namespace aidl::android::hardware::audio::core::sm6150
