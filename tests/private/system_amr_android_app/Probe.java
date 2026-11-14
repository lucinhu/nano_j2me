/* SPDX-License-Identifier: MIT */
package org.nanoj2me.amrprobe;
import android.app.Activity;
import android.os.Bundle;

/** Test-only host for the real native Android AMR service probe. */
public class Probe extends Activity {
    static { System.loadLibrary("amrprobe"); }
    private static native int run(String path);
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        new Thread(new Runnable() {
            public void run() {
                int result = Probe.run(getFilesDir().getAbsolutePath() + "/result.log");
                android.util.Log.i("NJAMRProbe", "RESULT=" + result);
            }
        }).start();
    }
}
