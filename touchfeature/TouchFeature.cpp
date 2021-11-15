// FIXME: your file license if you have one

#include "TouchFeature.h"

namespace vendor::xiaomi::hardware::touchfeature::implementation {

// Methods from ::vendor::xiaomi::hardware::touchfeature::V1_0::ITouchFeature follow.
Return<int32_t> TouchFeature::setTouchMode(int32_t mode, int32_t value) {
    // TODO implement
    return int32_t {};
}

Return<int32_t> TouchFeature::getTouchModeCurValue(int32_t mode) {
    // TODO implement
    return int32_t {};
}

Return<int32_t> TouchFeature::getTouchModeMaxValue(int32_t mode) {
    // TODO implement
    return int32_t {};
}

Return<int32_t> TouchFeature::getTouchModeMinValue(int32_t mode) {
    // TODO implement
    return int32_t {};
}

Return<int32_t> TouchFeature::getTouchModeDefValue(int32_t mode) {
    // TODO implement
    return int32_t {};
}

Return<int32_t> TouchFeature::resetTouchMode(int32_t mode) {
    // TODO implement
    return int32_t {};
}

Return<void> TouchFeature::getModeValues(int32_t mode, getModeValues_cb _hidl_cb) {
    // TODO implement
    return Void();
}


// Methods from ::android::hidl::base::V1_0::IBase follow.

//ITouchFeature* HIDL_FETCH_ITouchFeature(const char* /* name */) {
    //return new TouchFeature();
//}
//
}  // namespace vendor::xiaomi::hardware::touchfeature::implementation
