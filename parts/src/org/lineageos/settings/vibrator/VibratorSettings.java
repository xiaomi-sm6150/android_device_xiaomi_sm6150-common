package org.lineageos.settings.vibrator;

import androidx.preference.PreferenceFragment;
import androidx.preference.PreferenceManager;
import androidx.preference.SwitchPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceCategory;
import android.content.Context;
import android.os.Bundle;
import android.view.View;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.app.ActionBar;
import android.provider.Settings;
import android.util.Log;
import android.os.UserHandle;
import android.widget.TextView;
import android.widget.Toast;
import com.android.settingslib.widget.LayoutPreference;
import com.android.settingslib.widget.FooterPreference;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.BufferedWriter;
import java.io.FileWriter;

import org.lineageos.settings.R;

public class VibratorSettings extends PreferenceFragment implements
        SeekBar.OnSeekBarChangeListener, View.OnClickListener {

    private static final String TAG = "VibratorSettings";
    private static final boolean DEBUG = false;
    private static final String VIB_NODE = "/sys/devices/platform/soc/c440000.qcom,spmi/spmi-0/spmi0-01/c440000.qcom,spmi:qcom,pm6150@1:qcom,vibrator@5300/leds/vibrator/vtg_level";
    public static final String VIB_STRENGTH = "vib_strength";
    public static final String VIB_PREFERENCE = "vib_preference";
    private static final String SEEKBAR_PROGRESS = "seek_bar_progress";
    private static final int defaultValue = 2100;
    private static final int minValue = 2000;
    private static final int maxValue = 3600;

    private static Context mContext;
    private static TextView mSeekBarValue;
    private static SeekBar mSeekBar;
    private static ImageButton resetButton;
    private Vibrator mVibrator;
    private LayoutPreference seekbarPreference;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        mContext = getContext();
        setPreferencesFromResource(R.xml.vibration_control_settings, rootKey);
        final ActionBar actionBar = getActivity().getActionBar();
        actionBar.setDisplayHomeAsUpEnabled(true);
        mVibrator = (Vibrator) mContext.getSystemService(Context.VIBRATOR_SERVICE);
        seekbarPreference = findPreference(VIB_PREFERENCE);
        resetButton  = seekbarPreference.findViewById(R.id.reset);
        mSeekBar = seekbarPreference.findViewById(R.id.seekbar);
        mSeekBarValue = seekbarPreference.findViewById(R.id.value);
        resetButton.setOnClickListener(this);
        mSeekBar.setOnSeekBarChangeListener(this);
        mSeekBar.setMax(maxValue - minValue);
        int progress = Settings.System.getIntForUser(mContext.getContentResolver(), SEEKBAR_PROGRESS, 0, UserHandle.USER_CURRENT);
        if (progress != 0) {
            mSeekBar.setProgress(progress);
            mSeekBarValue.setText(String.valueOf((progress + minValue)*100 / maxValue) + "%");}
        else {
            mSeekBar.setProgress(defaultValue - minValue);
            mSeekBarValue.setText(String.valueOf((defaultValue)*100 / maxValue) + "%");}
        Preference footerPreference = findPreference(FooterPreference.KEY_FOOTER);
        footerPreference.setTitle(mContext.getResources().getString(R.string.vibration_control_description));
        resetButton.setEnabled(isSupported());
        mSeekBar.setEnabled(isSupported());
        mSeekBarValue.setEnabled(isSupported());
    }

    private static boolean isSupported() {
        boolean fileExists = new File(VIB_NODE).exists();
        boolean fileWritable = new File(VIB_NODE).canWrite();
        if (!fileExists) Log.e(TAG, "No such file " + VIB_NODE);
        if (!fileWritable) Log.e(TAG, "Could not write to file " + VIB_NODE);
        return fileExists && fileWritable;
    }

    private static void writeNode(int value) {
        if (DEBUG) Log.d(TAG, "Value " + value);
        if (!isSupported()) {
            return;
        }
        BufferedWriter writer = null;
        try {
            writer = new BufferedWriter(new FileWriter(VIB_NODE));
            writer.write(String.valueOf(value));
        } catch (FileNotFoundException e) {
        } catch (IOException e) {
        } finally {
            try {
                if (writer != null) {
                    writer.close();
                }
            } catch (IOException e) {
                // Ignored, not much we can do anyway
            }
        }
    }

    private void setValue(int newValue, boolean withFeedback) {
        writeNode(newValue);
        Settings.System.putIntForUser(mContext.getContentResolver(), VIB_STRENGTH, newValue, UserHandle.USER_CURRENT);
        if (mVibrator.hasVibrator()) {
            mVibrator.vibrate(VibrationEffect.createOneShot(30, VibrationEffect.DEFAULT_AMPLITUDE));
        }
    }

    public static void restoreValue(Context context) {
        if (!isSupported()) {
            return;
        }
        final int storedValue = Settings.System.getIntForUser(context.getContentResolver(), VIB_STRENGTH, defaultValue,UserHandle.USER_CURRENT);
        writeNode(storedValue);
    }

    @Override
    public void onClick(View v) {
        mSeekBar.setProgress(defaultValue - minValue);
        mSeekBarValue.setText(String.valueOf((defaultValue)*100 / maxValue) + "%");
        Settings.System.putIntForUser(mContext.getContentResolver(), SEEKBAR_PROGRESS, defaultValue - minValue, UserHandle.USER_CURRENT);
        setValue((defaultValue), true);
        Toast toast = Toast.makeText(mContext, R.string.vibration_reset, Toast.LENGTH_LONG);
        toast.show();
    }

    @Override
    public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
        if (fromUser) {
        if (DEBUG) Log.d(TAG, "progress " + progress);
        Settings.System.putIntForUser(mContext.getContentResolver(), SEEKBAR_PROGRESS, progress + 1, UserHandle.USER_CURRENT);
        mSeekBarValue.setText(String.valueOf((progress + minValue)*100 / maxValue) + "%");
            setValue((progress + minValue), true);
        }
    }

    @Override
    public void onStartTrackingTouch(SeekBar seekBar) {
    }

    @Override
    public void onStopTrackingTouch(SeekBar seekBar) {
    }
}
