package neuron.mod;

import android.app.Application;
import android.os.Handler;
import android.os.Looper;
import android.widget.Toast;

/** Shows a toast from native code; grabs the Application without needing a Context passed in. */
public class Toaster {
    public static void show(final String msg, final boolean isLong) {
        try {
            final Application app = (Application) Class.forName("android.app.ActivityThread")
                    .getMethod("currentApplication").invoke(null);
            if (app == null) return;
            new Handler(Looper.getMainLooper()).post(new Runnable() {
                public void run() {
                    Toast.makeText(app, msg, isLong ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show();
                }
            });
        } catch (Throwable t) {
            // never let a toast break the game
        }
    }
}
