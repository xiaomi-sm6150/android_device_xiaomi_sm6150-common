// FIXME: your file license if you have one

#pragma once

#include <vendor/xiaomi/hardware/touchfeature/1.0/ITouchFeature.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

namespace vendor::xiaomi::hardware::touchfeature::implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;

struct TouchFeature : public V1_0::ITouchFeature {
    // Methods from ::vendor::xiaomi::hardware::touchfeature::V1_0::ITouchFeature follow.
    Return<int32_t> setTouchMode(int32_t mode, int32_t value) override;
    Return<int32_t> getTouchModeCurValue(int32_t mode) override;
    Return<int32_t> getTouchModeMaxValue(int32_t mode) override;
    Return<int32_t> getTouchModeMinValue(int32_t mode) override;
    Return<int32_t> getTouchModeDefValue(int32_t mode) override;
    Return<int32_t> resetTouchMode(int32_t mode) override;
    Return<void> getModeValues(int32_t mode, getModeValues_cb _hidl_cb) override;

    // Methods from ::android::hidl::base::V1_0::IBase follow.

};

// FIXME: most likely delete, this is only for passthrough implementations
// extern "C" ITouchFeature* HIDL_FETCH_ITouchFeature(const char* name);

}  // namespace vendor::xiaomi::hardware::touchfeature::implementation
