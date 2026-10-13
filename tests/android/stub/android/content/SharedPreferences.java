package android.content;

/** The slice of {@code SharedPreferences} that {@code PadConfig} uses. */
public interface SharedPreferences {

    String getString(String key, String defValue);

    float getFloat(String key, float defValue);

    boolean getBoolean(String key, boolean defValue);

    int getInt(String key, int defValue);

    java.util.Map<String, ?> getAll();

    Editor edit();

    interface Editor {
        Editor putString(String key, String value);

        Editor putFloat(String key, float value);

        Editor putBoolean(String key, boolean value);

        Editor putInt(String key, int value);

        Editor remove(String key);

        Editor clear();

        void apply();

        boolean commit();
    }
}
