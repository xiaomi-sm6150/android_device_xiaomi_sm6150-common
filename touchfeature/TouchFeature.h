/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android-base/unique_fd.h>
#include <vendor/xiaomi/hardware/touchfeature/1.0/ITouchFeature.h>

namespace vendor::xiaomi::hardware::touchfeature::implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;

class TouchFeature : public V1_0::ITouchFeature {
  public:
    TouchFeature();

    // Methods from ::vendor::xiaomi::hardware::touchfeature::V1_0::ITouchFeature follow.
    Return<int32_t> setTouchMode(int32_t mode, int32_t value) override;
    Return<int32_t> getTouchModeCurValue(int32_t mode) override;
    Return<int32_t> getTouchModeMaxValue(int32_t mode) override;
    Return<int32_t> getTouchModeMinValue(int32_t mode) override;
    Return<int32_t> getTouchModeDefValue(int32_t mode) override;
    Return<int32_t> resetTouchMode(int32_t mode) override;
    Return<void> getModeValues(int32_t mode, getModeValues_cb _hidl_cb) override;

private:
    android::base::unique_fd touch_fd_;

};

}  // namespace vendor::xiaomi::hardware::touchfeature::implementation
