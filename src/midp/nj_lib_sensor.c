/*
 * nano_j2me - nj_lib_sensor.c
 *
 * javax.microedition.sensor: JSR 256, the Mobile Sensor API - SensorManager,
 * SensorInfo, ChannelInfo, Data, the listener interfaces, and the connection a
 * MIDlet reads samples from.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * Not for compatibility: not one of the 436 JARs in the corpus names
 * javax.microedition.sensor (the accelerometer API every handset shipped, and
 * that no J2ME game in this collection ever called).  It exists because a
 * MIDlet written today - a level, a pedometer, a tilt-controlled game - should
 * be able to ask a phone what it is doing, and because the port hook it needs
 * is four calls wide.
 *
 * ---------------------------------------------------------------------------
 * PULL AND PUSH ARE ONE CALL
 * ---------------------------------------------------------------------------
 * getData() and a DataListener are the same request - "hand me the newest
 * sample" - separated only by who asks.  The port therefore has ONE hook for
 * the sample (sensor_read) and the runtime owns everything else: the pacing
 * (sensor_frame() runs once per emulated frame and calls a listener no faster
 * than the period it asked for), the Data objects, and the bookkeeping of who
 * is listening.  A port never has to remember a MIDlet.
 *
 * A read is a snapshot, not a queue.  A port that only learns about motion
 * from a hardware event returns 0 channels until the first event arrives and
 * the newest sample after that, which is exactly what a MIDlet wants: an
 * accelerometer read once per frame is a picture of now, never a backlog.
 *
 * ---------------------------------------------------------------------------
 * WHAT A SENSOR CANNOT DO HERE, SAID OUT LOUD
 * ---------------------------------------------------------------------------
 *   - Conditions never fire.  isConditionPushSupported() returns false and
 *     Channel.addCondition() raises UnsupportedOperationException rather than
 *     accepting a condition that would never be evaluated.  A MIDlet that
 *     checks the flag (as the spec tells it to) is unaffected.
 *   - Availability push is not generated either; isAvailabilityPushSupported()
 *     is false and addSensorListener() is accepted but never calls back.
 *   - getErrorCodes() is always empty: no port hook reports an error, and an
 *     empty array is what the spec says an error-free device returns.
 *   - getAccuracy() is 0 (unknown), not a measurement this runtime invented,
 *     and getUncertainty() is 0 for the same reason.
 *   - getConnectionType() is CONN_EMBEDDED for every sensor a port lists: the
 *     hooks describe local hardware.
 *
 * A port that lists no sensors is not a broken port: findSensors() returns an
 * empty array and getSensorConnection() raises ConnectionNotFoundException,
 * which is what a handset without an accelerometer does.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

#define NJ_C_SENSORMGR    "javax/microedition/sensor/SensorManager"
#define NJ_C_SENSORINFO   "javax/microedition/sensor/SensorInfo"
#define NJ_C_CHANNELINFO  "javax/microedition/sensor/ChannelInfo"
#define NJ_C_DATA         "javax/microedition/sensor/Data"
#define NJ_C_DATALISTEN   "javax/microedition/sensor/DataListener"
#define NJ_C_SENSLISTEN   "javax/microedition/sensor/SensorListener"
#define NJ_C_DATAERR      "javax/microedition/sensor/DataAndErrorListener"
#define NJ_C_CONDLISTEN   "javax/microedition/sensor/ConditionListener"
#define NJ_C_CONDITION    "javax/microedition/sensor/Condition"
#define NJ_C_SENSCONN     "javax/microedition/sensor/SensorConnection"
#define NJ_C_CONNECTION   "javax/microedition/io/Connection"
#define NJ_C_CHANNEL      "javax/microedition/sensor/Channel"
#define NJ_C_MEASRANGE    "javax/microedition/sensor/MeasurementRange"

/* The objects the runtime actually builds.  They live in the same package as
 * the interfaces they implement - a MIDlet cannot name them, but a cast from
 * the interface type succeeds because each one's super IS its interface. */
#define NJ_C_SENSORINFO_I "javax/microedition/sensor/SensorInfoImpl"
#define NJ_C_CHANNELINFO_I "javax/microedition/sensor/ChannelInfoImpl"
#define NJ_C_DATA_I       "javax/microedition/sensor/DataImpl"
#define NJ_C_SENSCONN_I   "javax/microedition/sensor/SensorConnectionImpl"
#define NJ_C_CHANNEL_I    "javax/microedition/sensor/ChannelImpl"

/* Descriptors, spelled once. */
#define D_STR   "Ljava/lang/String;"
#define D_OBJ   "Ljava/lang/Object;"
#define D_INFO  "L" NJ_C_SENSORINFO ";"
#define D_INFOA "[" D_INFO
#define D_CINFO "L" NJ_C_CHANNELINFO ";"
#define D_CINFOA "[" D_CINFO
#define D_DATA  "L" NJ_C_DATA ";"
#define D_DATAA "[" D_DATA
#define D_CONN  "L" NJ_C_SENSCONN ";"
#define D_DLIST "L" NJ_C_DATALISTEN ";"
#define D_SLIST "L" NJ_C_SENSLISTEN ";"
#define D_CLIST "L" NJ_C_CONDLISTEN ";"
#define D_COND  "L" NJ_C_CONDITION ";"
#define D_CONDA "[" D_COND
#define D_MR    "L" NJ_C_MEASRANGE ";"
#define D_MRA   "[" D_MR
#define D_CHAN  "L" NJ_C_CHANNEL ";"

/* How much of a sensor's own sample buffer this runtime admits to.  JSR 256
 * lets a MIDlet ask how many samples it may batch; nothing here batches (a
 * read is the newest sample), so the answer is the same number a small
 * handset used to report rather than a zero that would forbid listening. */
#define NJ_SENSOR_BUFFER 256

/* The period asked of the port when a connection opens, before any MIDlet has
 * said what it wants: 20 ms is SENSOR_DELAY_GAME on Android and the rate a
 * tilt-controlled MIDlet needs.  The port is free to round it to whatever its
 * hardware can do. */
#define NJ_SENSOR_OPEN_MS 20

/* ------------------------------------------------------------------ */
/* Field access                                                        */
/* ------------------------------------------------------------------ */

