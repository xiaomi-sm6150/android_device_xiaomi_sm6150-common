/*
 * SPDX-FileCopyrightText: The PixelOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.pixelos.flingboost;

import android.os.IBinder;
import android.os.Parcel;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;

/**
 * Minimal client for vendor.qti.hardware.perf2.IPerf/default.
 *
 * The perf2 HAL ships only an NDK stub, so the calls are written by hand,
 * with the same transaction codes, argument order and reserved contents
 * ({tid, pid}, plus a 0 for hints) as the vendor client
 * (hardware/perf/libqti-perfd-client). Requests are ignored by the HAL while
 * the display is off.
 */
final class PerfHal {
    private static final String TAG = "FlingBoost";

    private static final String SERVICE = "vendor.qti.hardware.perf2.IPerf/default";
    private static final String DESCRIPTOR = "vendor.qti.hardware.perf2.IPerf";

    // FIRST_CALL_TRANSACTION + position in the (alphabetical) interface.
    private static final int TX_PERF_HINT = IBinder.FIRST_CALL_TRANSACTION + 6;
    private static final int TX_PERF_LOCK_RELEASE = IBinder.FIRST_CALL_TRANSACTION + 12;

    private IBinder mBinder;

    private final IBinder.DeathRecipient mDeath = () -> {
        synchronized (this) {
            mBinder = null;
        }
    };

    private synchronized IBinder get() {
        if (mBinder != null && mBinder.isBinderAlive()) return mBinder;
        mBinder = ServiceManager.checkService(SERVICE);
        if (mBinder != null) {
            try {
                mBinder.linkToDeath(mDeath, 0);
            } catch (RemoteException e) {
                mBinder = null;
            }
        }
        return mBinder;
    }

    private synchronized void drop(IBinder binder) {
        if (mBinder == binder) mBinder = null;
    }

    /** Returns the lock handle (> 0), or -1 on failure. */
    int perfHint(int hint, String pkg, int duration, int type) {
        final IBinder binder = get();
        if (binder == null) return -1;
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(hint);
            data.writeString(pkg != null ? pkg : "");
            data.writeInt(duration);
            data.writeInt(type);
            data.writeIntArray(new int[] {Process.myTid(), Process.myPid(), 0});
            binder.transact(TX_PERF_HINT, data, reply, 0);
            reply.readException();
            return reply.readInt();
        } catch (RemoteException | RuntimeException e) {
            Log.w(TAG, "perfHint 0x" + Integer.toHexString(hint) + " failed", e);
            drop(binder);
            return -1;
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    void perfLockRelease(int handle) {
        if (handle <= 0) return;
        final IBinder binder = get();
        if (binder == null) return;
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(handle);
            data.writeIntArray(new int[] {Process.myTid(), Process.myPid()});
            binder.transact(TX_PERF_LOCK_RELEASE, data, reply, 0);
            reply.readException();
        } catch (RemoteException | RuntimeException e) {
            Log.w(TAG, "perfLockRelease " + handle + " failed", e);
            drop(binder);
        } finally {
            reply.recycle();
            data.recycle();
        }
    }
}
