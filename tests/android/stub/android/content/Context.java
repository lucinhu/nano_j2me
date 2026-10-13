package android.content;

/**
 * Just enough of {@code android.content.Context} for the port's plain-Java
 * classes to compile off the device.
 *
 * <p>The off-device tests (tools/check_android_java.sh) run the keypad and the
 * manifest naming on a desktop JVM, where there is no SDK on the classpath.
 * {@code PadConfig} mentions a {@code Context} only in its preference
 * load/save, and no test may call either: this stub throws rather than
 * pretending to store anything.
 */
public class Context {

    public static final int MODE_PRIVATE = 0;

    public SharedPreferences getSharedPreferences(String name, int mode) {
        throw new UnsupportedOperationException("stub Context: no preferences off the device");
    }
}
