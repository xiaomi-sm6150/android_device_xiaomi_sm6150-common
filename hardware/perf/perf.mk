#
# SPDX-FileCopyrightText: The PixelOS Project
# SPDX-License-Identifier: Apache-2.0
#

PERF_PATH := device/xiaomi/sm6150-common/hardware/perf

PRODUCT_SOONG_NAMESPACES += \
    $(PERF_PATH)

# HAL
PRODUCT_PACKAGES += \
    libadaptlaunch \
    libappclassifier \
    liblearningmodule \
    liblmthermallistner \
    liblmutils-ns \
    libmeters \
    libmeters-ns \
    libperfconfig \
    libperfgluelayer \
    libperfioctl \
    libprefapps \
    libq-perflog \
    libqti-perfd \
    libqti-perfd-client \
    libqti-perfd-client-perf2 \
    libqti-util \
    libqti-utils \
    libsplh \
    vendor.qti.hardware.perf2-hal-service \
    vendor.qti.hardware.perf2-V1-ndk_vendor \
    vendor.qti.hardware.perf2.xml \
    vendor.qti.memory.pasrmanager-V1-ndk

PRODUCT_COPY_FILES += \
    $(PERF_PATH)/proprietary/vendor/etc/init/vendor.qti.hardware.perf2-hal-service.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/vendor.qti.hardware.perf2-hal-service.rc \
    $(call find-copy-subdir-files,*,$(PERF_PATH)/configs/perf,$(TARGET_COPY_OUT_VENDOR)/etc/perf) \
    $(call find-copy-subdir-files,*,$(PERF_PATH)/configs/lm,$(TARGET_COPY_OUT_VENDOR)/etc/lm)

# Properties
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.perf-hal.ver=3.0
