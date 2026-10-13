/*
 * nano_j2me - PlatformSensor.java
 *
 * The platform's sensors, behind the port's JSR 256 (javax.microedition.sensor)
 * hooks.
 *
 * WHY THE URL IS THE ONLY KEY
 * ---------------------------
 * A JSR 256 sensor is named by its url, and that string is what a midlet keeps
 * when it lets one sensor go and asks for another later:
 *
 *     sensor:<quantity>;context=<context>;model=<model>
 *
 * The runtime cannot hold a Sensor: that is a Java object on a heap the C side
 * does not manage.  So list() renders every sensor this device really has as
 * one line of text, and open() takes that same text back and answers with a
 * long handle - the shape MidiPlayer.java uses, and for the same reason.
 *
 * The model is part of the name, not of the lookup: a url whose quantity is
 * one of the ones mapped below opens after the model has changed, which is
 * what makes a url written down by an older list() still work.
 *
 * WHY ONLY EIGHT TYPES
 * --------------------
 * These are the eight whose readings are the hardware's own.  Android's
 * TYPE_GRAVITY, TYPE_LINEAR_ACCELERATION and TYPE_ROTATION_VECTOR are derived
 * from them: mapping those too would offer a midlet a second
 * "sensor:acceleration" that is not the accelerometer, and JSR 256 has one
 * quantity per physical thing.  A phone whose firmware has them is still read
 * through the sensor they came from.
 *
 * WHY A LISTENER AND A LAST SAMPLE
 * --------------------------------
 * SensorManager has no "read now": a sensor is a stream, and the framework's
 * own answer to a one-shot read is a listener that ignores everything after
 * the first event.  open() registers one listener per handle, latest() hands
 * back the last event - its values, then its timestamp in milliseconds, which
 * is what javax.microedition.sensor.Data carries.  Nothing here queues, so a
 * game that stops asking cannot make the port grow a backlog of samples; and
 * the callback takes no lock, so the sensor thread can never be made to wait
 * behind a caller of latest().
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Six statics, called on the VM thread:
 *
 *   init(Context)           keep the application context, find the service
 *   list()                  one TAB-separated line per sensor, never null
 *   open(url, periodMs)     a handle > 0, or 0 when there is no such sensor
 *   latest(handle)          the channels plus one timestamp, or null
 *   close(handle)           unregister; closing twice is closing once
 *   count()                 how many sensors list() would offer
 *
 * Every failure is a value, never an exception: 0, null or an empty array is
 * "the platform has nothing for you", which the runtime answers by not
 * offering that sensor to the midlet at all.
 *
 * The application context is kept, never the Activity: this class holds it in
 * a static field, and a static field holding an Activity pins that Activity -
 * and its whole view tree - for as long as the process lives.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.util.Log;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

final class PlatformSensor {

    private static final String TAG = "nj2me";

    /*
     * Every url this file produces and accepts starts here.  It is the
     * "sensor:" javax.microedition.sensor's SensorInfo.getUrl() carries, so a
     * url the runtime wrote down on one device still reads as a sensor url on
     * the next one.
     */
    private static final String URL_PREFIX = "sensor:";

    /*
     * The fastest this class will register a listener.  A JSR 256 period is in
     * milliseconds and its "no preference" is negative, which for a phone
     * sensor means the fastest rate the part has - the most battery a game can
     * spend doing nothing.  Ten milliseconds is 100 Hz, past every J2ME use for
     * a sensor, and a part whose own minimum delay is larger still wins; see
     * periodUs().
     */
    private static final int MIN_PERIOD_US = 10000;

    /* See logOnce(). */
    private static final int LOG_ONCE_MAX = 8;
    private static final Set<String> LOGGED = new HashSet<String>();

    /*
     * Handle -> the one open listener for it.  Handles are ids into this map,
     * never Java objects handed over: the runtime stores the id in an int and
     * must not be given a reference it cannot release.  nextHandle is shared by
     * every open handle so that a stale id can never name a new sensor.
     */
    private static final Map<Long, Entry> OPEN = new HashMap<Long, Entry>();
    private static long nextHandle = 1L;

    /*
     * Application context only.  This is a static field, so an Activity here
     * would keep the Activity - and its whole view tree - alive for the life of
     * the process; the SensorManager is a binder client and can be fetched
     * once, because it never goes stale.
     */
    private static Context appContext;
    private static SensorManager manager;

    /*
     * The Android sensors this port maps, in the order list() reports them.
     * The channels array is the channel order of list()'s channel specs and of
     * latest()'s values, and it has one name per axis: three for the physical
     * vector sensors, one for the scalar ones.
     */
    private static final Spec[] SPECS = {
            new Spec(Sensor.TYPE_ACCELEROMETER, "acceleration", "user", "m/s^2",
                    new String[]{"axis_x", "axis_y", "axis_z"}),
            new Spec(Sensor.TYPE_GYROSCOPE, "angular_velocity", "user", "rad/s",
                    new String[]{"axis_x", "axis_y", "axis_z"}),
            new Spec(Sensor.TYPE_MAGNETIC_FIELD, "magnetic_field", "ambient",
                    "T", new String[]{"axis_x", "axis_y", "axis_z"}),
            new Spec(Sensor.TYPE_LIGHT, "luminance", "ambient", "lux",
                    new String[]{"luminance"}),
            new Spec(Sensor.TYPE_PRESSURE, "pressure", "ambient", "Pa",
                    new String[]{"pressure"}),
            new Spec(Sensor.TYPE_AMBIENT_TEMPERATURE, "ambient_temperature",
                    "ambient", "C", new String[]{"temperature"}),
            new Spec(Sensor.TYPE_RELATIVE_HUMIDITY, "humidity", "ambient", "%",
                    new String[]{"humidity"}),
            new Spec(Sensor.TYPE_PROXIMITY, "proximity", "device", "m",
                    new String[]{"proximity"}),
    };

    private PlatformSensor() {
    }

    /**
     * Called once from the Activity, before the VM can ask for a sensor.
     * Keeps the application context and finds the sensor service; both are
     * then good for the life of the process, so there is nothing to redo.
     */
    static void init(Context ctx) {
        if (ctx == null) {
            return;
        }
        try {
            Context app = ctx.getApplicationContext();
            appContext = app != null ? app : ctx;
            manager = (SensorManager) appContext
                    .getSystemService(Context.SENSOR_SERVICE);
            if (manager == null) {
                logOnce("init-none", "this device has no sensor service");
            }
        } catch (Throwable t) {
            manager = null;
            logOnce("init:" + t.getClass().getName(),
                    "the platform has no sensor service (" + t + ")");
        }
    }

    /**
     * Every sensor this device has, one line each, TAB-separated, in this
     * order:
     *
     *     url, quantity, context, model, maxRateHz, channelSpecs
     *
     * <p>{@code channelSpecs} is one {@code name:unit:min:max:resolution} per
     * channel, comma-separated - three of them for an accelerometer, one for a
     * light sensor.  Numbers are plain decimals that {@code strtod()} reads:
     * Android reports one symmetric range per sensor, so every channel gets
     * min {@code -maxRange} and max {@code +maxRange}.
     *
     * <p>Never null.  An empty array means the device has none of the mapped
     * types, or init() has not run: a runtime with no sensors to offer, which
     * is the answer a phone without them should get.
     */
    static String[] list() {
        if (appContext == null || manager == null) {
            logOnce("list-none", "no sensors are available: init() has not run,"
                    + " or this device has no sensor service");
            return new String[0];
        }
        try {
            List<String> lines = new ArrayList<String>(SPECS.length);
            for (Spec spec : SPECS) {
                Sensor sensor = manager.getDefaultSensor(spec.type);
                if (sensor == null) {
                    continue;
                }
                try {
                    lines.add(line(sensor, spec));
                } catch (Throwable t) {
                    /*
                     * One sensor the platform cannot describe must not hide
                     * every other one: the runtime loses a quantity, not the
                     * whole list.
                     */
                    logOnce("describe:" + t.getClass().getName(),
                            "the platform cannot describe its " + spec.quantity
                                    + " sensor (" + t + ")");
                }
            }
            return lines.toArray(new String[lines.size()]);
        } catch (Throwable t) {
            logOnce("list:" + t.getClass().getName(),
                    "no sensor list could be read (" + t + ")");
            return new String[0];
        }
    }

    /**
     * Open the sensor the url names and start its listener.
     *
     * <p>{@code periodMs} is the JSR 256 sampling period in milliseconds.  It
     * is raised to the part's own minimum delay when that is larger, and to
     * {@link #MIN_PERIOD_US}, because a period the sensor cannot keep is a
     * registration the framework refuses outright.
     *
     * <p>The answer is a handle > 0, or 0 when the url names no sensor this
     * device has - a value, not an error: the runtime then offers the midlet
     * no SensorConnector for it.
     */
    static synchronized long open(String url, int periodMs) {
        if (appContext == null || manager == null) {
            logOnce("open-none", "a sensor cannot be opened before init(), or"
                    + " this device has no sensor service");
            return 0L;
        }
        try {
            Spec spec = specFor(quantityOf(url));
            if (spec == null) {
                logOnce("open-url", "this is not a sensor url the port maps");
                return 0L;
            }
            Sensor sensor = find(manager, url, spec);
            if (sensor == null) {
                logOnce("open-none:" + spec.quantity, "this device has no "
                        + spec.quantity + " sensor");
                return 0L;
            }
            int periodUs = periodUs(sensor, periodMs);
            Entry entry = new Entry(sensor, spec);
            if (!manager.registerListener(entry, sensor, periodUs)) {
                logOnce("open-register:" + spec.quantity, "the platform refused"
                        + " a listener for " + spec.quantity);
                return 0L;
            }
            long handle = nextHandle++;
            OPEN.put(Long.valueOf(handle), entry);
            /*
             * A MIDlet usually opens a connection and reads it in the same
             * breath, and on a handset the reading is already there: JSR 256's
             * getData() answers with the sensor's current value.  This
             * platform only hands a value to a listener, so wait here for the
             * first event, bounded by one period and by 100 ms.  The wait is
             * on this class's monitor, which the callback takes only to
             * notify; the sample itself arrives through Entry's volatile
             * fields and is never blocked by a reader.
             */
            waitForFirstSample(entry, periodMs);
            return handle;
        } catch (Throwable t) {
            logOnce("open:" + t.getClass().getName(),
                    "a sensor could not be opened (" + t + ")");
            return 0L;
        }
    }

    /**
     * Wait, briefly, for the handle's first sample.  Called with the class
     * monitor held; returns as soon as a value is published or when the wait
     * runs out, whichever comes first, so a sensor that reports slowly (or not
     * at all) cannot hold a MIDlet up for longer than the bound.
     */
    private static void waitForFirstSample(Entry entry, int periodMs) {
        long boundMs = Math.min(Math.max(periodMs, 20), 100);
        long deadline = System.nanoTime() + boundMs * 1000000L;

        while (entry.values == null) {
            long leftNs = deadline - System.nanoTime();
            if (leftNs <= 0L) {
                return;
            }
            try {
                PlatformSensor.class.wait(leftNs / 1000000L + 1L);
            } catch (InterruptedException ie) {
                Thread.currentThread().interrupt();
                return;
            }
        }
    }

    /**
     * The handle's last sample: one double per channel, in channel order,
     * followed by one more - the event timestamp in milliseconds.  A
     * three-axis sensor therefore answers with four doubles.
     *
     * <p>null when the handle is unknown or the sensor has not reported yet.
     * The array is newly allocated every call, because the caller carries it
     * across JNI while the sensor thread may already be publishing the next
     * sample over the one it came from.
     */
    static synchronized double[] latest(long handle) {
        Entry entry = OPEN.get(Long.valueOf(handle));
        if (entry == null) {
            return null;
        }
        try {
            double[] values = entry.values;
            if (values == null) {
                return null;
            }
            double[] out = new double[values.length + 1];
            System.arraycopy(values, 0, out, 0, values.length);
            out[values.length] = entry.timestampNs / 1000000.0;
            return out;
        } catch (Throwable t) {
            logOnce("latest:" + t.getClass().getName(),
                    "a sensor sample could not be read (" + t + ")");
            return null;
        }
    }

    /**
     * Stop the handle's listener and forget the handle.  Closing a handle that
     * was never opened, or was already closed, does nothing.
     */
    static synchronized void close(long handle) {
        Entry entry = OPEN.remove(Long.valueOf(handle));
        if (entry == null || manager == null) {
            return;
        }
        try {
            manager.unregisterListener(entry);
        } catch (Throwable t) {
            logOnce("close:" + t.getClass().getName(),
                    "a sensor listener could not be unregistered (" + t + ")");
        }
    }

    /**
     * How many sensors {@link #list} would offer.  Used for logging and tests,
     * and answered by list() itself so that the two can never disagree.
     */
    static synchronized int count() {
        return list().length;
    }

    /*
     * One mapped Android sensor: the type to ask the platform for, and the
     * JSR 256 names its readings are reported under.
     */
    private static final class Spec {

        final int type;
        final String quantity;
        final String context;
        final String unit;
        final String[] channels;

        Spec(int type, String quantity, String context, String unit,
             String[] channels) {
            this.type = type;
            this.quantity = quantity;
            this.context = context;
            this.unit = unit;
            this.channels = channels;
        }
    }

    /*
     * One open sensor: the Sensor, the spec its channels and unit come from,
     * and the last event.
     *
     * This object is also the listener.  One listener per handle is then a
     * property of the type rather than a promise - there is no second listener
     * to register, leak or unregister - and close() has exactly one thing to
     * take down with the handle.
     *
     * The two volatile fields are written by the sensor thread and read by
     * whatever thread calls latest().  The array is never written again after
     * it is published, so a reader sees the previous sample whole or the
     * current one whole, and never a half-filled one.
     */
    private static final class Entry implements SensorEventListener {

        final Sensor sensor;
        final Spec spec;
        volatile double[] values;
        volatile long timestampNs;

        Entry(Sensor sensor, Spec spec) {
            this.sensor = sensor;
            this.spec = spec;
        }

        public void onSensorChanged(SensorEvent event) {
            try {
                /*
                 * A fresh array: event.values belongs to the platform and is
                 * reused for the next event, while this one has to outlive the
                 * callback and be read by latest().
                 */
                /*
                 * Exactly one value per channel, always: a 3-axis part answers
                 * a 4-double latest() with 4 doubles, never 2, because the C
                 * side sizes its read from the channel count list() published.
                 * A part reporting a different count contradicts its own
                 * SensorInfo: the values it did send are kept, the channels it
                 * left out read as 0, and the disagreement is logged once.
                 */
                double[] copy = new double[spec.channels.length];
                int n = Math.min(event.values.length, copy.length);
                for (int i = 0; i < n; i++) {
                    copy[i] = event.values[i];
                }
                if (event.values.length != copy.length) {
                    logOnce("channels:" + spec.quantity, "the platform reports "
                            + event.values.length + " values for the "
                            + spec.channels.length + " channels of "
                            + spec.quantity + "; the channel count wins");
                }
                values = copy;
                timestampNs = event.timestamp;
                /*
                 * Wake an open() that is waiting for this first sample.  The
                 * class monitor is taken only for the notify itself; open()
                 * releases it while it waits, so the callback is never held up
                 * by a reader.
                 */
                synchronized (PlatformSensor.class) {
                    PlatformSensor.class.notifyAll();
                }
            } catch (Throwable t) {
                logOnce("event:" + t.getClass().getName(),
                        "a sensor event could not be read (" + t + ")");
            }
        }

        /* JSR 256 has no accuracy notion, and nothing here needs one. */
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }
    }

    /*
     * One line of list(): the six fields, tabs between them.  The model is
     * built once because the url carries it too.
     */
    private static String line(Sensor sensor, Spec spec) {
        String model = modelOf(sensor);
        return urlOf(spec, model) + "\t" + spec.quantity + "\t" + spec.context
                + "\t" + model + "\t" + maxRateHz(sensor) + "\t"
                + channelSpecs(sensor, spec);
    }

    /* The JSR 256 url a midlet stores and asks for again. */
    private static String urlOf(Spec spec, String model) {
        return URL_PREFIX + spec.quantity + ";context=" + spec.context
                + ";model=" + model;
    }

    /*
     * Build.MODEL and the platform's own name for the part, joined and made
     * url-safe.  A space becomes '_' because a url cannot hold one; a tab or a
     * newline becomes '_' too, because list() is read back by splitting the
     * line on tabs, and a field the platform can put anything into must not be
     * able to add a field or hide one.
     */
    private static String modelOf(Sensor sensor) {
        String phone = Build.MODEL;
        String name;
        try {
            name = sensor.getName();
        } catch (Throwable t) {
            name = null;
        }
        return sanitize((phone == null ? "unknown" : phone) + "-"
                + (name == null ? "unknown" : name));
    }

    private static String sanitize(String text) {
        StringBuilder out = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            out.append(c <= ' ' ? '_' : c);
        }
        return out.toString();
    }

    /*
     * The fastest rate this sensor reports, in whole hertz, from the largest
     * gap the platform will leave between two events.  getMaxDelay() is zero
     * on a part that reports only when its reading changes - a proximity
     * sensor - which is not a rate at all; one hertz is the honest floor.
     */
    private static int maxRateHz(Sensor sensor) {
        int delayUs = sensor.getMaxDelay();
        if (delayUs <= 0) {
            return 1;
        }
        int hz = 1000000 / delayUs;
        return hz < 1 ? 1 : hz;
    }

    /*
     * One channel per axis: its name, its unit, the range on either side of
     * zero and the resolution within it.  JSR 256 wants a range as min and
     * max, while Android reports a single symmetric maximum range, so both
     * ends come from the same number.
     */
    private static String channelSpecs(Sensor sensor, Spec spec) {
        double range = sensor.getMaximumRange();
        double resolution = sensor.getResolution();
        StringBuilder out = new StringBuilder(spec.channels.length * 32);
        for (int i = 0; i < spec.channels.length; i++) {
            if (i > 0) {
                out.append(',');
            }
            out.append(spec.channels[i]).append(':').append(spec.unit)
                    .append(':').append(num(-range)).append(':').append(num(range))
                    .append(':').append(num(resolution));
        }
        return out.toString();
    }

    /*
     * A number the C side can read with strtod(): plain decimal digits, never
     * an exponent and never a grouping separator, with no locale anywhere in
     * the path - a phone set to a comma-decimal locale must still produce a
     * string this runtime can parse.  Six places is finer than any sensor's
     * resolution and coarse enough that the float rounding behind a value like
     * 0.001 does not turn into sixteen digits; the trim keeps 9.80665 from
     * being written as 9.806650.
     */
    private static String num(double value) {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            return "0";
        }
        try {
            return BigDecimal.valueOf(value).setScale(6, RoundingMode.HALF_UP)
                    .stripTrailingZeros().toPlainString();
        } catch (Throwable t) {
            /* A finite double cannot get here; list() must not fail if one
             * somehow does. */
            return String.valueOf(value);
        }
    }

    /*
     * The sampling period to register, in microseconds.  registerListener
     * counts in microseconds and refuses a period below the part's own minimum
     * delay, so:
     *
     *   - getMinDelay() wins whenever it is larger, because asking for events
     *     the part cannot produce is a registration the framework declines;
     *   - MIN_PERIOD_US is the floor for a part that reports no minimum, so
     *     "as fast as you like" cannot become a busy loop;
     *   - a period longer than the framework's own parameter is capped there.
     */
    private static int periodUs(Sensor sensor, int periodMs) {
        long us = (long) periodMs * 1000L;
        int minUs = sensor.getMinDelay();
        if (us < minUs) {
            us = minUs;
        }
        if (us < MIN_PERIOD_US) {
            us = MIN_PERIOD_US;
        }
        if (us > Integer.MAX_VALUE) {
            us = Integer.MAX_VALUE;
        }
        return (int) us;
    }

    /*
     * The quantity inside a url, or null when this is not a sensor url at all.
     * Only the head is read here, so a url from an older or newer list() with
     * other fields still yields its quantity - the part of the name that
     * outlives a model change.
     */
    private static String quantityOf(String url) {
        if (url == null || !url.startsWith(URL_PREFIX)) {
            return null;
        }
        int start = URL_PREFIX.length();
        int end = url.indexOf(';', start);
        if (end < 0) {
            end = url.length();
        }
        if (end <= start) {
            return null;
        }
        return url.substring(start, end);
    }

    private static Spec specFor(String quantity) {
        if (quantity == null) {
            return null;
        }
        for (Spec spec : SPECS) {
            if (spec.quantity.equals(quantity)) {
                return spec;
            }
        }
        return null;
    }

    /*
     * The exact sensor list() offered, or the default one for its type.
     *
     * The first walk regenerates every url exactly as list() does and takes
     * the sensor whose url is the one asked for: the same part under the same
     * model, so the runtime gets the sensor it was told about.  When nothing
     * matches - the runtime kept a url across a device change, or an older
     * list() described the same quantity differently - the quantity still
     * names an Android type, and that type's default sensor is what the url
     * meant.  A quantity this file does not map has been refused already.
     */
    private static Sensor find(SensorManager manager, String url, Spec spec) {
        for (Spec other : SPECS) {
            Sensor sensor = manager.getDefaultSensor(other.type);
            if (sensor != null && url.equals(urlOf(other, modelOf(sensor)))) {
                return sensor;
            }
        }
        return manager.getDefaultSensor(spec.type);
    }

    /*
     * A game asks for its sensors every time a midlet starts, so a device that
     * cannot answer would otherwise write a line per attempt.  One line per
     * kind of failure, and the set is capped because nothing may grow a static
     * field one distinct failure at a time.
     */
    private static void logOnce(String kind, String detail) {
        synchronized (LOGGED) {
            if (LOGGED.size() >= LOG_ONCE_MAX || !LOGGED.add(kind)) {
                return;
            }
        }
        Log.w(TAG, "sensor: " + detail);
    }
}
