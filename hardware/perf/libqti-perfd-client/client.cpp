/*
 * Copyright (C) 2026 The PixelOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// 32-bit libqti-perfd-client for the perf2 AIDL HAL.
//
// The perf2 HAL and its client ship 64-bit only, but the stock 32-bit audio
// HAL (libadm) and Codec2 (libqcodec2_hooks) dlopen libqti-perfd-client.so
// for perf_lock_acq, perf_lock_rel and perf_hint. This sends those calls to
// vendor.qti.hardware.perf2.IPerf/default with hand-written parcels. The
// transaction codes, argument order and reserved contents ({tid, pid}, plus
// a 0 for hints) match the 64-bit client and vendor.qti.hardware.perf2-V1-ndk.

#define LOG_TAG "ANDR-PERF-CLIENT"

#include <android/binder_ibinder.h>
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <log/log.h>

#include <mutex>
#include <string.h>
#include <unistd.h>

namespace {

constexpr const char* kDescriptor = "vendor.qti.hardware.perf2.IPerf";
constexpr const char* kInstance = "vendor.qti.hardware.perf2.IPerf/default";

// FIRST_CALL_TRANSACTION + position in the (alphabetical) interface.
constexpr transaction_code_t TX_PERF_HINT = 7;
constexpr transaction_code_t TX_PERF_LOCK_ACQUIRE = 11;
constexpr transaction_code_t TX_PERF_LOCK_RELEASE = 13;

// The generated vendor stubs pass FLAG_PRIVATE_VENDOR.
constexpr binder_flags_t kFlagPrivateVendor = 0x10000000;

constexpr int kFailed = -1;
constexpr int kMaxArgs = 64;

void* onCreate(void* args) {
    return args;
}

void onDestroy(void*) {}

binder_status_t onTransact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    return STATUS_UNKNOWN_TRANSACTION;
}

std::mutex gLock;
AIBinder_Class* gClass = nullptr;
AIBinder* gBinder = nullptr;

// Returns a strong reference the caller must drop, or nullptr.
AIBinder* getPerf() {
    std::lock_guard<std::mutex> lock(gLock);
    if (gClass == nullptr) {
        gClass = AIBinder_Class_define(kDescriptor, onCreate, onDestroy, onTransact);
        if (gClass == nullptr) return nullptr;
    }
    if (gBinder != nullptr && !AIBinder_isAlive(gBinder)) {
        AIBinder_decStrong(gBinder);
        gBinder = nullptr;
    }
    if (gBinder == nullptr) {
        AIBinder* binder = AServiceManager_checkService(kInstance);
        if (binder == nullptr) return nullptr;
        if (!AIBinder_associateClass(binder, gClass)) {
            ALOGE("%s does not implement %s", kInstance, kDescriptor);
            AIBinder_decStrong(binder);
            return nullptr;
        }
        gBinder = binder;
    }
    AIBinder_incStrong(gBinder);
    return gBinder;
}

void dropPerf(AIBinder* binder) {
    std::lock_guard<std::mutex> lock(gLock);
    if (gBinder == binder) {
        AIBinder_decStrong(gBinder);
        gBinder = nullptr;
    }
}

binder_status_t writeReserved(AParcel* in, bool hint) {
    const int32_t reserved[] = {gettid(), getpid(), 0};
    return AParcel_writeInt32Array(in, reserved, hint ? 3 : 2);
}

// Sends one transaction and reads an int32 result when hasReturn is set.
// Returns kFailed if the HAL is missing or the call fails.
template <typename Writer>
int transact(transaction_code_t code, bool hasReturn, Writer&& write) {
    AIBinder* binder = getPerf();
    if (binder == nullptr) return kFailed;

    int ret = kFailed;
    AParcel* in = nullptr;
    AParcel* out = nullptr;
    binder_status_t status = AIBinder_prepareTransaction(binder, &in);
    if (status == STATUS_OK) status = write(in);
    if (status == STATUS_OK) {
        // Takes ownership of in, even on failure.
        status = AIBinder_transact(binder, code, &in, &out, kFlagPrivateVendor);
        in = nullptr;
    }
    if (status == STATUS_OK) {
        AStatus* remote = nullptr;
        status = AParcel_readStatusHeader(out, &remote);
        if (status == STATUS_OK && AStatus_isOk(remote)) {
            int32_t value = 0;
            if (!hasReturn) {
                ret = 0;
            } else if (AParcel_readInt32(out, &value) == STATUS_OK) {
                ret = value;
            }
        }
        if (remote != nullptr) AStatus_delete(remote);
    }
    if (status == STATUS_DEAD_OBJECT) dropPerf(binder);
    if (status != STATUS_OK) ALOGW("perf2 transaction %u failed: %d", code, status);

    if (in != nullptr) AParcel_delete(in);
    if (out != nullptr) AParcel_delete(out);
    AIBinder_decStrong(binder);
    return ret;
}

}  // namespace

extern "C" {

int perf_lock_acq(int handle, int duration, int list[], int numArgs) {
    if (numArgs < 0 || numArgs > kMaxArgs || (numArgs > 0 && list == nullptr)) return kFailed;
    return transact(TX_PERF_LOCK_ACQUIRE, true, [&](AParcel* in) {
        binder_status_t s = AParcel_writeInt32(in, handle);
        if (s == STATUS_OK) s = AParcel_writeInt32(in, duration);
        if (s == STATUS_OK) s = AParcel_writeInt32Array(in, list, numArgs);
        if (s == STATUS_OK) s = writeReserved(in, false);
        return s;
    });
}

int perf_lock_rel(int handle) {
    if (handle <= 0) return kFailed;
    return transact(TX_PERF_LOCK_RELEASE, false, [&](AParcel* in) {
        binder_status_t s = AParcel_writeInt32(in, handle);
        if (s == STATUS_OK) s = writeReserved(in, false);
        return s;
    });
}

int perf_hint(int hint, const char* pkg, int duration, int type) {
    if (pkg == nullptr) pkg = "";
    return transact(TX_PERF_HINT, true, [&](AParcel* in) {
        binder_status_t s = AParcel_writeInt32(in, hint);
        if (s == STATUS_OK) s = AParcel_writeString(in, pkg, strlen(pkg));
        if (s == STATUS_OK) s = AParcel_writeInt32(in, duration);
        if (s == STATUS_OK) s = AParcel_writeInt32(in, type);
        if (s == STATUS_OK) s = writeReserved(in, true);
        return s;
    });
}

}  // extern "C"