static nj_ref sget_ref(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    /* A NULL descriptor matches on name alone (field_lookup_walk checks it
     * only when it is given), which is what these helpers want: the field
     * names below are unique within their class. */
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void sset_ref(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                     nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static nj_s4 sget_int(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void sset_int(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                     nj_s4 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_s8 sget_long(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    return f ? nj_field_read_long(vm, o, f) : 0;
}

static void sset_long(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                      nj_s8 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    if (f) {
        nj_field_write_long(vm, o, f, v);
    }
}

/*
 * float and double fields hold their bit pattern, not a converted integer, so
 * these two go through the raw accessors - nj_field_write_int() on a float
 * field would store (float)(int)value.
 */
static double sget_double(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    nj_u8 bits;
    double v = 0.0;
    if (!f) {
        return 0.0;
    }
    bits = nj_field_read_bits64(vm, o, f);
    memcpy(&v, &bits, sizeof v);
    return v;
}

static void sset_double(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                        double v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    nj_u8 bits;
    if (!f) {
        return;
    }
    memcpy(&bits, &v, sizeof bits);
    nj_field_write_bits64(vm, o, f, bits);
}

static float sget_float(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    nj_u4 bits;
    float v = 0.0f;
    if (!f) {
        return 0.0f;
    }
    bits = nj_field_read_bits32(vm, o, f);
    memcpy(&v, &bits, sizeof v);
    return v;
}

static void sset_float(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                       float v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, NULL, 0);
    nj_u4 bits;
    if (!f) {
        return;
    }
    memcpy(&bits, &v, sizeof bits);
    nj_field_write_bits32(vm, o, f, bits);
}

/* Copy a String field into a C buffer.  Returns 0 when the field is absent or
 * holds no string, so callers can tell "empty" from "missing". */
static int sget_str(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                    char *out, int cap)
{
    nj_ref s;
    out[0] = '\0';
    if (cap <= 0) {
        return 0;
    }
    s = sget_ref(vm, o, cls, name);
    if (!s) {
        return 0;
    }
    return nj_string_to_utf8(s, out, cap);
}

/* A new String field value, or "" when the C string is NULL - a MIDlet that
 * reads getModel() gets an empty string rather than a NPE, and a port that
 * leaves a descriptor blank is not punished for it. */
static nj_ref s_new_str(nj_vm *vm, const char *utf8)
{
    return nj_string_new(vm, utf8 ? utf8 : "");
}

/* ------------------------------------------------------------------ */
/* The port's four calls                                               */
/* ------------------------------------------------------------------ */

/* java.util.Vector helpers, defined with the frame pump below and declared
 * here because the listener bookkeeping needs them first. */
static nj_s4 svec_size(nj_vm *vm, nj_ref v);
static nj_ref svec_at(nj_vm *vm, nj_ref v, nj_s4 i);

static int port_list(nj_vm *vm, nj_sensor_desc *out, int max)
{
    const nj_port *p = nj_vm_port(vm);
    int n;

    if (p == NULL || p->sensor_list == NULL || out == NULL || max <= 0) {
        return 0;
    }
    n = p->sensor_list(p->ud, out, max);
    if (n < 0) {
        n = 0;
    }
    if (n > max) {
        n = max;
    }
    return n;
}

static void *port_open(nj_vm *vm, const char *url, nj_u4 period_ms)
{
    const nj_port *p = nj_vm_port(vm);

    if (p == NULL || p->sensor_open == NULL || url == NULL || url[0] == '\0') {
        return NULL;
    }
    return p->sensor_open(p->ud, url, period_ms);
}

static void port_close(nj_vm *vm, void *handle)
{
    const nj_port *p = nj_vm_port(vm);

    if (handle != NULL && p != NULL && p->sensor_close != NULL) {
        p->sensor_close(p->ud, handle);
    }
}

/* Milliseconds on the port's own clock, or the VM's frame counter when the
 * port has no clock at all (a harness).  Used for pacing and for the
 * timestamp on a Data. */
static nj_s8 port_millis(nj_vm *vm)
{
    /* The VM's own monotonic millisecond clock (the port's millis() folded
     * into an epoch that survives a wrap) - the same one Thread.sleep() and
     * Object.wait() measure against. */
    return nj_now_ms(vm);
}

/* Wall-clock milliseconds, for Data.getTimestamp().  The port's millis() is a
 * monotonic uptime clock (SDL_GetTicks, CLOCK_MONOTONIC), which is not what a
 * MIDlet comparing timestamps against System.currentTimeMillis() expects, so
 * the epoch one comes from the class library. */
static nj_s8 epoch_millis(nj_vm *vm)
{
    nj_value r;

    r.j = 0;
    if (nj_call_static(vm, "java/lang/System", "currentTimeMillis", "()J",
                       NULL, &r) != NJ_OK) {
        nj_clear_exception(vm);
        return port_millis(vm);
    }
    return r.j;
}

/* ------------------------------------------------------------------ */
/* Objects the runtime builds                                          */
/* ------------------------------------------------------------------ */

static nj_ref channel_info_new(nj_vm *vm, const nj_sensor_desc *d, int index)
{
    nj_ref o = nj_object_new(vm, NJ_C_CHANNELINFO_I);
    nj_ref ranges;

    if (!o) {
        return NULL;
    }
    sset_int(vm, o, NJ_C_CHANNELINFO_I, "cIndex", index);
    sset_ref(vm, o, NJ_C_CHANNELINFO_I, "cName",
             s_new_str(vm, d->channel_names[index]));
    sset_ref(vm, o, NJ_C_CHANNELINFO_I, "cUnit", s_new_str(vm, d->units[index]));
    sset_int(vm, o, NJ_C_CHANNELINFO_I, "cDataType", 1);  /* TYPE_DOUBLE */
    sset_float(vm, o, NJ_C_CHANNELINFO_I, "cScale", 1.0f);
    sset_float(vm, o, NJ_C_CHANNELINFO_I, "cAccuracy", 0.0f);
    sset_double(vm, o, NJ_C_CHANNELINFO_I, "cSmallest", d->min_values[index]);
    sset_double(vm, o, NJ_C_CHANNELINFO_I, "cLargest", d->max_values[index]);
    sset_double(vm, o, NJ_C_CHANNELINFO_I, "cResolution",
                d->resolutions[index]);

    ranges = nj_array_new(vm, NJ_C_MEASRANGE, 1u);
    if (ranges) {
        nj_ref r = nj_object_new(vm, NJ_C_MEASRANGE);
        nj_value ctor[6];       /* smallest, largest, resolution: 2 slots each */
        if (r) {
            ctor[0].d = d->min_values[index];
            ctor[1].d = d->max_values[index];
            ctor[2].d = d->resolutions[index];
            if (nj_call_special(vm, r, NJ_C_MEASRANGE, "<init>", "(DDD)V", ctor,
                                NULL) != NJ_OK) {
                nj_clear_exception(vm);
            }
            nj_array_set_ref(ranges, 0u, r);
        }
    }
    sset_ref(vm, o, NJ_C_CHANNELINFO_I, "cRanges", ranges);
    return o;
}

/*
 * One SensorInfo, with its ChannelInfos built alongside it.  The port's
 * descriptor is copied field by field: it is borrowed for the length of the
 * call that produced it, and a MIDlet may hold a SensorInfo for the life of
 * the application.
 */
static nj_ref sensor_info_new(nj_vm *vm, const nj_sensor_desc *d, int index)
{
    nj_ref o = nj_object_new(vm, NJ_C_SENSORINFO_I);
    nj_ref infos;
    int channels = d->channels;
    int c;

    if (channels < 0) {
        channels = 0;
    }
    if (channels > NJ_SENSOR_MAX_CHANNELS) {
        channels = NJ_SENSOR_MAX_CHANNELS;   /* a port cannot describe more */
    }
    if (!o) {
        return NULL;
    }
    sset_int(vm, o, NJ_C_SENSORINFO_I, "sIndex", index);
    sset_ref(vm, o, NJ_C_SENSORINFO_I, "sUrl", s_new_str(vm, d->url));
    sset_ref(vm, o, NJ_C_SENSORINFO_I, "sQuantity", s_new_str(vm, d->quantity));
    sset_ref(vm, o, NJ_C_SENSORINFO_I, "sContext", s_new_str(vm, d->context));
    sset_ref(vm, o, NJ_C_SENSORINFO_I, "sModel", s_new_str(vm, d->model));
    sset_int(vm, o, NJ_C_SENSORINFO_I, "sChannels", channels);
    sset_int(vm, o, NJ_C_SENSORINFO_I, "sMaxRate", (nj_s4)d->max_rate_hz);
    sset_int(vm, o, NJ_C_SENSORINFO_I, "sBuffer", NJ_SENSOR_BUFFER);

    infos = nj_array_new(vm, NJ_C_CHANNELINFO, (nj_u4)channels);
    if (infos) {
        for (c = 0; c < channels; c++) {
            nj_array_set_ref(infos, (nj_u4)c, channel_info_new(vm, d, c));
        }
    }
    sset_ref(vm, o, NJ_C_SENSORINFO_I, "sInfos", infos);
    return o;
}

/*
 * One Data is one SAMPLE, holding one value per channel of the sensor: the
 * per-channel index on getTimestamp() and isValid() only means something that
 * way, and it is how a MIDlet reads an accelerometer - data[0].getDoubleValues()
 * is x, y and z.  getChannelInfo() answers with the first channel, which is the
 * whole answer for the single-channel sensors (a thermometer, a barometer).
 */
static nj_ref data_new(nj_vm *vm, nj_ref infos, const double *values, nj_u4 n)
{
    nj_ref o = nj_object_new(vm, NJ_C_DATA_I);
    nj_ref vals;
    nj_u4 i;

    if (!o) {
        return NULL;
    }
    sset_ref(vm, o, NJ_C_DATA_I, "dChannel",
             (infos && nj_array_length(infos) > 0u)
                 ? nj_array_get_ref(infos, 0u) : NULL);
    sset_double(vm, o, NJ_C_DATA_I, "dValue", n > 0u ? values[0] : 0.0);
    sset_long(vm, o, NJ_C_DATA_I, "dTimestamp", epoch_millis(vm));
    vals = nj_array_new(vm, "D", n);
    if (vals) {
        double *p = (double *)nj_array_data(vals);
        if (p) {
            for (i = 0u; i < n; i++) {
                p[i] = values[i];
            }
        }
    }
    sset_ref(vm, o, NJ_C_DATA_I, "dValues", vals);
    return o;
}

/* Read the newest sample of an open connection into `out`.  Returns the number
 * of channel values written (0 when the device has not measured anything
 * yet). */
/*
 * Sleep for a moment without spinning.  The port owns the clock, so this is
 * the port's sleep_ms() when it has one and idle() when it does not.
 */
static void sensor_sleep(nj_vm *vm, nj_u4 ms)
{
    const nj_port *p = nj_vm_port(vm);

    if (p == NULL) {
        return;
    }
    if (p->sleep_ms) {
        p->sleep_ms(p->ud, ms);
    } else if (p->idle) {
        p->idle(p->ud);
    }
}

static int conn_read(nj_vm *vm, nj_ref conn, double *out, int max_values)
{
    const nj_port *p = nj_vm_port(vm);
    void *handle = (void *)(intptr_t)sget_long(vm, conn, NJ_C_SENSCONN_I,
                                               "kHandle");
    int n;

    if (handle == NULL || p == NULL || p->sensor_read == NULL) {
        return 0;
    }
    n = p->sensor_read(p->ud, handle, out, max_values);
    if (n < 0) {
        n = 0;
    }
    if (n > max_values) {
        n = max_values;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* SensorManager                                                       */
/* ------------------------------------------------------------------ */

/*
 * findSensors(quantity) and findSensors(quantity, context).
 *
 * A NULL or empty argument means "any".  The port is asked once per call: a
 * MIDlet that searches while holding a SensorInfo from an earlier search still
 * gets a working object, because the SensorInfo carries its own copy of the
 * descriptor.
 */
static nj_status find_common(nj_vm *vm, nj_value *args, nj_value *ret,
                             int with_context)
{
    char quantity[40];
    char context[24];
    nj_sensor_desc descs[NJ_SENSOR_MAX_DESCRIPTORS];
    nj_ref out[NJ_SENSOR_MAX_DESCRIPTORS];
    int n, i, count = 0;

    quantity[0] = '\0';
    context[0] = '\0';
    if (args[0].r) {
        nj_string_to_utf8(args[0].r, quantity, (int)sizeof quantity);
    }
    if (with_context && args[1].r) {
        nj_string_to_utf8(args[1].r, context, (int)sizeof context);
    }

    n = port_list(vm, descs, NJ_SENSOR_MAX_DESCRIPTORS);
    for (i = 0; i < n; i++) {
        if (quantity[0] && strcmp(descs[i].quantity ? descs[i].quantity : "",
                                  quantity) != 0) {
            continue;
        }
        if (with_context && context[0] &&
            strcmp(descs[i].context ? descs[i].context : "", context) != 0) {
            continue;
        }
        out[count] = sensor_info_new(vm, &descs[i], i);
        if (out[count]) {
            count++;
        }
    }

    ret->r = nj_array_new(vm, NJ_C_SENSORINFO, (nj_u4)count);
    if (!ret->r) {
        return NJ_ERR;
    }
    for (i = 0; i < count; i++) {
        nj_array_set_ref(ret->r, (nj_u4)i, out[i]);
    }
    return NJ_OK;
}

NJFN(sensor_find_quantity)
{
    return find_common(vm, args, ret, 0);
}

NJFN(sensor_find_quantity_context)
{
    return find_common(vm, args, ret, 1);
}

/*
 * Open a connection.  The SensorInfo the MIDlet passes was built by an earlier
 * findSensors(), so this re-asks the port for the descriptor list and matches
 * on the URL: a device that has since lost the sensor refuses here, which is
 * the honest answer.
 */
NJFN(sensor_get_connection)
{
    nj_ref info = args[0].r;
    char url[NJ_SENSOR_MAX_URL];
    int index;
    int made;
    void *handle;
    nj_ref infos;
    nj_ref conn;

    if (!info) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the sensor info is null");
    }
    index = sget_int(vm, info, NJ_C_SENSORINFO_I, "sIndex");
    made = sget_str(vm, info, NJ_C_SENSORINFO_I, "sUrl", url, (int)sizeof url);
    if (made <= 0 || made >= (int)sizeof url) {
        /* A url that did not fit the buffer would be opened truncated, which
         * finds a different sensor - or none.  Refusing is the honest answer;
         * NJ_SENSOR_MAX_URL is what a port should stay under. */
        index = -1;
    }
    if (index < 0) {
        return nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                        "the sensor info has no url");
    }

    handle = port_open(vm, url, NJ_SENSOR_OPEN_MS);
    if (handle == NULL) {
        return nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                        "the sensor is not available");
    }

    infos = sget_ref(vm, info, NJ_C_SENSORINFO_I, "sInfos");
    /*
     * No <init> call: a builtin class is allocated with nj_object_new() and
     * its C side fills every field, which is what every other module here
     * does (see nj_http_new()).  The 1 that used to be passed to a
     * (SensorInfo,int) constructor is now simply not part of the object's
     * shape - see the state field below.
     */
    conn = nj_object_new(vm, NJ_C_SENSCONN_I);
    if (!conn) {
        port_close(vm, handle);
        return NJ_ERR;
    }
    sset_ref(vm, conn, NJ_C_SENSCONN_I, "kInfo", info);
    sset_ref(vm, conn, NJ_C_SENSCONN_I, "kInfos", infos);
    sset_ref(vm, conn, NJ_C_SENSCONN_I, "kUrl", s_new_str(vm, url));
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kHandle", (nj_s8)(intptr_t)handle);
    sset_int(vm, conn, NJ_C_SENSCONN_I, "kState", 1);       /* STATE_OPENED */
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kPeriod", 100);   /* 10 Hz default */
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kLast", port_millis(vm));
    ret->r = conn;
    return NJ_OK;
}

/*
 * Availability listeners are accepted and never called: isAvailabilityPush
 * Support() returns false, and the spec tells a MIDlet to check that before
 * registering.  Raising here instead would kill a MIDlet that registered
 * defensively, which is worse than a callback that never happens.
 */
NJFN(sensor_add_listener_info)
{
    if (!args[0].r || !args[1].r) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the listener and the sensor info must not be null");
    }
    return NJ_OK;
}

NJFN(sensor_add_listener_quantity)
{
    if (!args[0].r) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the listener must not be null");
    }
    return NJ_OK;
}

