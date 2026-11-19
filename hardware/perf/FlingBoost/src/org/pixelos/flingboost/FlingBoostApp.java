/*
 * SPDX-FileCopyrightText: The PixelOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.pixelos.flingboost;

import android.app.Application;

/** Persistent system app; the service lives as long as the process. */
public class FlingBoostApp extends Application {
    private FlingBoostService mService;

    @Override
    public void onCreate() {
        super.onCreate();
        mService = new FlingBoostService(this);
        mService.start();
    }
}
