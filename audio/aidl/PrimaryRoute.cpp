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

#define LOG_TAG "AHAL_PrimaryRoute"

#include <android-base/logging.h>
#include <audio_route/audio_route.h>

#include "PrimaryRoute.h"
#include "primary/PrimaryMixer.h"

namespace aidl::android::hardware::audio::core::sm6150 {

namespace {
constexpr char kMixerPathsFile[] = "/vendor/etc/mixer_paths_violet.xml";
}  // namespace

// static
PrimaryRoute& PrimaryRoute::getInstance() {
    static PrimaryRoute gInstance;
    return gInstance;
}

struct audio_route* PrimaryRoute::openLocked() {
    if (mRoute == nullptr) {
        // Fails while the sound card is not registered yet; the caller retries on the next stream.
        mRoute = audio_route_init(primary::PrimaryMixer::kAlsaCard, kMixerPathsFile);
        if (mRoute == nullptr) {
            LOG(WARNING) << __func__ << ": no mixer routes for card " << primary::PrimaryMixer::kAlsaCard
                         << " from " << kMixerPathsFile;
        }
    }
    return mRoute;
}

void PrimaryRoute::closeLocked() {
    if (mRoute != nullptr) {
        audio_route_free(mRoute);
        mRoute = nullptr;
    }
}

bool PrimaryRoute::apply(const std::string& path) {
    if (path.empty()) return true;
    std::lock_guard l(mLock);
    // The second attempt reopens the mixer, in case the card re-registered since the first open.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (openLocked() == nullptr) return false;
        if (audio_route_apply_and_update_path(mRoute, path.c_str()) == 0) {
            LOG(INFO) << __func__ << ": applied '" << path << "'";
            ++mActivePaths[path];
            return true;
        }
        closeLocked();
    }
    LOG(ERROR) << __func__ << ": failed to apply '" << path << "'";
    return false;
}

void PrimaryRoute::reset(const std::string& path) {
    if (path.empty()) return;
    std::lock_guard l(mLock);
    if (auto it = mActivePaths.find(path); it != mActivePaths.end() && --it->second <= 0) {
        mActivePaths.erase(it);
    }
    if (mRoute == nullptr) return;
    if (audio_route_reset_path(mRoute, path.c_str()) != 0) {
        LOG(WARNING) << __func__ << ": failed to reset '" << path << "'";
        closeLocked();
        return;
    }
    for (const auto& [active, users] : mActivePaths) {
        audio_route_apply_path(mRoute, active.c_str());
    }
    audio_route_update_mixer(mRoute);
    LOG(INFO) << __func__ << ": reset '" << path << "', " << mActivePaths.size()
              << " path(s) still active";
}

}  // namespace aidl::android::hardware::audio::core::sm6150