NJFN(sensor_remove_listener)
{
    if (!args[0].r) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the listener must not be null");
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* SensorInfo                                                          */
/* ------------------------------------------------------------------ */

NJFN(info_get_url)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sUrl");
    return NJ_OK;
}

NJFN(info_get_quantity)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sQuantity");
    return NJ_OK;
}

NJFN(info_get_context)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sContext");
    return NJ_OK;
}

NJFN(info_get_model)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sModel");
    return NJ_OK;
}

NJFN(info_get_connection_type)
{
    /* Every hook describes hardware on this device, so CONN_EMBEDDED. */
    ret->i = 1;
    return NJ_OK;
}

NJFN(info_get_channel_infos)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sInfos");
    return NJ_OK;
}

NJFN(info_get_max_buffer)
{
    ret->i = sget_int(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sBuffer");
    return NJ_OK;
}

NJFN(info_is_available)
{
    /* It was in the list when this object was built; a sensor that has gone
     * away since is reported by the open() that fails, not by this. */
    ret->i = 1;
    return NJ_OK;
}

NJFN(info_availability_push)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(info_condition_push)
{
    ret->i = 0;
    return NJ_OK;
}

/*
 * The properties a port can actually answer.  JSR 256 says a device returns
 * null for a property it does not have; inventing a vendor or a version string
 * would be a lie a MIDlet might print to a user.
 */
NJFN(info_get_property)
{
    char name[40];
    nj_s4 max_rate = sget_int(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sMaxRate");

    if (!args[0].r) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the property name is null");
    }
    nj_string_to_utf8(args[0].r, name, (int)sizeof name);
    if (strcmp(name, "maxSamplingRate") == 0 && max_rate > 0) {
        ret->r = nj_string_new_from_int(vm, max_rate);
        return NJ_OK;
    }
    ret->r = NULL;
    return NJ_OK;
}

NJFN(info_get_property_names)
{
    nj_s4 max_rate = sget_int(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sMaxRate");
    int count = (max_rate > 0) ? 1 : 0;
    nj_ref a = nj_array_new(vm, "java/lang/String", (nj_u4)count);

    if (a && count > 0) {
        nj_array_set_ref(a, 0u, nj_string_new(vm, "maxSamplingRate"));
    }
    ret->r = a;
    return NJ_OK;
}

NJFN(info_get_description)
{
    /* A one-line description built from what the port knows.  JSR 256 leaves
     * the format to the device; a MIDlet shows this to a human. */
    char quantity[40];
    char model[48];

    if (sget_str(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sQuantity", quantity,
                 (int)sizeof quantity) <= 0) {
        quantity[0] = '\0';
    }
    if (sget_str(vm, NJ_THIS(args), NJ_C_SENSORINFO_I, "sModel", model,
                 (int)sizeof model) <= 0) {
        model[0] = '\0';
    }
    if (model[0]) {
        ret->r = nj_string_new(vm, model);
    } else if (quantity[0]) {
        ret->r = nj_string_new(vm, quantity);
    } else {
        ret->r = nj_string_new(vm, "sensor");
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* ChannelInfo and MeasurementRange                                    */
/* ------------------------------------------------------------------ */

NJFN(cinfo_get_name)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cName");
    return NJ_OK;
}

NJFN(cinfo_get_unit)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cUnit");
    return NJ_OK;
}

NJFN(cinfo_get_data_type)
{
    ret->i = sget_int(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cDataType");
    return NJ_OK;
}

NJFN(cinfo_get_scale)
{
    ret->f = sget_float(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cScale");
    return NJ_OK;
}

NJFN(cinfo_get_accuracy)
{
    ret->f = sget_float(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cAccuracy");
    return NJ_OK;
}

NJFN(cinfo_get_ranges)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_CHANNELINFO_I, "cRanges");
    return NJ_OK;
}

/* The spec's constructor takes floats; both shapes are registered because a
 * builtin class has one signature per name, and a MIDlet written against the
 * javadoc emits (FFF)V while one written against a decompiled runtime may not. */
NJFN(mrange_init)   /* (DDD)V */
{
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rSmallest", args[0].d);
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rLargest", args[1].d);
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rResolution", args[2].d);
    return NJ_OK;
}

NJFN(mrange_init_f) /* (FFF)V */
{
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rSmallest",
                (double)args[0].f);
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rLargest",
                (double)args[1].f);
    sset_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rResolution",
                (double)args[2].f);
    return NJ_OK;
}

NJFN(mrange_smallest)
{
    /* The spec calls these float; the range is stored as a double
     * because that is the shape the port describes hardware in. */
    ret->f = (float)sget_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rSmallest");
    return NJ_OK;
}

NJFN(mrange_largest)
{
    /* The spec calls these float; the range is stored as a double
     * because that is the shape the port describes hardware in. */
    ret->f = (float)sget_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rLargest");
    return NJ_OK;
}

NJFN(mrange_resolution)
{
    /* The spec calls these float; the range is stored as a double
     * because that is the shape the port describes hardware in. */
    ret->f = (float)sget_double(vm, NJ_THIS(args), NJ_C_MEASRANGE, "rResolution");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Data                                                                */
/* ------------------------------------------------------------------ */

NJFN(data_get_channel_info)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_DATA_I, "dChannel");
    return NJ_OK;
}

NJFN(data_get_double_values)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_DATA_I, "dValues");
    return NJ_OK;
}

NJFN(data_get_int_values)
{
    double v = sget_double(vm, NJ_THIS(args), NJ_C_DATA_I, "dValue");
    nj_ref a = nj_array_new(vm, "I", 1u);

    if (a) {
        nj_array_set_int(a, 0u, (nj_s4)v);
    }
    ret->r = a;
    return NJ_OK;
}

