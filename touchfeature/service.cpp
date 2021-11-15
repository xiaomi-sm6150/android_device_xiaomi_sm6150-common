/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.xiaomi.hardware.touchfeature@1.0-service-xiaomi_sm6150"

#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>
#include <utils/Errors.h>

#include "TouchFeature.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using vendor::xiaomi::hardware::touchfeature::V1_0::ITouchFeature;
using vendor::xiaomi::hardware::touchfeature::implementation::TouchFeature;

int main() {
    configureRpcThreadpool(1, true /*callerWillJoin*/);

    android::sp<ITouchFeature> touchFeature = new TouchFeature();
    if (touchFeature->registerAsService() != android::OK) {
        LOG(ERROR) << "Failed to register TouchFeature HAL instance.";
        return -1;
    }

    LOG(INFO) << "TouchFeature HAL service is Ready.";
    joinRpcThreadpool();
    return 1;  // joinRpcThreadpool shouldn't exit
}
