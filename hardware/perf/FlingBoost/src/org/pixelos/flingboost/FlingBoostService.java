/*
 * SPDX-FileCopyrightText: The PixelOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.pixelos.flingboost;

import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.display.DisplayManager;
import android.os.Binder;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Message;
import android.os.Parcel;
import android.os.Process;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;
import android.util.SparseArray;
import android.view.Display;

/**
 * The "flingboost" service behind android.widget.FlingBoost.
 *
 * OverScroller reports FLING_START(durationMs, type) when a fling starts and
 * FLING_END when it is aborted early; both are oneway. A start becomes a perf2
 * fling hint (0x1080) for the fling's duration, an end releases it.
 *
 * perf2 picks the 0x1080 resources by panel refresh rate, which it reads from
 * the AIDL DisplayConfig service. The lahaina display stack only has the HIDL
 * one, so this service also sends perf2 the refresh rate (0x1094), as the QTI
 * SurfaceFlinger extension does on QTI ROMs.
 */
final class FlingBoostService extends Binder {
    private static final String TAG = "FlingBoost";

    static final String SERVICE = "flingboost";
    private static final String DESCRIPTOR = "android.widget.IFlingBoost";

    private static final int TRANSACTION_FLING_START = IBinder.FIRST_CALL_TRANSACTION;
    private static final int TRANSACTION_FLING_END = IBinder.FIRST_CALL_TRANSACTION + 1;

    // perf2 hints
    private static final int VENDOR_HINT_SCROLL_BOOST = 0x00001080;
    private static final int VENDOR_HINT_FPS_UPDATE = 0x00001094;

    // lahaina only has vertical (type 1) 0x1080 configs; a horizontal fling
    // needs the same CPU/GPU headroom, so boost both with them.
    private static final int SCROLL_TYPE_FLING = 1;

    // OverScroller durations are in this range; clamp anything odd.
    private static final int MIN_DURATION_MS = 50;
    private static final int MAX_DURATION_MS = 3000;

    private static final int MSG_FLING_START = 1;
    private static final int MSG_FLING_END = 2;
    private static final int MSG_DISPLAY_CHANGED = 3;

    private final Context mContext;
    private final PerfHal mPerf = new PerfHal();
    private final Handler mHandler;
    private final DisplayManager mDisplayManager;

    // Worker thread state
    private int mHandle = -1;
    private int mFps;
    private int mDisplayState = Display.STATE_UNKNOWN;
    private final SparseArray<String> mPackages = new SparseArray<>();

    private final DisplayManager.DisplayListener mDisplayListener =
            new DisplayManager.DisplayListener() {
                @Override
                public void onDisplayAdded(int displayId) {}

                @Override
                public void onDisplayRemoved(int displayId) {}

                @Override
                public void onDisplayChanged(int displayId) {
                    if (displayId == Display.DEFAULT_DISPLAY) {
                        mHandler.sendEmptyMessage(MSG_DISPLAY_CHANGED);
                    }
                }
            };

    FlingBoostService(Context context) {
        mContext = context;
        mDisplayManager = context.getSystemService(DisplayManager.class);

        final HandlerThread thread = new HandlerThread(TAG, Process.THREAD_PRIORITY_DISPLAY);
        thread.start();
        mHandler = new Handler(thread.getLooper(), this::handleMessage);
    }

    void start() {
        mDisplayManager.registerDisplayListener(mDisplayListener, mHandler);
        mHandler.sendEmptyMessage(MSG_DISPLAY_CHANGED);
        ServiceManager.addService(SERVICE, this);
        Log.i(TAG, "registered");
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        switch (code) {
            case TRANSACTION_FLING_START: {
                data.enforceInterface(DESCRIPTOR);
                final int duration = data.readInt();
                data.readInt(); // type: 1 vertical, 2 horizontal
                mHandler.obtainMessage(MSG_FLING_START, duration, Binder.getCallingUid())
                        .sendToTarget();
                return true;
            }
            case TRANSACTION_FLING_END:
                data.enforceInterface(DESCRIPTOR);
                mHandler.sendEmptyMessage(MSG_FLING_END);
                return true;
            default:
                return super.onTransact(code, data, reply, flags);
        }
    }

    private boolean handleMessage(Message msg) {
        switch (msg.what) {
            case MSG_FLING_START:
                onFlingStart(msg.arg1, msg.arg2);
                return true;
            case MSG_FLING_END:
                release();
                return true;
            case MSG_DISPLAY_CHANGED:
                onDisplayChanged();
                return true;
        }
        return false;
    }

    private void onFlingStart(int duration, int uid) {
        if (mDisplayState != Display.STATE_ON) return;
        if (duration <= 0) return;
        duration = Math.max(MIN_DURATION_MS, Math.min(duration, MAX_DURATION_MS));

        // A new fling replaces the running boost.
        release();
        mHandle = mPerf.perfHint(VENDOR_HINT_SCROLL_BOOST, packageFor(uid), duration,
                SCROLL_TYPE_FLING);
    }

    private void release() {
        if (mHandle > 0) {
            mPerf.perfLockRelease(mHandle);
            mHandle = -1;
        }
    }

    private void onDisplayChanged() {
        final Display display = mDisplayManager.getDisplay(Display.DEFAULT_DISPLAY);
        if (display == null) return;

        final int state = display.getState();
        final int fps = Math.round(display.getRefreshRate());
        final boolean turnedOn = state == Display.STATE_ON && mDisplayState != Display.STATE_ON;
        mDisplayState = state;

        if (state != Display.STATE_ON) {
            release();
            return;
        }
        // perf2 drops requests while the display is off, so resend on wakeup.
        if (fps > 0 && (fps != mFps || turnedOn)) {
            if (mPerf.perfHint(VENDOR_HINT_FPS_UPDATE, "", 0, fps) >= 0) {
                mFps = fps;
            }
        }
    }

    private String packageFor(int uid) {
        String pkg = mPackages.get(uid);
        if (pkg == null) {
            final PackageManager pm = mContext.getPackageManager();
            pkg = pm.getNameForUid(uid);
            if (pkg == null) pkg = "";
            // shared uids come back as "name:uid"; perf2 wants a package
            final int colon = pkg.indexOf(':');
            if (colon > 0) pkg = pkg.substring(0, colon);
            if (mPackages.size() > 64) mPackages.clear();
            mPackages.put(uid, pkg);
        }
        return pkg;
    }
}