NJFN(data_get_object_values)
{
    double v = sget_double(vm, NJ_THIS(args), NJ_C_DATA_I, "dValue");
    nj_ref a = nj_array_new(vm, "java/lang/Object", 1u);

    if (a) {
        nj_ref box = nj_object_new(vm, "java/lang/Double");
        if (box) {
            nj_value av[2];     /* a double takes two slots */
            av[0].d = v;
            if (nj_call_special(vm, box, "java/lang/Double", "<init>", "(D)V",
                                av, NULL) != NJ_OK) {
                nj_clear_exception(vm);
            }
            nj_array_set_ref(a, 0u, box);
        }
    }
    ret->r = a;
    return NJ_OK;
}

/*
 * getTimestamp, getUncertainty and isValid all take an index into this Data's
 * own values.  A Data here always holds exactly one value, so index 0 is the
 * only valid one and anything else is the IndexOutOfBoundsException the spec
 * asks for rather than a plausible-looking zero.
 */
static int data_index_ok(nj_vm *vm, nj_value *args)
{
    if (args[0].i < 0 || args[0].i > 0) {
        (void)nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                       "this data holds one value");
        return 0;
    }
    return 1;
}

NJFN(data_get_timestamp)
{
    if (!data_index_ok(vm, args)) {
        return NJ_ERR;
    }
    ret->j = sget_long(vm, NJ_THIS(args), NJ_C_DATA_I, "dTimestamp");
    return NJ_OK;
}

NJFN(data_get_uncertainty)
{
    if (!data_index_ok(vm, args)) {
        return NJ_ERR;
    }
    /* Level 0 in the spec's terms: no uncertainty information.  Zero is the
     * documented "not known" answer, not a measurement of quality. */
    ret->f = 0.0f;
    return NJ_OK;
}

NJFN(data_is_valid)
{
    if (!data_index_ok(vm, args)) {
        return NJ_ERR;
    }
    ret->i = 1;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* SensorConnection                                                    */
/* ------------------------------------------------------------------ */

NJFN(conn_get_sensor_info)
{
    ret->r = sget_ref(vm, NJ_THIS(args), NJ_C_SENSCONN_I, "kInfo");
    return NJ_OK;
}

NJFN(conn_get_state)
{
    ret->i = sget_int(vm, NJ_THIS(args), NJ_C_SENSCONN_I, "kState");
    return NJ_OK;
}

/*
 * Both getData() overloads return the newest sample.  bufferSize, the timeout,
 * and the three booleans of the long form are accepted and ignored after being
 * range-checked, because a read is a snapshot here: there is never more than
 * one sample to hand back, and nothing waits.
 */
static nj_status conn_get_data(nj_vm *vm, nj_value *args, nj_value *ret,
                               int long_form)
{
    nj_ref conn = NJ_THIS(args);
    nj_ref infos;
    nj_ref list;
    double values[NJ_SENSOR_MAX_CHANNELS];
    nj_u4 n;
    int got;
    nj_s4 buffer_size = args[0].i;

    if (buffer_size <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the buffer size must be positive");
    }
    if (long_form && args[1].j < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the timeout must not be negative");
    }
    if (sget_int(vm, conn, NJ_C_SENSCONN_I, "kState") == 4) {
        return nj_throw(vm, "java/io/IOException", "the connection is closed");
    }

    got = conn_read(vm, conn, values, NJ_SENSOR_MAX_CHANNELS);
    /*
     * The long form is the one that may wait: a MIDlet calls it when it wants
     * a reading rather than a snapshot, and a port that delivers samples from
     * an interrupt - rather than holding the sensor's current value - has
     * nothing to give on the very first call.  The wait is bounded by the
     * timeout the caller passed, and it is the only place this module blocks;
     * the short form never waits, because a game loop calls it every frame.
     */
    if (long_form && got <= 0 && args[1].j > 0) {
        nj_s8 left = args[1].j;

        while (left > 0 && got <= 0) {
            nj_u4 step = left > 10 ? 10u : (nj_u4)left;

            sensor_sleep(vm, step);
            left -= (nj_s8)step;
            got = conn_read(vm, conn, values, NJ_SENSOR_MAX_CHANNELS);
        }
    }
    infos = sget_ref(vm, conn, NJ_C_SENSCONN_I, "kInfos");
    n = infos ? nj_array_length(infos) : 0u;
    if (got <= 0) {
        n = 0u;                     /* nothing measured yet: an empty array,
                                     * never a fabricated zero */
    } else if ((nj_u4)got < n) {
        n = (nj_u4)got;
    }

    /* A read is a snapshot, so the result holds at most one sample: bufferSize
     * is a limit, and there is no queue behind it. */
    list = nj_array_new(vm, NJ_C_DATA, n > 0u ? 1u : 0u);
    if (!list) {
        return NJ_ERR;
    }
    if (n > 0u) {
        nj_array_set_ref(list, 0u, data_new(vm, infos, values, n));
    }
    ret->r = list;
    return NJ_OK;
}

NJFN(conn_get_data_short)
{
    return conn_get_data(vm, args, ret, 0);
}

NJFN(conn_get_data_long)
{
    return conn_get_data(vm, args, ret, 1);
}

/* Register a listener on the VM's connection list, so sensor_frame() can pace
 * it.  Registering twice replaces the first registration rather than
 * delivering twice. */
static int conn_listen(nj_vm *vm, nj_ref conn, nj_ref listener, nj_s8 period_ms)
{
    nj_ref v = vm->sensor_conns;
    nj_value a[4];
    nj_s4 n, i;

    if (period_ms <= 0) {
        period_ms = 100;            /* JSR 256: a period of 0 means "default" */
    }
    sset_ref(vm, conn, NJ_C_SENSCONN_I, "kListener", listener);
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kPeriod", period_ms);
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kLast", port_millis(vm));
    sset_int(vm, conn, NJ_C_SENSCONN_I, "kState", 2);       /* STATE_LISTENING */

    if (!v) {
        v = nj_object_new(vm, "java/util/Vector");
        if (!v) {
            nj_clear_exception(vm);
            return 0;
        }
        vm->sensor_conns = v;
    }
    n = svec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        if (svec_at(vm, v, i) == conn) {
            return 1;               /* already on the list */
        }
    }
    a[0].r = conn;
    if (nj_call(vm, v, "java/util/Vector", "addElement",
                "(Ljava/lang/Object;)V", a, NULL) != NJ_OK) {
        nj_clear_exception(vm);
        return 0;
    }
    return 1;
}

static void conn_unlisten(nj_vm *vm, nj_ref conn)
{
    nj_ref v = vm->sensor_conns;
    nj_s4 n, i;

    sset_ref(vm, conn, NJ_C_SENSCONN_I, "kListener", NULL);
    if (sget_int(vm, conn, NJ_C_SENSCONN_I, "kState") == 2) {
        sset_int(vm, conn, NJ_C_SENSCONN_I, "kState", 1);
    }
    if (!v) {
        return;
    }
    n = svec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        if (svec_at(vm, v, i) == conn) {
            nj_value a[4];
            a[0].r = conn;
            (void)nj_call(vm, v, "java/util/Vector", "removeElement",
                          "(Ljava/lang/Object;)Z", a, NULL);
            break;
        }
    }
}

static nj_status conn_set_listener(nj_vm *vm, nj_value *args, nj_s4 period_ms)
{
    nj_ref conn = NJ_THIS(args);

    if (!args[0].r) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the listener is null");
    }
    if (period_ms < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the period must not be negative");
    }
    if (sget_int(vm, conn, NJ_C_SENSCONN_I, "kState") == 4) {
        return nj_throw(vm, "java/io/IOException", "the connection is closed");
    }
    if (!conn_listen(vm, conn, args[0].r, (nj_s8)period_ms)) {
        return nj_throw(vm, "java/lang/OutOfMemoryError",
                        "the listener list could not grow");
    }
    return NJ_OK;
}

NJFN(conn_set_listener_short)
{
    return conn_set_listener(vm, args, args[1].i);
}

NJFN(conn_set_listener_long)
{
    return conn_set_listener(vm, args, (nj_s4)args[1].j);
}

NJFN(conn_remove_listener)
{
    conn_unlisten(vm, NJ_THIS(args));
    return NJ_OK;
}

/*
 * getChannel(ChannelInfo) hands back a Channel object only for a ChannelInfo
 * that belongs to this connection.  The spec says the ChannelInfo must come
 * from the connection's own SensorInfo, so an alien one is refused instead of
 * quietly accepted.
 */
NJFN(conn_get_channel)
{
    nj_ref conn = NJ_THIS(args);
    nj_ref want = args[0].r;
    nj_ref infos = sget_ref(vm, conn, NJ_C_SENSCONN_I, "kInfos");
    nj_u4 i, n;

    if (!want) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "the channel info is null");
    }
    n = infos ? nj_array_length(infos) : 0u;
    for (i = 0u; i < n; i++) {
        if (nj_array_get_ref(infos, i) == want) {
            nj_ref ch = nj_object_new(vm, NJ_C_CHANNEL_I);
            if (!ch) {
                return NJ_ERR;
            }
            sset_ref(vm, ch, NJ_C_CHANNEL_I, "nConn", conn);
            sset_int(vm, ch, NJ_C_CHANNEL_I, "nIndex", (nj_s4)i);
            sset_float(vm, ch, NJ_C_CHANNEL_I, "nRate",
                       1000.0f / (float)sget_long(vm, conn, NJ_C_SENSCONN_I,
                                                  "kPeriod"));
            ret->r = ch;
            return NJ_OK;
        }
    }
    return nj_throw(vm, "java/lang/IllegalArgumentException",
                    "the channel info does not belong to this connection");
}

