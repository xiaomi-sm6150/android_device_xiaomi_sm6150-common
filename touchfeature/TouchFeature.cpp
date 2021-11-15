/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/ioctl.h>

#include <android-base/logging.h>
#include <utils/Errors.h>

#include "TouchDefines.h"
#include "TouchFeature.h"

namespace vendor::xiaomi::hardware::touchfeature::implementation {

TouchFeature::TouchFeature() {
    LOG(INFO) << "in constructor";
    touch_fd_ = android::base::unique_fd(open(TOUCH_DEV_PATH, O_RDWR));
}

// Methods from ::vendor::xiaomi::hardware::touchfeature::V1_0::ITouchFeature follow.
Return<int32_t> TouchFeature::setTouchMode(int32_t mode, int32_t value) {
    LOG(INFO) << "called setTouchMode, mode: " << mode << ", value: " << value;
    int arg[MAX_BUF_SIZE] = {mode, value};
    ioctl(touch_fd_.get(), TOUCH_IOC_SET_CUR_VALUE, &arg);
    return arg[0];
}

Return<int32_t> TouchFeature::getTouchModeCurValue(int32_t mode) {
    int arg[MAX_BUF_SIZE] = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_GET_CUR_VALUE, &arg);
    return arg[0];
}

Return<int32_t> TouchFeature::getTouchModeMaxValue(int32_t mode) {
    int arg[MAX_BUF_SIZE] = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_GET_MAX_VALUE, &arg);
    return arg[0];
}

Return<int32_t> TouchFeature::getTouchModeMinValue(int32_t mode) {
    int arg[MAX_BUF_SIZE] = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_GET_MIN_VALUE, &arg);
    return arg[0];
}

Return<int32_t> TouchFeature::getTouchModeDefValue(int32_t mode) {
    int arg[MAX_BUF_SIZE] = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_GET_DEF_VALUE, &arg);
    return arg[0];
}

Return<int32_t> TouchFeature::resetTouchMode(int32_t mode) {
    int arg[MAX_BUF_SIZE] = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_RESET_MODE, &arg);
    return arg[0];
}

Return<void> TouchFeature::getModeValues(int32_t mode, getModeValues_cb _hidl_cb) {
    android::hardware::hidl_vec<int> arg = {mode, 0};
    ioctl(touch_fd_.get(), TOUCH_IOC_GET_MODE_VALUE, &arg);
    _hidl_cb(arg);
    return Void();
}

}  // namespace vendor::xiaomi::hardware::touchfeature::implementation
