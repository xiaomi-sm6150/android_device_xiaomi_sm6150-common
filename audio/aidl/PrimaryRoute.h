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

#include <map>
#include <mutex>
#include <string>

struct audio_route;

namespace aidl::android::hardware::audio::core::sm6150 {

// Applies the mixer routes ("paths") of a mixer_paths XML file (libaudioroute format) to the
// primary sound card. The card may register after the HAL has started, and may re-register, so the
// mixer is opened on demand and reopened when it goes stale.
class PrimaryRoute {
  public:
    static PrimaryRoute& getInstance();

    // Returns true if the path is now applied (false e.g. while the card is not registered).
    bool apply(const std::string& path);
    // Paths share controls (e.g. the TX decoder for a mic stream and a voice call), so after
    // resetting one path the paths that are still in use are applied again.
    void reset(const std::string& path);

  private:
    PrimaryRoute() = default;

    struct audio_route* openLocked();
    void closeLocked();

    std::mutex mLock;
    struct audio_route* mRoute = nullptr;
    std::map<std::string, int> mActivePaths;  // path -> number of users
};

}  // namespace aidl::android::hardware::audio::core::sm6150
