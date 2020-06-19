package org.lineageos.settings.vibrator;

import android.os.Bundle;
import com.android.settingslib.collapsingtoolbar.CollapsingToolbarBaseActivity;
import com.android.settingslib.collapsingtoolbar.R;

public class VibratorSettingsActivity extends CollapsingToolbarBaseActivity {

    private static final String TAG = "VibratorSettingsActivity";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getFragmentManager().beginTransaction().replace(R.id.content_frame, new VibratorSettings(), TAG)
        .commit();
    }
}