NJFN(conn_get_error_codes)
{
    /* No port hook reports a sensor error, and the spec's "no errors" answer
     * is an empty array. */
    ret->r = nj_array_new(vm, "I", 0u);
    return NJ_OK;
}

NJFN(conn_get_error_text)
{
    ret->r = nj_string_new(vm, "no sensor errors are reported by this runtime");
    return NJ_OK;
}

NJFN(conn_close)
{
    nj_ref conn = NJ_THIS(args);
    nj_s8 st = sget_int(vm, conn, NJ_C_SENSCONN_I, "kState");

    if (st == 4) {
        return NJ_OK;               /* close() is idempotent */
    }
    conn_unlisten(vm, conn);
    port_close(vm, (void *)(intptr_t)sget_long(vm, conn, NJ_C_SENSCONN_I,
                                               "kHandle"));
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kHandle", 0);
    sset_int(vm, conn, NJ_C_SENSCONN_I, "kState", 4);       /* STATE_CLOSED */
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Channel                                                             */
/* ------------------------------------------------------------------ */

static nj_ref channel_conn(nj_vm *vm, nj_value *args)
{
    return sget_ref(vm, NJ_THIS(args), NJ_C_CHANNEL_I, "nConn");
}

NJFN(channel_get_channel_info)
{
    nj_ref conn = channel_conn(vm, args);
    nj_ref infos = conn ? sget_ref(vm, conn, NJ_C_SENSCONN_I, "kInfos") : NULL;
    nj_s4 idx = sget_int(vm, NJ_THIS(args), NJ_C_CHANNEL_I, "nIndex");

    if (!infos || idx < 0 || (nj_u4)idx >= nj_array_length(infos)) {
        ret->r = NULL;
        return NJ_OK;
    }
    ret->r = nj_array_get_ref(infos, (nj_u4)idx);
    return NJ_OK;
}

NJFN(channel_get_url)
{
    nj_ref conn = channel_conn(vm, args);

    ret->r = conn ? sget_ref(vm, conn, NJ_C_SENSCONN_I, "kUrl") : NULL;
    return NJ_OK;
}

/*
 * Conditions are not evaluated by this runtime, so a MIDlet that tries to add
 * one is told rather than handed a condition that never fires.
 * isConditionPushSupported() is false and the spec tells a MIDlet to check it.
 */
NJFN(channel_add_condition)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "this runtime does not evaluate sensor conditions");
}

NJFN(channel_remove_condition)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "this runtime does not evaluate sensor conditions");
}

NJFN(channel_get_conditions)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "this runtime does not evaluate sensor conditions");
}

NJFN(channel_remove_all_conditions)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "this runtime does not evaluate sensor conditions");
}

NJFN(channel_remove_condition_listener)
{
    return nj_throw(vm, "java/lang/UnsupportedOperationException",
                    "this runtime does not evaluate sensor conditions");
}

/*
 * setSampleRate changes the period the runtime delivers at, and re-asks the
 * port for the hardware rate: a MIDlet that wants 1 Hz should not make the
 * phone sample at 50 Hz for an hour.  The new handle is opened before the old
 * one is closed, so a device that refuses the change keeps working at the rate
 * it already had.
 */
static nj_status channel_set_rate(nj_vm *vm, nj_ref ch, float hz)
{
    nj_ref conn = sget_ref(vm, ch, NJ_C_CHANNEL_I, "nConn");
    char url[NJ_SENSOR_MAX_URL];
    nj_s8 period;
    void *old_handle;
    void *new_handle;

    if (hz <= 0.0f) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the sample rate must be positive");
    }
    if (!conn) {
        return nj_throw(vm, "java/io/IOException", "the connection is gone");
    }
    period = (nj_s8)(1000.0f / hz);
    if (period < 1) {
        period = 1;
    }

    old_handle = (void *)(intptr_t)sget_long(vm, conn, NJ_C_SENSCONN_I,
                                             "kHandle");
    if (old_handle != NULL) {
        int made = sget_str(vm, conn, NJ_C_SENSCONN_I, "kUrl", url,
                            (int)sizeof url);

        if (made <= 0 || made >= (int)sizeof url) {
            return nj_throw(vm, "java/io/IOException",
                            "the connection has no usable url");
        }
        new_handle = port_open(vm, url, (nj_u4)period);
        if (new_handle != NULL) {
            port_close(vm, old_handle);
            sset_long(vm, conn, NJ_C_SENSCONN_I, "kHandle",
                      (nj_s8)(intptr_t)new_handle);
        } else {
            /* The device would not reopen; keep measuring at the old rate and
             * say so through the period the runtime paces at, which still
             * changes: the listener is what a MIDlet actually observes. */
            period = sget_long(vm, conn, NJ_C_SENSCONN_I, "kPeriod");
        }
    }
    sset_long(vm, conn, NJ_C_SENSCONN_I, "kPeriod", period);
    sset_float(vm, ch, NJ_C_CHANNEL_I, "nRate", hz);
    return NJ_OK;
}

NJFN(channel_set_sample_rate)
{
    return channel_set_rate(vm, NJ_THIS(args), args[0].f);
}

NJFN(channel_get_sample_rate)
{
    nj_ref conn = channel_conn(vm, args);
    float rate = sget_float(vm, NJ_THIS(args), NJ_C_CHANNEL_I, "nRate");

    if (conn == NULL) {
        return nj_throw(vm, "java/io/IOException", "the connection is gone");
    }
    ret->f = rate > 0.0f ? rate : 10.0f;
    return NJ_OK;
}

NJFN(channel_get_sample_rates)
{
    /* One entry: the rate the connection is running at.  A port hook cannot
     * enumerate a device's discrete rates yet, so promising a list of them
     * would be a guess. */
    nj_ref a = nj_array_new(vm, "F", 1u);
    float rate = sget_float(vm, NJ_THIS(args), NJ_C_CHANNEL_I, "nRate");
    float *p = a ? (float *)nj_array_data(a) : NULL;

    if (p) {
        p[0] = rate > 0.0f ? rate : 10.0f;
    }
    ret->r = a;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The frame pump                                                      */
/* ------------------------------------------------------------------ */

/* java.util.Vector, through the usual virtual dispatch. */
static nj_s4 svec_size(nj_vm *vm, nj_ref v)
{
    nj_value r;

    if (!v) {
        return 0;
    }
    r.i = 0;
    if (nj_call(vm, v, "java/util/Vector", "size", "()I", NULL, &r) != NJ_OK) {
        nj_clear_exception(vm);
        return 0;
    }
    return r.i;
}

static nj_ref svec_at(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a[4];
    nj_value r;

    a[0].i = i;
    r.r = NULL;
    if (nj_call(vm, v, "java/util/Vector", "elementAt",
                "(I)Ljava/lang/Object;", a, &r) != NJ_OK) {
        nj_clear_exception(vm);
        return NULL;
    }
    return r.r;
}

/*
 * Once per emulated frame: wake every connection that is listening and whose
 * period has elapsed, and hand its listener one Data per channel.  The
 * listener runs on the MIDlet's own thread, which is why a MIDlet callback
 * here may touch its own state without a lock.
 */
void nj_sensor_frame(nj_vm *vm)
{
    nj_ref v;
    nj_s4 n, i;
    nj_s8 now;

    if (!vm) {
        return;
    }
    v = vm->sensor_conns;
    if (!v) {
        return;
    }
    now = port_millis(vm);
    n = svec_size(vm, v);
    for (i = n - 1; i >= 0; i--) {
        nj_ref conn = svec_at(vm, v, i);
        nj_ref infos;
        nj_ref listener;
        nj_ref list;
        nj_s8 period;
        double values[NJ_SENSOR_MAX_CHANNELS];
        nj_u4 count;
        int got;

        if (!conn) {
            continue;
        }
        listener = sget_ref(vm, conn, NJ_C_SENSCONN_I, "kListener");
        if (!listener) {
            continue;
        }
        period = sget_long(vm, conn, NJ_C_SENSCONN_I, "kPeriod");
        if (period > 0 && now - sget_long(vm, conn, NJ_C_SENSCONN_I, "kLast") <
            period) {
            continue;
        }
        sset_long(vm, conn, NJ_C_SENSCONN_I, "kLast", now);

        got = conn_read(vm, conn, values, NJ_SENSOR_MAX_CHANNELS);
        if (got <= 0) {
            continue;               /* nothing measured yet: no event */
        }
        infos = sget_ref(vm, conn, NJ_C_SENSCONN_I, "kInfos");
        count = infos ? nj_array_length(infos) : 0u;
        if ((nj_u4)got < count) {
            count = (nj_u4)got;
        }
        list = nj_array_new(vm, NJ_C_DATA, 1u);
        if (!list) {
            continue;
        }
        nj_array_set_ref(list, 0u, data_new(vm, infos, values, count));
        {
            nj_value a[4];
            a[0].r = conn;
            a[1].r = list;
            a[2].i = 0;             /* isValid: false means "some data is
                                     * invalid", and nothing here is */
            if (nj_call(vm, listener, NJ_C_DATALISTEN, "dataReceived",
                        "(Ljavax/microedition/sensor/SensorConnection;"
                        "[Ljavax/microedition/sensor/Data;Z)V", a,
                        NULL) != NJ_OK) {
                nj_clear_exception(vm);   /* a listener that throws must not
                                           * stop the MIDlet's frame */
            }
        }
    }
}

/*
 * Close every connection still open.  Called when the VM shuts down: a port's
 * own shutdown() should also stop its hardware, but a connection that is
 * closed here has already been told.
 */
void nj_sensor_shutdown(nj_vm *vm)
{
    nj_ref v;
    nj_s4 n, i;

    if (!vm) {
        return;
    }
    v = vm->sensor_conns;
    if (!v) {
        return;
    }
    n = svec_size(vm, v);
    for (i = 0; i < n; i++) {
        nj_ref conn = svec_at(vm, v, i);
        if (conn) {
            port_close(vm, (void *)(intptr_t)sget_long(vm, conn,
                                                       NJ_C_SENSCONN_I,
                                                       "kHandle"));
            sset_long(vm, conn, NJ_C_SENSCONN_I, "kHandle", 0);
        }
    }
    vm->sensor_conns = NULL;
}

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/*
 * The static final values every MIDlet reads.  They are fields on the
 * interfaces, declared in the tables below and filled in here once the class
 * library is built - the same shape javax.microedition.io uses for
 * Connector.READ and HttpConnection.GET.
 */
nj_status nj_sensor_late_init(nj_vm *vm)
{
    static const struct { const char *cls; const char *n; nj_s4 v; } ki[] = {
        { NJ_C_SENSORINFO, "CONN_EMBEDDED", 1 },
        { NJ_C_SENSORINFO, "CONN_REMOTE", 2 },
        { NJ_C_SENSORINFO, "CONN_SHORT_RANGE_WIRELESS", 4 },
        { NJ_C_SENSORINFO, "CONN_WIRED", 8 },
        { NJ_C_CHANNELINFO, "TYPE_DOUBLE", 1 },
        { NJ_C_CHANNELINFO, "TYPE_INT", 2 },
        { NJ_C_CHANNELINFO, "TYPE_OBJECT", 4 },
        { NJ_C_SENSCONN, "STATE_OPENED", 1 },
        { NJ_C_SENSCONN, "STATE_LISTENING", 2 },
        { NJ_C_SENSCONN, "STATE_CLOSED", 4 }
    };
    static const struct { const char *cls; const char *n; const char *v; } ks[] = {
        { NJ_C_SENSORINFO, "CONTEXT_TYPE_USER", "user" },
        { NJ_C_SENSORINFO, "CONTEXT_TYPE_AMBIENT", "ambient" },
        { NJ_C_SENSORINFO, "CONTEXT_TYPE_DEVICE", "device" },
        { NJ_C_SENSORINFO, "CONTEXT_TYPE_VEHICLE", "vehicle" },
        { NJ_C_SENSORINFO, "PROP_IS_CONTROLLABLE", "controllable" },
        { NJ_C_SENSORINFO, "PROP_IS_REPORTING_ERRORS", "errorsReported" },
        { NJ_C_SENSORINFO, "PROP_LATITUDE", "latitude" },
        { NJ_C_SENSORINFO, "PROP_LOCATION", "location" },
        { NJ_C_SENSORINFO, "PROP_LONGITUDE", "longitude" },
        { NJ_C_SENSORINFO, "PROP_MAX_RATE", "maxSamplingRate" },
        { NJ_C_SENSORINFO, "PROP_PERMISSION", "permission" },
        { NJ_C_SENSORINFO, "PROP_VENDOR", "vendor" },
        { NJ_C_SENSORINFO, "PROP_VERSION", "version" },
        { NJ_C_CONDITION, "OP_EQUALS", "eq" },
        { NJ_C_CONDITION, "OP_GREATER_THAN", "gt" },
        { NJ_C_CONDITION, "OP_GREATER_THAN_OR_EQUALS", "ge" },
        { NJ_C_CONDITION, "OP_LESS_THAN", "lt" },
        { NJ_C_CONDITION, "OP_LESS_THAN_OR_EQUALS", "le" }
    };
    unsigned i;

    if (!vm) {
        return NJ_ERR;
    }
    for (i = 0u; i < sizeof(ki) / sizeof(ki[0]); i++) {
        nj_field *f = nj_field_lookup(vm, ki[i].cls, ki[i].n, "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, ki[i].v);
        }
    }
    for (i = 0u; i < sizeof(ks) / sizeof(ks[0]); i++) {
        nj_field *f = nj_field_lookup(vm, ks[i].cls, ks[i].n,
                                      "Ljava/lang/String;", 1);
        if (f) {
            nj_field_write_ref(vm, NULL, f, nj_string_new(vm, ks[i].v));
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Class and native tables                                             */
/* ------------------------------------------------------------------ */

/*
 * One interface and the object that implements it share one method list: the
 * interface declares the methods abstract (a MIDlet only reads the contract)
 * and the object declares the same names concrete, because this runtime
 * resolves a method from the class the call is made on.  Writing the list once
 * and expanding it twice keeps the two from drifting.
 */
#define X_SENSORINFO(X) \
    X("getUrl", "()Ljava/lang/String;") \
    X("getQuantity", "()Ljava/lang/String;") \
    X("getContextType", "()Ljava/lang/String;") \
    X("getConnectionType", "()I") \
    X("getModel", "()Ljava/lang/String;") \
    X("getChannelInfos", "()[Ljavax/microedition/sensor/ChannelInfo;") \
    X("getMaxBufferSize", "()I") \
    X("isAvailable", "()Z") \
    X("isAvailabilityPushSupported", "()Z") \
    X("isConditionPushSupported", "()Z") \
    X("getProperty", "(Ljava/lang/String;)Ljava/lang/String;") \
    X("getPropertyNames", "()[Ljava/lang/String;") \
    X("getDescription", "()Ljava/lang/String;")

#define X_CHANNELINFO(X) \
    X("getName", "()Ljava/lang/String;") \
    X("getDataType", "()I") \
    X("getScale", "()F") \
    X("getUnit", "()Ljava/lang/String;") \
    X("getAccuracy", "()F") \
    X("getMeasurementRanges", "()[Ljavax/microedition/sensor/MeasurementRange;")

#define X_DATA(X) \
    X("getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;") \
    X("getDoubleValues", "()[D") \
    X("getIntValues", "()[I") \
    X("getObjectValues", "()[Ljava/lang/Object;") \
    X("getTimestamp", "(I)J") \
    X("getUncertainty", "(I)F") \
    X("isValid", "(I)Z")

#define X_CHANNEL(X) \
    X("getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;") \
    X("getChannelUrl", "()Ljava/lang/String;") \
    X("addCondition", "(Ljavax/microedition/sensor/ConditionListener;Ljavax/microedition/sensor/Condition;)V") \
    X("removeCondition", "(Ljavax/microedition/sensor/ConditionListener;Ljavax/microedition/sensor/Condition;)V") \
    X("getConditions", "(Ljavax/microedition/sensor/ConditionListener;)[Ljavax/microedition/sensor/Condition;") \
    X("removeAllConditions", "()V") \
    X("removeConditionListener", "(Ljavax/microedition/sensor/ConditionListener;)V") \
    /* Not in the spec's Channel: this runtime puts the sample rate on the \
     * channel object instead of a separate SampleRateControl, and it is the \
     * only way a MIDlet can ask for a different rate. */ \
    X("setSampleRate", "(F)V") \
    X("getSampleRate", "()F") \
    X("getSampleRates", "()[F")

/* SensorConnection carries close() as well: it extends javax.microedition.io
 * .Connection, and a builtin class has one super, so the method has to be
 * declared here for a MIDlet's conn.close() to resolve. */
#define X_SENSCONN(X) \
    X("getSensorInfo", "()Ljavax/microedition/sensor/SensorInfo;") \
    X("getData", "(I)[Ljavax/microedition/sensor/Data;") \
    X("getData", "(IJZZZ)[Ljavax/microedition/sensor/Data;") \
    X("setDataListener", "(Ljavax/microedition/sensor/DataListener;I)V") \
    X("setDataListener", "(Ljavax/microedition/sensor/DataListener;IJZZZ)V") \
    X("removeDataListener", "()V") \
    X("getChannel", "(Ljavax/microedition/sensor/ChannelInfo;)Ljavax/microedition/sensor/Channel;") \
    X("getErrorCodes", "()[I") \
    X("getErrorText", "(I)Ljava/lang/String;") \
    X("getState", "()I") \
    X("close", "()V")

#define ABST(name, desc) { name, desc, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT },
#define CONC(name, desc) { name, desc, NJ_ACC_PUBLIC },
#define STAT(name, desc) { name, desc, NJ_ACC_STATIC }
#define FINT(name)       { name, "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
#define FSTR(name)       { name, "Ljava/lang/String;", \
                           NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
#define FOBJ(name)       { name, "I", NJ_ACC_FINAL }

/* --- the interfaces ------------------------------------------------ */

static const nj_builtin_method datalisten_methods[] = {
    { "dataReceived",
      "(Ljavax/microedition/sensor/SensorConnection;"
      "[Ljavax/microedition/sensor/Data;Z)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

static const nj_builtin_method sensorlisten_methods[] = {
    { "sensorAvailable",
      "(Ljavax/microedition/sensor/SensorInfo;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT },
    { "sensorUnavailable",
      "(Ljavax/microedition/sensor/SensorInfo;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

static const nj_builtin_method dataerrlisten_methods[] = {
    { "errorReceived",
      "(Ljavax/microedition/sensor/SensorConnection;IJ)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

static const nj_builtin_method condlisten_methods[] = {
    { "conditionMet",
      "(Ljavax/microedition/sensor/SensorConnection;"
      "Ljavax/microedition/sensor/Data;"
      "Ljavax/microedition/sensor/Condition;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

static const nj_builtin_field condition_fields[] = {
    FSTR("OP_EQUALS"), FSTR("OP_GREATER_THAN"), FSTR("OP_GREATER_THAN_OR_EQUALS"),
    FSTR("OP_LESS_THAN"), FSTR("OP_LESS_THAN_OR_EQUALS")
};

static const nj_builtin_method condition_methods[] = {
    { "isMet", "(D)Z", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT },
    { "isMet", "(Ljava/lang/Object;)Z", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

static const nj_builtin_field channelinfo_fields[] = {
    FINT("TYPE_DOUBLE"), FINT("TYPE_INT"), FINT("TYPE_OBJECT")
};

static const nj_builtin_method channelinfo_methods[] = { X_CHANNELINFO(ABST) };

static const nj_builtin_method data_methods[] = { X_DATA(ABST) };

static const nj_builtin_method channel_methods[] = { X_CHANNEL(ABST) };

static const nj_builtin_field sensconn_fields[] = {
    FINT("STATE_OPENED"), FINT("STATE_LISTENING"), FINT("STATE_CLOSED")
};

static const nj_builtin_method sensconn_methods[] = { X_SENSCONN(ABST) };

static const nj_builtin_field sensorinfo_fields[] = {
    FINT("CONN_EMBEDDED"), FINT("CONN_REMOTE"), FINT("CONN_SHORT_RANGE_WIRELESS"),
    FINT("CONN_WIRED"),
    FSTR("CONTEXT_TYPE_USER"), FSTR("CONTEXT_TYPE_AMBIENT"),
    FSTR("CONTEXT_TYPE_DEVICE"), FSTR("CONTEXT_TYPE_VEHICLE"),
    FSTR("PROP_IS_CONTROLLABLE"), FSTR("PROP_IS_REPORTING_ERRORS"),
    FSTR("PROP_LATITUDE"), FSTR("PROP_LOCATION"), FSTR("PROP_LONGITUDE"),
    FSTR("PROP_MAX_RATE"), FSTR("PROP_PERMISSION"), FSTR("PROP_VENDOR"),
    FSTR("PROP_VERSION")
};

static const nj_builtin_method sensorinfo_methods[] = { X_SENSORINFO(ABST) };

/* --- MeasurementRange and SensorManager ---------------------------- */

static const nj_builtin_field measrange_fields[] = {
    { "rSmallest", "D", NJ_ACC_FINAL },
    { "rLargest", "D", NJ_ACC_FINAL },
    { "rResolution", "D", NJ_ACC_FINAL }
};

static const nj_builtin_method measrange_methods[] = {
    { "<init>", "(FFF)V", NJ_ACC_PUBLIC },
    { "<init>", "(DDD)V", NJ_ACC_PUBLIC },
    { "getSmallestValue", "()F", NJ_ACC_PUBLIC },
    { "getLargestValue", "()F", NJ_ACC_PUBLIC },
    { "getResolution", "()F", NJ_ACC_PUBLIC }
};

static const nj_builtin_method sensormgr_methods[] = {
    STAT("findSensors", "(Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;"),
    STAT("findSensors", "(Ljava/lang/String;Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;"),
    STAT("getSensorConnection", "(Ljavax/microedition/sensor/SensorInfo;)Ljavax/microedition/sensor/SensorConnection;"),
    STAT("addSensorListener", "(Ljavax/microedition/sensor/SensorListener;Ljavax/microedition/sensor/SensorInfo;)V"),
    STAT("addSensorListener", "(Ljavax/microedition/sensor/SensorListener;Ljava/lang/String;)V"),
    STAT("removeSensorListener", "(Ljavax/microedition/sensor/SensorListener;)V")
};

/* --- the objects behind the interfaces ----------------------------- */

static const nj_builtin_field sensorinfoimpl_fields[] = {
    { "sIndex", "I", NJ_ACC_FINAL }, { "sUrl", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "sQuantity", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "sContext", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "sModel", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "sChannels", "I", NJ_ACC_FINAL }, { "sMaxRate", "I", NJ_ACC_FINAL },
    { "sBuffer", "I", NJ_ACC_FINAL },
    { "sInfos", "[Ljavax/microedition/sensor/ChannelInfo;", NJ_ACC_FINAL }
};

static const nj_builtin_method sensorinfoimpl_methods[] = { X_SENSORINFO(CONC) };

static const nj_builtin_field channelinfoimpl_fields[] = {
    { "cName", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "cUnit", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "cDataType", "I", NJ_ACC_FINAL }, { "cScale", "F", NJ_ACC_FINAL },
    { "cAccuracy", "F", NJ_ACC_FINAL },
    { "cRanges", "[Ljavax/microedition/sensor/MeasurementRange;", NJ_ACC_FINAL }
};

static const nj_builtin_method channelinfoimpl_methods[] = { X_CHANNELINFO(CONC) };

static const nj_builtin_field dataimpl_fields[] = {
    { "dChannel", "Ljavax/microedition/sensor/ChannelInfo;", NJ_ACC_FINAL },
    { "dValue", "D", NJ_ACC_FINAL }, { "dTimestamp", "J", NJ_ACC_FINAL },
    { "dValues", "[D", NJ_ACC_FINAL }
};

static const nj_builtin_method dataimpl_methods[] = { X_DATA(CONC) };

static const nj_builtin_field sensconnimpl_fields[] = {
    { "kInfo", "Ljavax/microedition/sensor/SensorInfo;", NJ_ACC_FINAL },
    { "kInfos", "[Ljavax/microedition/sensor/ChannelInfo;", NJ_ACC_FINAL },
    { "kUrl", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "kHandle", "J", 0 }, { "kState", "I", 0 }, { "kPeriod", "J", 0 },
    { "kLast", "J", 0 },
    { "kListener", "Ljavax/microedition/sensor/DataListener;", 0 },
    { "kErrListener", "Ljavax/microedition/sensor/DataAndErrorListener;", 0 },
    { "kChannels", "Ljava/util/Vector;", NJ_ACC_FINAL }
};

static const nj_builtin_method sensconnimpl_methods[] = { X_SENSCONN(CONC) };

static const nj_builtin_field channelimpl_fields[] = {
    { "nInfo", "Ljavax/microedition/sensor/ChannelInfo;", NJ_ACC_FINAL },
    { "nConn", "Ljavax/microedition/sensor/SensorConnection;", NJ_ACC_FINAL },
    { "nUrl", "Ljava/lang/String;", NJ_ACC_FINAL },
    { "nRate", "F", 0 }
};

static const nj_builtin_method channelimpl_methods[] = { X_CHANNEL(CONC) };

/* --- the class table ----------------------------------------------- */

const nj_builtin_class nj_lib_sensor_classes[] = {
    { NJ_C_DATALISTEN, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, datalisten_methods, (nj_u2)(sizeof(datalisten_methods) / sizeof(datalisten_methods[0])) },
    { NJ_C_SENSLISTEN, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, sensorlisten_methods, (nj_u2)(sizeof(sensorlisten_methods) / sizeof(sensorlisten_methods[0])) },
    { NJ_C_DATAERR, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, dataerrlisten_methods, (nj_u2)(sizeof(dataerrlisten_methods) / sizeof(dataerrlisten_methods[0])) },
    { NJ_C_CONDLISTEN, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, condlisten_methods, (nj_u2)(sizeof(condlisten_methods) / sizeof(condlisten_methods[0])) },
    { NJ_C_CONDITION, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      condition_fields, (nj_u2)(sizeof(condition_fields) / sizeof(condition_fields[0])),
      condition_methods, (nj_u2)(sizeof(condition_methods) / sizeof(condition_methods[0])) },
    { NJ_C_CHANNELINFO, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      channelinfo_fields, (nj_u2)(sizeof(channelinfo_fields) / sizeof(channelinfo_fields[0])),
      channelinfo_methods, (nj_u2)(sizeof(channelinfo_methods) / sizeof(channelinfo_methods[0])) },
    { NJ_C_DATA, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, data_methods, (nj_u2)(sizeof(data_methods) / sizeof(data_methods[0])) },
    { NJ_C_CHANNEL, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, channel_methods, (nj_u2)(sizeof(channel_methods) / sizeof(channel_methods[0])) },
    { NJ_C_SENSCONN, "javax/microedition/io/Connection",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      sensconn_fields, (nj_u2)(sizeof(sensconn_fields) / sizeof(sensconn_fields[0])),
      sensconn_methods, (nj_u2)(sizeof(sensconn_methods) / sizeof(sensconn_methods[0])) },
    { NJ_C_SENSORINFO, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      sensorinfo_fields, (nj_u2)(sizeof(sensorinfo_fields) / sizeof(sensorinfo_fields[0])),
      sensorinfo_methods, (nj_u2)(sizeof(sensorinfo_methods) / sizeof(sensorinfo_methods[0])) },
    { NJ_C_MEASRANGE, "java/lang/Object", NJ_ACC_PUBLIC,
      measrange_fields, (nj_u2)(sizeof(measrange_fields) / sizeof(measrange_fields[0])),
      measrange_methods, (nj_u2)(sizeof(measrange_methods) / sizeof(measrange_methods[0])) },
    { NJ_C_SENSORMGR, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      NULL, 0u, sensormgr_methods, (nj_u2)(sizeof(sensormgr_methods) / sizeof(sensormgr_methods[0])) },
    { NJ_C_SENSORINFO_I, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      sensorinfoimpl_fields, (nj_u2)(sizeof(sensorinfoimpl_fields) / sizeof(sensorinfoimpl_fields[0])),
      sensorinfoimpl_methods, (nj_u2)(sizeof(sensorinfoimpl_methods) / sizeof(sensorinfoimpl_methods[0])) },
    { NJ_C_CHANNELINFO_I, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      channelinfoimpl_fields, (nj_u2)(sizeof(channelinfoimpl_fields) / sizeof(channelinfoimpl_fields[0])),
      channelinfoimpl_methods, (nj_u2)(sizeof(channelinfoimpl_methods) / sizeof(channelinfoimpl_methods[0])) },
    { NJ_C_DATA_I, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      dataimpl_fields, (nj_u2)(sizeof(dataimpl_fields) / sizeof(dataimpl_fields[0])),
      dataimpl_methods, (nj_u2)(sizeof(dataimpl_methods) / sizeof(dataimpl_methods[0])) },
    { NJ_C_SENSCONN_I, "javax/microedition/sensor/SensorConnection", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      sensconnimpl_fields, (nj_u2)(sizeof(sensconnimpl_fields) / sizeof(sensconnimpl_fields[0])),
      sensconnimpl_methods, (nj_u2)(sizeof(sensconnimpl_methods) / sizeof(sensconnimpl_methods[0])) },
    { NJ_C_CHANNEL_I, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      channelimpl_fields, (nj_u2)(sizeof(channelimpl_fields) / sizeof(channelimpl_fields[0])),
      channelimpl_methods, (nj_u2)(sizeof(channelimpl_methods) / sizeof(channelimpl_methods[0])) }
};

const int nj_lib_sensor_class_count =
    (int)(sizeof(nj_lib_sensor_classes) / sizeof(nj_lib_sensor_classes[0]));

#define NAT(c, n, d, f) { c, n, d, f }

const nj_native_entry nj_lib_sensor_table[] = {
    NAT(NJ_C_SENSORMGR, "findSensors", "(Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;", sensor_find_quantity),
    NAT(NJ_C_SENSORMGR, "findSensors", "(Ljava/lang/String;Ljava/lang/String;)[Ljavax/microedition/sensor/SensorInfo;", sensor_find_quantity_context),
    NAT(NJ_C_SENSORMGR, "getSensorConnection", "(Ljavax/microedition/sensor/SensorInfo;)Ljavax/microedition/sensor/SensorConnection;", sensor_get_connection),
    NAT(NJ_C_SENSORMGR, "addSensorListener", "(Ljavax/microedition/sensor/SensorListener;Ljavax/microedition/sensor/SensorInfo;)V", sensor_add_listener_info),
    NAT(NJ_C_SENSORMGR, "addSensorListener", "(Ljavax/microedition/sensor/SensorListener;Ljava/lang/String;)V", sensor_add_listener_quantity),
    NAT(NJ_C_SENSORMGR, "removeSensorListener", "(Ljavax/microedition/sensor/SensorListener;)V", sensor_remove_listener),

    NAT(NJ_C_SENSORINFO_I, "getUrl", "()Ljava/lang/String;", info_get_url),
    NAT(NJ_C_SENSORINFO_I, "getQuantity", "()Ljava/lang/String;", info_get_quantity),
    NAT(NJ_C_SENSORINFO_I, "getContextType", "()Ljava/lang/String;", info_get_context),
    NAT(NJ_C_SENSORINFO_I, "getModel", "()Ljava/lang/String;", info_get_model),
    NAT(NJ_C_SENSORINFO_I, "getConnectionType", "()I", info_get_connection_type),
    NAT(NJ_C_SENSORINFO_I, "getChannelInfos", "()[Ljavax/microedition/sensor/ChannelInfo;", info_get_channel_infos),
    NAT(NJ_C_SENSORINFO_I, "getMaxBufferSize", "()I", info_get_max_buffer),
    NAT(NJ_C_SENSORINFO_I, "isAvailable", "()Z", info_is_available),
    NAT(NJ_C_SENSORINFO_I, "isAvailabilityPushSupported", "()Z", info_availability_push),
    NAT(NJ_C_SENSORINFO_I, "isConditionPushSupported", "()Z", info_condition_push),
    NAT(NJ_C_SENSORINFO_I, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;", info_get_property),
    NAT(NJ_C_SENSORINFO_I, "getPropertyNames", "()[Ljava/lang/String;", info_get_property_names),
    NAT(NJ_C_SENSORINFO_I, "getDescription", "()Ljava/lang/String;", info_get_description),

    NAT(NJ_C_CHANNELINFO_I, "getName", "()Ljava/lang/String;", cinfo_get_name),
    NAT(NJ_C_CHANNELINFO_I, "getUnit", "()Ljava/lang/String;", cinfo_get_unit),
    NAT(NJ_C_CHANNELINFO_I, "getDataType", "()I", cinfo_get_data_type),
    NAT(NJ_C_CHANNELINFO_I, "getScale", "()F", cinfo_get_scale),
    NAT(NJ_C_CHANNELINFO_I, "getAccuracy", "()F", cinfo_get_accuracy),
    NAT(NJ_C_CHANNELINFO_I, "getMeasurementRanges", "()[Ljavax/microedition/sensor/MeasurementRange;", cinfo_get_ranges),

    NAT(NJ_C_MEASRANGE, "<init>", "(FFF)V", mrange_init_f),
    NAT(NJ_C_MEASRANGE, "<init>", "(DDD)V", mrange_init),
    NAT(NJ_C_MEASRANGE, "getSmallestValue", "()F", mrange_smallest),
    NAT(NJ_C_MEASRANGE, "getLargestValue", "()F", mrange_largest),
    NAT(NJ_C_MEASRANGE, "getResolution", "()F", mrange_resolution),

    NAT(NJ_C_DATA_I, "getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;", data_get_channel_info),
    NAT(NJ_C_DATA_I, "getDoubleValues", "()[D", data_get_double_values),
    NAT(NJ_C_DATA_I, "getIntValues", "()[I", data_get_int_values),
    NAT(NJ_C_DATA_I, "getObjectValues", "()[Ljava/lang/Object;", data_get_object_values),
    NAT(NJ_C_DATA_I, "getTimestamp", "(I)J", data_get_timestamp),
    NAT(NJ_C_DATA_I, "getUncertainty", "(I)F", data_get_uncertainty),
    NAT(NJ_C_DATA_I, "isValid", "(I)Z", data_is_valid),

    NAT(NJ_C_SENSCONN_I, "getSensorInfo", "()Ljavax/microedition/sensor/SensorInfo;", conn_get_sensor_info),
    NAT(NJ_C_SENSCONN_I, "getState", "()I", conn_get_state),
    NAT(NJ_C_SENSCONN_I, "getData", "(I)[Ljavax/microedition/sensor/Data;", conn_get_data_short),
    NAT(NJ_C_SENSCONN_I, "getData", "(IJZZZ)[Ljavax/microedition/sensor/Data;", conn_get_data_long),
    NAT(NJ_C_SENSCONN_I, "setDataListener", "(Ljavax/microedition/sensor/DataListener;I)V", conn_set_listener_short),
    NAT(NJ_C_SENSCONN_I, "setDataListener", "(Ljavax/microedition/sensor/DataListener;IJZZZ)V", conn_set_listener_long),
    NAT(NJ_C_SENSCONN_I, "removeDataListener", "()V", conn_remove_listener),
    NAT(NJ_C_SENSCONN_I, "getChannel", "(Ljavax/microedition/sensor/ChannelInfo;)Ljavax/microedition/sensor/Channel;", conn_get_channel),
    NAT(NJ_C_SENSCONN_I, "getErrorCodes", "()[I", conn_get_error_codes),
    NAT(NJ_C_SENSCONN_I, "getErrorText", "(I)Ljava/lang/String;", conn_get_error_text),
    NAT(NJ_C_SENSCONN_I, "close", "()V", conn_close),

    NAT(NJ_C_CHANNEL_I, "getChannelInfo", "()Ljavax/microedition/sensor/ChannelInfo;", channel_get_channel_info),
    NAT(NJ_C_CHANNEL_I, "getChannelUrl", "()Ljava/lang/String;", channel_get_url),
    NAT(NJ_C_CHANNEL_I, "addCondition", "(Ljavax/microedition/sensor/ConditionListener;Ljavax/microedition/sensor/Condition;)V", channel_add_condition),
    NAT(NJ_C_CHANNEL_I, "removeCondition", "(Ljavax/microedition/sensor/ConditionListener;Ljavax/microedition/sensor/Condition;)V", channel_remove_condition),
    NAT(NJ_C_CHANNEL_I, "getConditions", "(Ljavax/microedition/sensor/ConditionListener;)[Ljavax/microedition/sensor/Condition;", channel_get_conditions),
    NAT(NJ_C_CHANNEL_I, "removeAllConditions", "()V", channel_remove_all_conditions),
    NAT(NJ_C_CHANNEL_I, "removeConditionListener", "(Ljavax/microedition/sensor/ConditionListener;)V", channel_remove_condition_listener),
    NAT(NJ_C_CHANNEL_I, "setSampleRate", "(F)V", channel_set_sample_rate),
    NAT(NJ_C_CHANNEL_I, "getSampleRate", "()F", channel_get_sample_rate),
    NAT(NJ_C_CHANNEL_I, "getSampleRates", "()[F", channel_get_sample_rates)
};

const int nj_lib_sensor_count =
    (int)(sizeof(nj_lib_sensor_table) / sizeof(nj_lib_sensor_table[0]));
