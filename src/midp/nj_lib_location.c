/*
 * nano_j2me - src/midp/nj_lib_location.c
 *
 * JSR 179: javax.microedition.location.
 *
 * WHY THIS EXISTS
 * ---------------
 * Titles that name these classes die with NoClassDefFoundError inside their
 * own feature code when the runtime does not carry them.  The classes are
 * cheap; the interesting part is deciding what a phone without a GPS radio
 * can honestly answer.
 *
 * WHAT IS EMULATED
 * ----------------
 * There is one provider.  It is AVAILABLE and every fix is the same place:
 * 60.1699 N, 24.9384 E (Helsinki), 10 m altitude, 30 m horizontal accuracy,
 * 10 m vertical accuracy, speed 0, course 0, timestamp = the VM clock.  That
 * is what a device with an emulator feed would report when the feed is a
 * fixed point; it makes orientation/location dependent titles render instead
 * of dying, and it never invents movement that did not happen.  A title that
 * compares successive fixes sees distance 0 - the truth for a fixed feed.
 *
 * setLocationListener() stores the listener and delivers ONE
 * locationUpdated() immediately, from the calling thread.  A real provider
 * would deliver periodically from its own thread; this runtime does not run
 * a location pump, and a fake one would keep the game's CPU awake for no
 * data.  The immediate callback is enough for the "where am I once" pattern
 * and is documented as a limitation rather than hidden.
 *
 * getOrientation() (the compass half) answers an orientation of 0 degrees,
 * magnetic, pitch/roll 0 - a device lying flat pointing north.  There is no
 * compass radio to ask either.
 *
 * Landmark and LandmarkStore are phase 2: the store is per-VM in-memory and
 * not persisted to RMS.  They are declared in a later change so this file
 * stays reviewable.
 *
 * CLASSES ARE SPELLED THROUGH THEIR PARENTS
 * -----------------------------------------
 * A built-in class carries one super and no interface list (see the note at
 * the top of nj_lib_pim.c); is-a is spelled as the parent, and an interface
 * method is resolved in the interface's own table, so the implementation
 * class repeats the methods it exposes.
 *
 * STATE
 * -----
 * All state is in Java fields (the MIDP rule: no writable C globals).  Value
 * classes hold their numbers; the provider implementation holds the
 * registered listener reference; nothing here needs a registry yet.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <math.h>
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

/* ------------------------------------------------------------------ */
/* Class names                                                         */
/* ------------------------------------------------------------------ */
#define NJ_C_LOCEXC     "javax/microedition/location/LocationException"
#define NJ_C_COORD      "javax/microedition/location/Coordinates"
#define NJ_C_QCOORD     "javax/microedition/location/QualifiedCoordinates"
#define NJ_C_ADDR       "javax/microedition/location/AddressInfo"
#define NJ_C_ORIENT     "javax/microedition/location/Orientation"
#define NJ_C_CRIT       "javax/microedition/location/Criteria"
#define NJ_C_LOC        "javax/microedition/location/Location"
#define NJ_C_LOCLISTEN  "javax/microedition/location/LocationListener"
#define NJ_C_PROXLISTEN "javax/microedition/location/ProximityListener"
#define NJ_C_PROVIDER   "javax/microedition/location/LocationProvider"
#define NJ_C_PROVIDER_I "javax/microedition/location/LocationProviderImpl"

/* Field names (part of the ABI between this file and anything that looks the
 * objects up by name; there is nothing else yet, but keep the rule). */
#define F_LAT   "lat"       /* D */
#define F_LON   "lon"       /* D */
#define F_ALT   "alt"       /* F */
#define F_HACC  "hAcc"      /* F  QualifiedCoordinates horizontal accuracy  */
#define F_VACC  "vAcc"      /* F  QualifiedCoordinates vertical accuracy    */
#define F_AZ    "azimuth"   /* F  Orientation                               */
#define F_MAG   "magnetic"  /* Z  Orientation                               */
#define F_PITCH "pitch"     /* F  Orientation                               */
#define F_ROLL  "roll"      /* F  Orientation                               */
#define F_POWER "power"     /* I  Criteria preferred power consumption      */
#define F_COST  "costAllowed" /* Z Criteria                                   */
#define F_CHACC "horizontalAccuracy" /* I Criteria                           */
#define F_CVACC "verticalAccuracy"   /* I Criteria                           */
#define F_RESP  "preferredResponseTime" /* I Criteria                       */
#define F_SPEED "speedRequired"     /* Z Criteria                            */
#define F_ALTREQ "altitudeRequired" /* Z Criteria                            */
#define F_ADDRREQ "addressInfoRequired" /* Z Criteria                        */
#define F_QC    "qc"        /* LQualifiedCoordinates; Location coordinates  */
#define F_SPEEDV "speedValue" /* F Location                                 */
#define F_COURSE "courseValue" /* F Location                                */
#define F_TS    "timestamp" /* J  Location                                  */
#define F_METHOD "method"   /* I  Location                                  */
#define F_LAI   "addressInfo" /* LAddressInfo; Location                     */
#define F_EXTRA "extraInfo" /* LString; Location                            */
#define F_FIELDS "fields"   /* [LString; AddressInfo (17 slots)             */
#define F_EMAIL "email"     /* LString; AddressInfo                         */
#define F_LINE1 "line1"     /* LString; AddressInfo                         */
#define F_LINE2 "line2"     /* LString; AddressInfo                         */
#define F_PHONE "phone"     /* LString; AddressInfo                         */
#define F_LISTENER "listener" /* LLocationListener; provider impl           */
#define F_STATE "state"     /* I provider impl                              */
#define F_INTERVAL "interval" /* I provider impl                            */
#define F_TIMEOUT "timeout" /* I provider impl                              */

/* Constants (written in late-init: built-in classes carry no ConstantValue). */
#define LP_AVAILABLE 1
#define LP_TEMP_UNAVAILABLE 2
#define LP_OUT_OF_SERVICE 3
#define CRIT_NO_REQUIREMENT 0
#define CRIT_POWER_LOW 1
#define CRIT_POWER_MEDIUM 2
#define CRIT_POWER_HIGH 3
#define COORD_DD_MM_SS 1
#define COORD_DD_MM 2
#define LOC_MTE_SATELLITE 1
#define LOC_MTE_TIMEDIFFERENCE 2
#define LOC_MTE_TIMEOFARRIVAL 4
#define LOC_MTE_CELLID 8
#define LOC_MTE_SHORTRANGE 16
#define LOC_MTE_ANGLEOFARRIVAL 32
#define LOC_MTY_TERMINALBASED 65536
#define LOC_MTY_NETWORKBASED 131072
#define LOC_MTA_ASSISTED 262144
#define LOC_MTA_UNASSISTED 524288
#define ADDR_EXTENSION 1
#define ADDR_STREET 2
#define ADDR_POSTAL_CODE 3
#define ADDR_CITY 4
#define ADDR_COUNTY 5
#define ADDR_STATE 6
#define ADDR_COUNTRY 7
#define ADDR_COUNTRY_CODE 8
#define ADDR_DISTRICT 9
#define ADDR_BUILDING_NAME 10
#define ADDR_BUILDING_FLOOR 11
#define ADDR_BUILDING_ROOM 12
#define ADDR_BUILDING_ZONE 13
#define ADDR_CROSSING1 14
#define ADDR_CROSSING2 15
#define ADDR_URL 16
#define ADDR_PHONE 17
#define ADDR_NUM_FIELDS 17
#define ADDR_MAX_FIELD_SIZE 255

/* The emulated fix: Helsinki harbour, on land, stable. */
#define FIX_LAT 60.1699
#define FIX_LON 24.9384
#define FIX_ALT 10.0f
#define FIX_HACC 30.0f
#define FIX_VACC 10.0f

/* ------------------------------------------------------------------ */
/* Bit helpers: float/double fields hold bit patterns, not numbers     */
/* ------------------------------------------------------------------ */
static nj_s4 f2bits(float v)
{
    union { float f; nj_s4 i; } u;
    u.f = v;
    return u.i;
}

static float bits2float(nj_s4 v)
{
    union { float f; nj_s4 i; } u;
    u.i = v;
    return u.f;
}

static nj_u8 d2bits(double v)
{
    union { double d; nj_u8 u; } u;
    u.d = v;
    return u.u;
}

static double bits2double(nj_u8 v)
{
    union { double d; nj_u8 u; } u;
    u.u = v;
    return u.d;
}

/* ------------------------------------------------------------------ */
/* Field helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_s4 fget_i(nj_vm *vm, nj_ref o, const char *cls, const char *n)
{
    return nj_iget(vm, o, NJ_F(vm, cls, n, "I"));
}

static void fput_i(nj_vm *vm, nj_ref o, const char *cls, const char *n, nj_s4 v)
{
    nj_iput(vm, o, NJ_F(vm, cls, n, "I"), v);
}

static float fget_f(nj_vm *vm, nj_ref o, const char *cls, const char *n)
{
    return bits2float(nj_iget(vm, o, NJ_F(vm, cls, n, "F")));
}

static void fput_f(nj_vm *vm, nj_ref o, const char *cls, const char *n, float v)
{
    nj_iput(vm, o, NJ_F(vm, cls, n, "F"), f2bits(v));
}

static double fget_d(nj_vm *vm, nj_ref o, const char *cls, const char *n)
{
    return bits2double(nj_field_read_bits64(vm, o, NJ_F(vm, cls, n, "D")));
}

static void fput_d(nj_vm *vm, nj_ref o, const char *cls, const char *n, double v)
{
    nj_field_write_bits64(vm, o, NJ_F(vm, cls, n, "D"), d2bits(v));
}

static nj_ref fget_r(nj_vm *vm, nj_ref o, const char *cls, const char *n,
                     const char *desc)
{
    return nj_field_read_ref(vm, o, NJ_F(vm, cls, n, desc));
}

static void fput_r(nj_vm *vm, nj_ref o, const char *cls, const char *n,
                   const char *desc, nj_ref v)
{
    nj_field_write_ref(vm, o, NJ_F(vm, cls, n, desc), v);
}

/* Boolean fields are int-sized in this runtime (Z is stored as an int). */
static int fget_b(nj_vm *vm, nj_ref o, const char *cls, const char *n)
{
    return nj_iget(vm, o, NJ_F(vm, cls, n, "Z")) != 0;
}

static void fput_b(nj_vm *vm, nj_ref o, const char *cls, const char *n, int v)
{
    nj_iput(vm, o, NJ_F(vm, cls, n, "Z"), v ? 1 : 0);
}

static nj_ref qcoords_new(nj_vm *vm, double lat, double lon, float alt,
                          float hacc, float vacc)
{
    nj_ref o = nj_object_new(vm, NJ_C_QCOORD);
    if (!o) {
        return 0;
    }
    fput_d(vm, o, NJ_C_QCOORD, F_LAT, lat);
    fput_d(vm, o, NJ_C_QCOORD, F_LON, lon);
    fput_f(vm, o, NJ_C_QCOORD, F_ALT, alt);
    fput_f(vm, o, NJ_C_QCOORD, F_HACC, hacc);
    fput_f(vm, o, NJ_C_QCOORD, F_VACC, vacc);
    return o;
}

static nj_ref location_new(nj_vm *vm)
{
    nj_ref qc;
    nj_ref loc = nj_object_new(vm, NJ_C_LOC);
    if (!loc) {
        return 0;
    }
    qc = qcoords_new(vm, FIX_LAT, FIX_LON, FIX_ALT, FIX_HACC, FIX_VACC);
    if (!qc) {
        return 0;
    }
    fput_r(vm, loc, NJ_C_LOC, F_QC, "L" NJ_C_QCOORD ";", qc);
    fput_f(vm, loc, NJ_C_LOC, F_SPEEDV, 0.0f);
    fput_f(vm, loc, NJ_C_LOC, F_COURSE, 0.0f);
    fput_i(vm, loc, NJ_C_LOC, F_METHOD, 0);
    {
        nj_u8 ts = (nj_u8)nj_now_ms(vm);
        nj_field_write_bits64(vm, loc, NJ_F(vm, NJ_C_LOC, F_TS, "J"), ts);
    }
    return loc;
}

/* Deliver locationUpdated() to a listener now.  Returns NJ_OK, or an error
 * with vm->exception set (a callback that throws must not be swallowed). */
static nj_status deliver_location_update(nj_vm *vm, nj_ref provider, nj_ref loc)
{
    nj_ref listener = fget_r(vm, provider, NJ_C_PROVIDER_I, F_LISTENER,
                             "L" NJ_C_LOCLISTEN ";");
    nj_value args[2];
    nj_value ret;

    if (!listener) {
        return NJ_OK;
    }
    args[0].r = provider;
    args[1].r = loc;
    ret.r = 0;
    return nj_call(vm, listener, NJ_C_LOCLISTEN, "locationUpdated",
                   "(L" NJ_C_PROVIDER ";" "L" NJ_C_LOC ";)V", args, &ret);
}

/* ------------------------------------------------------------------ */
/* Natives: Coordinates / QualifiedCoordinates                         */
/* ------------------------------------------------------------------ */
static int latitude_ok(double v)
{
    return !(v != v) && v >= -90.0 && v <= 90.0;
}

static int longitude_ok(double v)
{
    return !(v != v) && v >= -180.0 && v <= 180.0;
}

NJFN(loc_coord_init)
{
    double lat = NJ_ARG_DOUBLE(args, 0);
    double lon = NJ_ARG_DOUBLE(args, 2);
    float alt = NJ_ARG_FLOAT(args, 4);
    nj_ref self = NJ_THIS(args);

    if (!latitude_ok(lat)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "latitude out of range");
    }
    if (!longitude_ok(lon)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "longitude out of range");
    }
    fput_d(vm, self, NJ_C_COORD, F_LAT, lat);
    fput_d(vm, self, NJ_C_COORD, F_LON, lon);
    fput_f(vm, self, NJ_C_COORD, F_ALT, alt);
    return NJ_OK;
}

NJFN(loc_coord_get_latitude)
{
    NJ_UNUSED(args);
    ret->d = fget_d(vm, NJ_THIS(args), NJ_C_COORD, F_LAT);
    return NJ_OK;
}

NJFN(loc_coord_get_longitude)
{
    NJ_UNUSED(args);
    ret->d = fget_d(vm, NJ_THIS(args), NJ_C_COORD, F_LON);
    return NJ_OK;
}

NJFN(loc_coord_get_altitude)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_COORD, F_ALT);
    return NJ_OK;
}

NJFN(loc_coord_set_latitude)
{
    double v = NJ_ARG_DOUBLE(args, 0);
    if (!latitude_ok(v)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "latitude out of range");
    }
    fput_d(vm, NJ_THIS(args), NJ_C_COORD, F_LAT, v);
    return NJ_OK;
}

NJFN(loc_coord_set_longitude)
{
    double v = NJ_ARG_DOUBLE(args, 0);
    if (!longitude_ok(v)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "longitude out of range");
    }
    fput_d(vm, NJ_THIS(args), NJ_C_COORD, F_LON, v);
    return NJ_OK;
}

NJFN(loc_coord_set_altitude)
{
    float v = NJ_ARG_FLOAT(args, 0);
    if (v != v) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "altitude is NaN");
    }
    fput_f(vm, NJ_THIS(args), NJ_C_COORD, F_ALT, v);
    return NJ_OK;
}

/* convert(double, int): format decimal degrees as D/M/S or D/M. */
NJFN(loc_coord_convert)
{
    double deg = NJ_ARG_DOUBLE(args, 0);
    nj_s4 type = NJ_ARG_INT(args, 2);
    char buf[48];
    const char *sign;
    long long hundredths;
    long long dd;
    long long mm;
    long long ss;

    if (type != COORD_DD_MM_SS && type != COORD_DD_MM) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "unknown output type");
    }
    if (!(deg == deg) || deg < -180.0 || deg > 180.0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "coordinate out of range");
    }
    sign = deg < 0.0 ? "-" : "";
    if (deg < 0.0) {
        deg = -deg;
    }
    if (type == COORD_DD_MM) {
        /* dd:mm.mmm - thousandths of a minute, carrying at 60. */
        hundredths = (long long)(deg * 60000.0 + 0.5);
        dd = hundredths / 60000;
        mm = (hundredths / 1000) % 60;
        ss = hundredths % 1000;
        snprintf(buf, sizeof(buf), "%s%lld:%02lld.%03lld", sign, dd, mm, ss);
    } else {
        /* dd:mm:ss.ss - hundredths of a second, carrying at 60. */
        hundredths = (long long)(deg * 360000.0 + 0.5);
        dd = hundredths / 360000;
        mm = (hundredths / 6000) % 60;
        ss = (hundredths / 100) % 60;
        snprintf(buf, sizeof(buf), "%s%lld:%02lld:%02lld.%02lld", sign, dd, mm,
                 ss, hundredths % 100);
    }
    ret->r = nj_string_new(vm, buf);
    return ret->r ? NJ_OK : NJ_ERR;
}

/* convert(String): accepts the spec's colon forms ("60:10:11.4",
 * "60:10.5"), space-separated D M [S] and plain decimal degrees, with an
 * optional trailing N/S/E/W that overrides the sign.  A local scanner keeps
 * the conversion independent of the C library's locale. */
static double loc_parse_num(const char **ps, int *any)
{
    const char *s = *ps;
    double whole = 0.0;
    double frac = 0.0;
    double scale = 1.0;

    while (*s >= '0' && *s <= '9') {
        whole = whole * 10.0 + (double)(*s - '0');
        s++;
        *any = 1;
    }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            scale *= 10.0;
            frac = frac * 10.0 + (double)(*s - '0');
            s++;
            *any = 1;
        }
    }
    *ps = s;
    return whole + frac / scale;
}

static double loc_parse_degrees(const char *s, int *ok)
{
    double parts[3];
    int nparts = 0;
    int sign = 1;
    int any;

    *ok = 0;
    if (!s) {
        return 0.0;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '-' || *s == '+') {
        sign = *s == '-' ? -1 : 1;
        s++;
    }
    any = 0;
    parts[nparts++] = loc_parse_num(&s, &any);
    if (!any) {
        return 0.0;
    }
    while (nparts < 3) {
        if (*s == ':') {
            s++;
        } else if (*s == ' ') {
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            if (!(*s >= '0' && *s <= '9')) {
                break;
            }
        } else {
            break;
        }
        any = 0;
        parts[nparts] = loc_parse_num(&s, &any);
        if (!any) {
            break;
        }
        nparts++;
    }
    {
        double deg = parts[0];
        if (nparts > 1) {
            deg += parts[1] / 60.0;
        }
        if (nparts > 2) {
            deg += parts[2] / 3600.0;
        }
        deg *= (double)sign;
        if (*s == 'S' || *s == 's' || *s == 'W' || *s == 'w') {
            deg = -deg;
        }
        *ok = 1;
        return deg;
    }
}

NJFN(loc_coord_convert_in)
{
    nj_ref str = NJ_ARG_REF(args, 0);
    char buf[64];
    int ok = 0;
    double v;

    if (!str) {
        return nj_throw(vm, "java/lang/NullPointerException", "coordinate");
    }
    if (nj_string_to_utf8(str, buf, (int)sizeof(buf)) <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "coordinate string is empty");
    }
    v = loc_parse_degrees(buf, &ok);
    if (!ok || !(v == v) || v < -180.0 || v > 180.0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "coordinate string is not a valid coordinate");
    }
    ret->d = v;
    return NJ_OK;
}

/* Azimuth and distance use the standard spherical formulas; a fixed feed
 * makes distance 0 and azimuth defined (0) for equal points. */
static double coord_lat(nj_vm *vm, nj_ref o, const char *cls)
{
    return fget_d(vm, o, cls, F_LAT);
}

static double coord_lon(nj_vm *vm, nj_ref o, const char *cls)
{
    return fget_d(vm, o, cls, F_LON);
}

static double to_rad(double d)
{
#if NJ_USE_LIBC
    return d * 3.14159265358979323846 / 180.0;
#else
    return d;
#endif
}

static double to_deg(double r)
{
#if NJ_USE_LIBC
    return r * 180.0 / 3.14159265358979323846;
#else
    return r;
#endif
}

NJFN(loc_coord_distance)
{
    nj_ref other = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);
    double lat1;
    double lon1;
    double lat2;
    double lon2;
    double dlat;
    double dlon;
    double a;

    if (!other) {
        return nj_throw(vm, "java/lang/NullPointerException", "to");
    }
    lat1 = to_rad(coord_lat(vm, self, NJ_C_COORD));
    lon1 = to_rad(coord_lon(vm, self, NJ_C_COORD));
    lat2 = to_rad(coord_lat(vm, other, NJ_C_COORD));
    lon2 = to_rad(coord_lon(vm, other, NJ_C_COORD));
    dlat = lat2 - lat1;
    dlon = lon2 - lon1;
#if NJ_USE_LIBC
    a = sin(dlat / 2.0) * sin(dlat / 2.0)
        + cos(lat1) * cos(lat2) * sin(dlon / 2.0) * sin(dlon / 2.0);
    if (a < 0.0) {
        a = 0.0;
    }
    if (a > 1.0) {
        a = 1.0;
    }
    ret->f = (float)(2.0 * 6371000.0 * asin(sqrt(a)));
#else
    ret->f = 0.0f;
#endif
    return NJ_OK;
}

NJFN(loc_coord_azimuth)
{
    nj_ref other = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);
    double lat1;
    double lon1;
    double lat2;
    double lon2;
    double dlon;
    double y;
    double x;
    double az;

    if (!other) {
        return nj_throw(vm, "java/lang/NullPointerException", "to");
    }
    lat1 = to_rad(coord_lat(vm, self, NJ_C_COORD));
    lon1 = to_rad(coord_lon(vm, self, NJ_C_COORD));
    lat2 = to_rad(coord_lat(vm, other, NJ_C_COORD));
    lon2 = to_rad(coord_lon(vm, other, NJ_C_COORD));
    dlon = lon2 - lon1;
#if NJ_USE_LIBC
    y = sin(dlon) * cos(lat2);
    x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon);
    az = to_deg(atan2(y, x));
    if (az < 0.0) {
        az += 360.0;
    }
    ret->f = (float)az;
#else
    NJ_UNUSED(dlon);
    ret->f = 0.0f;
#endif
    return NJ_OK;
}

NJFN(loc_coord_equals)
{
    nj_ref other = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);

    if (!other) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = fget_d(vm, self, NJ_C_COORD, F_LAT) == fget_d(vm, other, NJ_C_COORD, F_LAT)
          && fget_d(vm, self, NJ_C_COORD, F_LON) == fget_d(vm, other, NJ_C_COORD, F_LON)
          && fget_f(vm, self, NJ_C_COORD, F_ALT) == fget_f(vm, other, NJ_C_COORD, F_ALT);
    return NJ_OK;
}

NJFN(loc_coord_hash_code)
{
    double lat = fget_d(vm, NJ_THIS(args), NJ_C_COORD, F_LAT);
    double lon = fget_d(vm, NJ_THIS(args), NJ_C_COORD, F_LON);
    nj_u8 a = d2bits(lat);
    nj_u8 b = d2bits(lon);
    ret->i = (nj_s4)((a ^ (a >> 32)) ^ ((b << 1) ^ (b >> 31)));
    return NJ_OK;
}

NJFN(loc_qcoord_init)
{
    double lat = NJ_ARG_DOUBLE(args, 0);
    double lon = NJ_ARG_DOUBLE(args, 2);
    float alt = NJ_ARG_FLOAT(args, 4);
    float hacc = NJ_ARG_FLOAT(args, 5);
    float vacc = NJ_ARG_FLOAT(args, 6);
    nj_ref self = NJ_THIS(args);

    if (!latitude_ok(lat) || !longitude_ok(lon)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "coordinate out of range");
    }
    if (!(hacc >= 0.0f) || !(vacc >= 0.0f)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "accuracy must be non-negative");
    }
    fput_d(vm, self, NJ_C_QCOORD, F_LAT, lat);
    fput_d(vm, self, NJ_C_QCOORD, F_LON, lon);
    fput_f(vm, self, NJ_C_QCOORD, F_ALT, alt);
    fput_f(vm, self, NJ_C_QCOORD, F_HACC, hacc);
    fput_f(vm, self, NJ_C_QCOORD, F_VACC, vacc);
    return NJ_OK;
}

NJFN(loc_qcoord_get_hacc)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_QCOORD, F_HACC);
    return NJ_OK;
}

NJFN(loc_qcoord_get_vacc)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_QCOORD, F_VACC);
    return NJ_OK;
}

NJFN(loc_qcoord_set_hacc)
{
    float v = NJ_ARG_FLOAT(args, 0);
    if (!(v >= 0.0f)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "accuracy must be non-negative");
    }
    fput_f(vm, NJ_THIS(args), NJ_C_QCOORD, F_HACC, v);
    return NJ_OK;
}

NJFN(loc_qcoord_set_vacc)
{
    float v = NJ_ARG_FLOAT(args, 0);
    if (!(v >= 0.0f)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "accuracy must be non-negative");
    }
    fput_f(vm, NJ_THIS(args), NJ_C_QCOORD, F_VACC, v);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: AddressInfo                                                */
/* ------------------------------------------------------------------ */
static nj_ref addr_fields(nj_vm *vm, nj_ref self)
{
    nj_ref a = fget_r(vm, self, NJ_C_ADDR, F_FIELDS, "[Ljava/lang/String;");
    if (!a) {
        a = nj_array_new(vm, "java/lang/String", ADDR_NUM_FIELDS);
        if (!a) {
            return 0;
        }
        fput_r(vm, self, NJ_C_ADDR, F_FIELDS, "[Ljava/lang/String;", a);
    }
    return a;
}

static int addr_set_field(nj_vm *vm, nj_ref self, nj_s4 field, nj_ref value)
{
    nj_ref a = addr_fields(vm, self);
    if (!a) {
        return NJ_ERR;
    }
    ((nj_ref *)nj_array_data(a))[field - 1] = value;
    return NJ_OK;
}

static nj_ref addr_get_field(nj_vm *vm, nj_ref self, nj_s4 field)
{
    nj_ref a = fget_r(vm, self, NJ_C_ADDR, F_FIELDS, "[Ljava/lang/String;");
    if (!a) {
        return 0;
    }
    return ((nj_ref *)nj_array_data(a))[field - 1];
}

NJFN(loc_addr_init)
{
    nj_ref self = NJ_THIS(args);
    NJ_UNUSED(args);
    /* The array is created lazily; nothing else to initialise. */
    if (!addr_fields(vm, self)) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(loc_addr_init4)
{
    nj_ref self = NJ_THIS(args);
    nj_ref line1 = NJ_ARG_REF(args, 0);
    nj_ref line2 = NJ_ARG_REF(args, 1);
    nj_ref phone = NJ_ARG_REF(args, 2);
    nj_ref email = NJ_ARG_REF(args, 3);

    if (!addr_fields(vm, self)) {
        return NJ_ERR;
    }
    if (addr_set_field(vm, self, ADDR_EXTENSION, line1) != NJ_OK) {
        return NJ_ERR;
    }
    if (addr_set_field(vm, self, ADDR_STREET, line2) != NJ_OK) {
        return NJ_ERR;
    }
    if (addr_set_field(vm, self, ADDR_PHONE, phone) != NJ_OK) {
        return NJ_ERR;
    }
    fput_r(vm, self, NJ_C_ADDR, F_PHONE, "Ljava/lang/String;", phone);
    fput_r(vm, self, NJ_C_ADDR, F_EMAIL, "Ljava/lang/String;", email);
    fput_r(vm, self, NJ_C_ADDR, F_LINE1, "Ljava/lang/String;", line1);
    fput_r(vm, self, NJ_C_ADDR, F_LINE2, "Ljava/lang/String;", line2);
    return NJ_OK;
}

NJFN(loc_addr_get_field)
{
    nj_s4 field = NJ_ARG_INT(args, 0);
    if (field < 1 || field > ADDR_NUM_FIELDS) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "field number out of range");
    }
    ret->r = addr_get_field(vm, NJ_THIS(args), field);
    return NJ_OK;
}

NJFN(loc_addr_set_field)
{
    nj_s4 field = NJ_ARG_INT(args, 0);
    nj_ref value = NJ_ARG_REF(args, 1);
    nj_ref self = NJ_THIS(args);
    nj_s4 len;

    if (field < 1 || field > ADDR_NUM_FIELDS) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "field number out of range");
    }
    /* The specification truncates values longer than the slot allows. */
    if (value) {
        len = (nj_s4)nj_string_length(value);
        if (len > ADDR_MAX_FIELD_SIZE) {
            value = nj_string_new_chars(vm, nj_string_data(value),
                                        ADDR_MAX_FIELD_SIZE);
            if (!value) {
                return NJ_ERR;
            }
        }
    }
    if (addr_set_field(vm, self, field, value) != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(loc_addr_get_line1)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_ADDR, F_LINE1, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(loc_addr_get_line2)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_ADDR, F_LINE2, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(loc_addr_get_phone)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_ADDR, F_PHONE, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(loc_addr_get_email)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_ADDR, F_EMAIL, "Ljava/lang/String;");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: Orientation                                                */
/* ------------------------------------------------------------------ */
NJFN(loc_orient_init)
{
    nj_ref self = NJ_THIS(args);
    fput_f(vm, self, NJ_C_ORIENT, F_AZ, NJ_ARG_FLOAT(args, 0));
    fput_b(vm, self, NJ_C_ORIENT, F_MAG, NJ_ARG_INT(args, 1) != 0);
    fput_f(vm, self, NJ_C_ORIENT, F_PITCH, NJ_ARG_FLOAT(args, 2));
    fput_f(vm, self, NJ_C_ORIENT, F_ROLL, NJ_ARG_FLOAT(args, 3));
    return NJ_OK;
}

NJFN(loc_orient_get_azimuth)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_ORIENT, F_AZ);
    return NJ_OK;
}

NJFN(loc_orient_is_magnetic)
{
    NJ_UNUSED(args);
    ret->i = fget_b(vm, NJ_THIS(args), NJ_C_ORIENT, F_MAG);
    return NJ_OK;
}

NJFN(loc_orient_get_pitch)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_ORIENT, F_PITCH);
    return NJ_OK;
}

NJFN(loc_orient_get_roll)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_ORIENT, F_ROLL);
    return NJ_OK;
}

NJFN(loc_orient_get_orientation)
{
    /* No compass radio: answer "flat, pointing north" - the neutral value. */
    NJ_UNUSED(args);
    ret->r = nj_object_new(vm, NJ_C_ORIENT);
    if (!ret->r) {
        return NJ_ERR;
    }
    fput_f(vm, ret->r, NJ_C_ORIENT, F_AZ, 0.0f);
    fput_b(vm, ret->r, NJ_C_ORIENT, F_MAG, 1);
    fput_f(vm, ret->r, NJ_C_ORIENT, F_PITCH, 0.0f);
    fput_f(vm, ret->r, NJ_C_ORIENT, F_ROLL, 0.0f);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: Criteria                                                   */
/* ------------------------------------------------------------------ */
NJFN(loc_crit_init)
{
    nj_ref self = NJ_THIS(args);
    fput_i(vm, self, NJ_C_CRIT, F_POWER, CRIT_NO_REQUIREMENT);
    fput_b(vm, self, NJ_C_CRIT, F_COST, 0);
    fput_i(vm, self, NJ_C_CRIT, F_CHACC, 0);
    fput_i(vm, self, NJ_C_CRIT, F_CVACC, 0);
    fput_i(vm, self, NJ_C_CRIT, F_RESP, 0);
    fput_b(vm, self, NJ_C_CRIT, F_SPEED, 0);
    fput_b(vm, self, NJ_C_CRIT, F_ALTREQ, 0);
    fput_b(vm, self, NJ_C_CRIT, F_ADDRREQ, 0);
    return NJ_OK;
}

NJFN(loc_crit_get_power)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_CRIT, F_POWER);
    return NJ_OK;
}

NJFN(loc_crit_set_power)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    if (v < CRIT_NO_REQUIREMENT || v > CRIT_POWER_HIGH) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "power consumption level out of range");
    }
    fput_i(vm, NJ_THIS(args), NJ_C_CRIT, F_POWER, v);
    return NJ_OK;
}

NJFN(loc_crit_is_cost_allowed)
{
    NJ_UNUSED(args);
    ret->i = fget_b(vm, NJ_THIS(args), NJ_C_CRIT, F_COST);
    return NJ_OK;
}

NJFN(loc_crit_set_cost_allowed)
{
    fput_b(vm, NJ_THIS(args), NJ_C_CRIT, F_COST, NJ_ARG_INT(args, 0) != 0);
    return NJ_OK;
}

NJFN(loc_crit_get_hacc)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_CRIT, F_CHACC);
    return NJ_OK;
}

NJFN(loc_crit_set_hacc)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    if (v < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "accuracy must be non-negative");
    }
    fput_i(vm, NJ_THIS(args), NJ_C_CRIT, F_CHACC, v);
    return NJ_OK;
}

NJFN(loc_crit_get_vacc)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_CRIT, F_CVACC);
    return NJ_OK;
}

NJFN(loc_crit_set_vacc)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    if (v < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "accuracy must be non-negative");
    }
    fput_i(vm, NJ_THIS(args), NJ_C_CRIT, F_CVACC, v);
    return NJ_OK;
}

NJFN(loc_crit_get_response_time)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_CRIT, F_RESP);
    return NJ_OK;
}

NJFN(loc_crit_set_response_time)
{
    nj_s4 v = NJ_ARG_INT(args, 0);
    if (v < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "response time must be non-negative");
    }
    fput_i(vm, NJ_THIS(args), NJ_C_CRIT, F_RESP, v);
    return NJ_OK;
}

NJFN(loc_crit_is_speed_required)
{
    NJ_UNUSED(args);
    ret->i = fget_b(vm, NJ_THIS(args), NJ_C_CRIT, F_SPEED);
    return NJ_OK;
}

NJFN(loc_crit_set_speed_required)
{
    fput_b(vm, NJ_THIS(args), NJ_C_CRIT, F_SPEED, NJ_ARG_INT(args, 0) != 0);
    return NJ_OK;
}

NJFN(loc_crit_is_alt_required)
{
    NJ_UNUSED(args);
    ret->i = fget_b(vm, NJ_THIS(args), NJ_C_CRIT, F_ALTREQ);
    return NJ_OK;
}

NJFN(loc_crit_set_alt_required)
{
    fput_b(vm, NJ_THIS(args), NJ_C_CRIT, F_ALTREQ, NJ_ARG_INT(args, 0) != 0);
    return NJ_OK;
}

NJFN(loc_crit_is_addr_required)
{
    NJ_UNUSED(args);
    ret->i = fget_b(vm, NJ_THIS(args), NJ_C_CRIT, F_ADDRREQ);
    return NJ_OK;
}

NJFN(loc_crit_set_addr_required)
{
    fput_b(vm, NJ_THIS(args), NJ_C_CRIT, F_ADDRREQ, NJ_ARG_INT(args, 0) != 0);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: Location                                                   */
/* ------------------------------------------------------------------ */
NJFN(loc_loc_get_qc)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LOC, F_QC, "L" NJ_C_QCOORD ";");
    return NJ_OK;
}

NJFN(loc_loc_get_speed)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_LOC, F_SPEEDV);
    return NJ_OK;
}

NJFN(loc_loc_get_course)
{
    NJ_UNUSED(args);
    ret->f = fget_f(vm, NJ_THIS(args), NJ_C_LOC, F_COURSE);
    return NJ_OK;
}

NJFN(loc_loc_get_timestamp)
{
    NJ_UNUSED(args);
    ret->j = (nj_s8)nj_field_read_bits64(vm, NJ_THIS(args),
                                         NJ_F(vm, NJ_C_LOC, F_TS, "J"));
    return NJ_OK;
}

NJFN(loc_loc_get_method)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_LOC, F_METHOD);
    return NJ_OK;
}

NJFN(loc_loc_get_address)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LOC, F_LAI, "L" NJ_C_ADDR ";");
    return NJ_OK;
}

NJFN(loc_loc_get_extra)
{
    NJ_UNUSED(args);
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LOC, F_EXTRA, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(loc_loc_is_valid)
{
    NJ_UNUSED(args);
    ret->i = fget_r(vm, NJ_THIS(args), NJ_C_LOC, F_QC,
                    "L" NJ_C_QCOORD ";") != 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: LocationProvider                                          */
/* ------------------------------------------------------------------ */
NJFN(loc_provider_new)
{
    nj_ref p = nj_object_new(vm, NJ_C_PROVIDER_I);
    if (!p) {
        return NJ_ERR;
    }
    fput_i(vm, p, NJ_C_PROVIDER_I, F_STATE, LP_AVAILABLE);
    fput_i(vm, p, NJ_C_PROVIDER_I, F_INTERVAL, 0);
    fput_i(vm, p, NJ_C_PROVIDER_I, F_TIMEOUT, 0);
    ret->r = p;
    return NJ_OK;
}

NJFN(loc_provider_get_instance)
{
    /* A criteria says what the caller wants; the emulated provider can meet
     * every requirement cheaply, so it answers with itself. */
    NJ_UNUSED(args);
    return loc_provider_new(vm, th, args, ret);
}

NJFN(loc_provider_get_last)
{
    NJ_UNUSED(args);
    ret->r = location_new(vm);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(loc_provider_get_location)
{
    nj_s4 timeout = NJ_ARG_INT(args, 0);

    if (timeout < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "timeout must be -1 or non-negative");
    }
    ret->r = location_new(vm);
    if (!ret->r) {
        return NJ_ERR;
    }
    /* One immediate update for a registered listener (see the file header). */
    if (deliver_location_update(vm, NJ_THIS(args), ret->r) != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(loc_provider_set_listener)
{
    nj_ref self = NJ_THIS(args);
    nj_ref listener = NJ_ARG_REF(args, 0);
    nj_s4 interval = NJ_ARG_INT(args, 1);
    nj_ref loc;

    if (!listener) {
        return nj_throw(vm, "java/lang/NullPointerException", "listener");
    }
    if (interval < -1) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "interval must be -1 or non-negative");
    }
    fput_r(vm, self, NJ_C_PROVIDER_I, F_LISTENER,
           "L" NJ_C_LOCLISTEN ";", listener);
    fput_i(vm, self, NJ_C_PROVIDER_I, F_INTERVAL, interval);
    fput_i(vm, self, NJ_C_PROVIDER_I, F_TIMEOUT, NJ_ARG_INT(args, 2));
    loc = location_new(vm);
    if (!loc) {
        return NJ_ERR;
    }
    return deliver_location_update(vm, self, loc);
}

NJFN(loc_provider_state)
{
    NJ_UNUSED(args);
    ret->i = fget_i(vm, NJ_THIS(args), NJ_C_PROVIDER_I, F_STATE);
    return NJ_OK;
}

NJFN(loc_provider_reset)
{
    nj_ref self = NJ_THIS(args);
    NJ_UNUSED(args);
    fput_r(vm, self, NJ_C_PROVIDER_I, F_LISTENER,
           "L" NJ_C_LOCLISTEN ";", 0);
    fput_i(vm, self, NJ_C_PROVIDER_I, F_INTERVAL, 0);
    fput_i(vm, self, NJ_C_PROVIDER_I, F_TIMEOUT, 0);
    fput_i(vm, self, NJ_C_PROVIDER_I, F_STATE, LP_AVAILABLE);
    return NJ_OK;
}

NJFN(loc_provider_add_proximity)
{
    /* Proximity needs a moving fix; a fixed feed can never enter a radius it
     * did not start in, so the honest answer is the spec's exception. */
    NJ_UNUSED(args);
    return nj_throw(vm, NJ_C_LOCEXC,
                    "proximity monitoring is not available");
}

NJFN(loc_provider_remove_proximity)
{
    NJ_UNUSED(args);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field qcoord_fields[] = {
    FLD(F_HACC, "F", NJ_ACC_PRIVATE),
    FLD(F_VACC, "F", NJ_ACC_PRIVATE)
};

static const nj_builtin_field orient_fields[] = {
    FLD(F_AZ, "F", NJ_ACC_PRIVATE),
    FLD(F_MAG, "Z", NJ_ACC_PRIVATE),
    FLD(F_PITCH, "F", NJ_ACC_PRIVATE),
    FLD(F_ROLL, "F", NJ_ACC_PRIVATE)
};

static const nj_builtin_field provider_i_fields[] = {
    FLD(F_LISTENER, "L" NJ_C_LOCLISTEN ";", NJ_ACC_PRIVATE),
    FLD(F_STATE, "I", NJ_ACC_PRIVATE),
    FLD(F_INTERVAL, "I", NJ_ACC_PRIVATE),
    FLD(F_TIMEOUT, "I", NJ_ACC_PRIVATE)
};

/* Static final constant slots: written by nj_location_late_init(). */
#define CONST(name, desc) FLD(name, desc, NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL)

static const nj_builtin_field coord_all_fields[] = {
    FLD(F_LAT, "D", NJ_ACC_PRIVATE),
    FLD(F_LON, "D", NJ_ACC_PRIVATE),
    FLD(F_ALT, "F", NJ_ACC_PRIVATE),
    CONST("DD_MM_SS", "I"),
    CONST("DD_MM", "I")
};

static const nj_builtin_field crit_all_fields[] = {
    FLD(F_POWER, "I", NJ_ACC_PRIVATE),
    FLD(F_COST, "Z", NJ_ACC_PRIVATE),
    FLD(F_CHACC, "I", NJ_ACC_PRIVATE),
    FLD(F_CVACC, "I", NJ_ACC_PRIVATE),
    FLD(F_RESP, "I", NJ_ACC_PRIVATE),
    FLD(F_SPEED, "Z", NJ_ACC_PRIVATE),
    FLD(F_ALTREQ, "Z", NJ_ACC_PRIVATE),
    FLD(F_ADDRREQ, "Z", NJ_ACC_PRIVATE),
    CONST("NO_REQUIREMENT", "I"),
    CONST("POWER_USAGE_LOW", "I"),
    CONST("POWER_USAGE_MEDIUM", "I"),
    CONST("POWER_USAGE_HIGH", "I")
};

static const nj_builtin_field loc_all_fields[] = {
    FLD(F_QC, "L" NJ_C_QCOORD ";", NJ_ACC_PRIVATE),
    FLD(F_SPEEDV, "F", NJ_ACC_PRIVATE),
    FLD(F_COURSE, "F", NJ_ACC_PRIVATE),
    FLD(F_TS, "J", NJ_ACC_PRIVATE),
    FLD(F_METHOD, "I", NJ_ACC_PRIVATE),
    FLD(F_LAI, "L" NJ_C_ADDR ";", NJ_ACC_PRIVATE),
    FLD(F_EXTRA, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    CONST("MTE_SATELLITE", "I"),
    CONST("MTE_TIMEDIFFERENCE", "I"),
    CONST("MTE_TIMEOFARRIVAL", "I"),
    CONST("MTE_CELLID", "I"),
    CONST("MTE_SHORTRANGE", "I"),
    CONST("MTE_ANGLEOFARRIVAL", "I"),
    CONST("MTY_TERMINALBASED", "I"),
    CONST("MTY_NETWORKBASED", "I"),
    CONST("MTA_ASSISTED", "I"),
    CONST("MTA_UNASSISTED", "I")
};

static const nj_builtin_field provider_all_fields[] = {
    CONST("AVAILABLE", "I"),
    CONST("TEMPORARILY_UNAVAILABLE", "I"),
    CONST("OUT_OF_SERVICE", "I")
};

static const nj_builtin_field addr_all_fields[] = {
    FLD(F_FIELDS, "[Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_LINE1, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_LINE2, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_PHONE, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_EMAIL, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    CONST("EXTENSION", "I"),
    CONST("STREET", "I"),
    CONST("POSTAL_CODE", "I"),
    CONST("CITY", "I"),
    CONST("COUNTY", "I"),
    CONST("STATE", "I"),
    CONST("COUNTRY", "I"),
    CONST("COUNTRY_CODE", "I"),
    CONST("DISTRICT", "I"),
    CONST("BUILDING_NAME", "I"),
    CONST("BUILDING_FLOOR", "I"),
    CONST("BUILDING_ROOM", "I"),
    CONST("BUILDING_ZONE", "I"),
    CONST("CROSSING1", "I"),
    CONST("CROSSING2", "I"),
    CONST("URL", "I"),
    CONST("PHONE_NUMBER", "I")
};

/* Methods */
static const nj_builtin_method coord_methods[] = {
    M("<init>", "(DDF)V", NJ_ACC_PUBLIC),
    M("getLatitude", "()D", 0),
    M("getLongitude", "()D", 0),
    M("getAltitude", "()F", 0),
    M("setLatitude", "(D)V", 0),
    M("setLongitude", "(D)V", 0),
    M("setAltitude", "(F)V", 0),
    M("convert", "(Ljava/lang/String;)D", NJ_ACC_STATIC),
    M("convert", "(DI)Ljava/lang/String;", NJ_ACC_STATIC),
    M("azimuthTo", "(L" NJ_C_COORD ";)F", 0),
    M("distance", "(L" NJ_C_COORD ";)F", 0),
    M("equals", "(Ljava/lang/Object;)Z", 0),
    M("hashCode", "()I", 0)
};

static const nj_builtin_method qcoord_methods[] = {
    M("<init>", "(DDFFF)V", NJ_ACC_PUBLIC),
    M("getHorizontalAccuracy", "()F", 0),
    M("getVerticalAccuracy", "()F", 0),
    M("setHorizontalAccuracy", "(F)V", 0),
    M("setVerticalAccuracy", "(F)V", 0)
};

static const nj_builtin_method addr_methods[] = {
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
      NJ_ACC_PUBLIC),
    M("getField", "(I)Ljava/lang/String;", 0),
    M("setField", "(ILjava/lang/String;)V", 0),
    M("getLine1", "()Ljava/lang/String;", 0),
    M("getLine2", "()Ljava/lang/String;", 0),
    M("getPhoneNumber", "()Ljava/lang/String;", 0),
    M("getEmailAddress", "()Ljava/lang/String;", 0)
};

static const nj_builtin_method orient_methods[] = {
    M("<init>", "(FZFF)V", NJ_ACC_PUBLIC),
    M("getCompassAzimuth", "()F", 0),
    M("isOrientationMagnetic", "()Z", 0),
    M("getPitch", "()F", 0),
    M("getRoll", "()F", 0),
    M("getOrientation", "()L" NJ_C_ORIENT ";", NJ_ACC_STATIC)
};

static const nj_builtin_method crit_methods[] = {
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getPreferredPowerConsumption", "()I", 0),
    M("setPreferredPowerConsumption", "(I)V", 0),
    M("isAllowedToCost", "()Z", 0),
    M("setCostAllowed", "(Z)V", 0),
    M("getHorizontalAccuracy", "()I", 0),
    M("setHorizontalAccuracy", "(I)V", 0),
    M("getVerticalAccuracy", "()I", 0),
    M("setVerticalAccuracy", "(I)V", 0),
    M("getPreferredResponseTime", "()I", 0),
    M("setPreferredResponseTime", "(I)V", 0),
    M("isSpeedAndCourseRequired", "()Z", 0),
    M("setSpeedAndCourseRequired", "(Z)V", 0),
    M("isAltitudeRequired", "()Z", 0),
    M("setAltitudeRequired", "(Z)V", 0),
    M("isAddressInfoRequired", "()Z", 0),
    M("setAddressInfoRequired", "(Z)V", 0)
};

static const nj_builtin_method loc_methods[] = {
    M("isValid", "()Z", 0),
    M("getTimestamp", "()J", 0),
    M("getQualifiedCoordinates", "()L" NJ_C_QCOORD ";", 0),
    M("getSpeed", "()F", 0),
    M("getCourse", "()F", 0),
    M("getLocationMethod", "()I", 0),
    M("getAddressInfo", "()L" NJ_C_ADDR ";", 0),
    M("getExtraInfo", "(Ljava/lang/String;)Ljava/lang/String;", 0)
};

static const nj_builtin_method loclistener_methods[] = {
    M("locationUpdated", "(L" NJ_C_PROVIDER ";" "L" NJ_C_LOC ";)V",
      NJ_ACC_ABSTRACT),
    M("providerStateChanged", "(L" NJ_C_PROVIDER ";" "I)V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method proxlistener_methods[] = {
    M("proximityEvent", "(L" NJ_C_COORD ";" "L" NJ_C_LOC ";)V",
      NJ_ACC_ABSTRACT),
    M("monitoringStateChanged", "(Z)V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method provider_methods[] = {
    M("getInstance", "(L" NJ_C_CRIT ";)L" NJ_C_PROVIDER ";", NJ_ACC_STATIC),
    M("getInstance", "(L" NJ_C_CRIT ";Z)L" NJ_C_PROVIDER ";", NJ_ACC_STATIC),
    M("getLastKnownLocation", "()L" NJ_C_LOC ";", NJ_ACC_STATIC),
    M("addProximityListener", "(L" NJ_C_PROXLISTEN ";" "L" NJ_C_COORD ";" "F)V",
      NJ_ACC_STATIC),
    M("removeProximityListener", "(L" NJ_C_PROXLISTEN ";)V", NJ_ACC_STATIC),
    M("getLocation", "(I)L" NJ_C_LOC ";", NJ_ACC_ABSTRACT),
    M("setLocationListener", "(L" NJ_C_LOCLISTEN ";" "III)V", NJ_ACC_ABSTRACT),
    M("getState", "()I", NJ_ACC_ABSTRACT),
    M("reset", "()V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method provider_i_methods[] = {
    M("getLocation", "(I)L" NJ_C_LOC ";", 0),
    M("setLocationListener", "(L" NJ_C_LOCLISTEN ";" "III)V", 0),
    M("getState", "()I", 0),
    M("reset", "()V", 0)
};


/* ------------------------------------------------------------------ */
/* JSR-179 phase 2: Landmark, LandmarkStore (in-memory, per-VM)        */
/* ------------------------------------------------------------------ */
#define NJ_C_LANDMARK   "javax/microedition/location/Landmark"
#define NJ_C_LSTORE     "javax/microedition/location/LandmarkStore"
#define NJ_C_LMEXC      "javax/microedition/location/LandmarkException"

#define F_LM_NAME       "lmName"
#define F_LM_DESC       "lmDesc"
#define F_LM_COORD      "lmCoord"
#define F_LM_ADDR       "lmAddr"
#define F_LM_EXTRA      "lmExtra"
#define F_LS_NAME       "lsName"
#define F_LS_RECORDS    "lsRecords"
#define LS_VEC          "java/util/Vector"

/* Store records live in a per-VM static Vector of Object[3] entries:
 *   [0] String   store name
 *   [1] Landmark landmark (NULL for a store/category marker)
 *   [2] String   category (NULL for "uncategorised")
 * A record with landmark == NULL and category == NULL is the store marker;
 * a record with landmark == NULL and a category is the category marker.
 * The VM's GC sees the vector through the class's static slot, so nothing
 * here is a C global. */
static nj_ref ls_vec_new(nj_vm *vm);

/* The registry is created lazily on the first store call: creating it from
 * nj_location_late_init() would need a Java call during builtin setup, and
 * the native-call machinery only exists once the VM is running. */
static nj_ref ls_records(nj_vm *vm)
{
    nj_field *f = NJ_FS(vm, NJ_C_LSTORE, F_LS_RECORDS, "Ljava/util/Vector;");
    nj_ref vec;
    if (!f) {
        return 0;
    }
    vec = nj_field_read_ref(vm, NULL, f);
    if (!vec) {
        vec = ls_vec_new(vm);
        if (vec) {
            nj_field_write_ref(vm, NULL, f, vec);
        }
    }
    return vec;
}

static nj_ref ls_store_name(nj_vm *vm, nj_ref store)
{
    return fget_r(vm, store, NJ_C_LSTORE, F_LS_NAME, "Ljava/lang/String;");
}

static int ls_str_eq(nj_vm *vm, nj_ref a, nj_ref b)
{
    nj_value arg;
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    arg.r = b;
    return nj_call_int(vm, a, "java/lang/String", "equals",
                       "(Ljava/lang/Object;)Z", &arg) != 0;
}

static nj_ref ls_vec_new(nj_vm *vm)
{
    nj_ref v = nj_object_new(vm, LS_VEC);
    if (!v) {
        return 0;
    }
    if (nj_call_special(vm, v, LS_VEC, "<init>", "()V", NULL, NULL) != NJ_OK) {
        return 0;
    }
    return v;
}

static void ls_vec_add(nj_vm *vm, nj_ref v, nj_ref o)
{
    nj_value a;
    a.r = o;
    nj_call(vm, v, LS_VEC, "addElement", "(Ljava/lang/Object;)V", &a, NULL);
}

static nj_s4 ls_vec_size(nj_vm *vm, nj_ref v)
{
    return nj_call_int(vm, v, LS_VEC, "size", "()I", NULL);
}

static nj_ref ls_vec_get(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a;
    a.i = i;
    return nj_call_ref(vm, v, LS_VEC, "elementAt",
                       "(I)Ljava/lang/Object;", &a);
}

static void ls_vec_remove(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a;
    a.i = i;
    nj_call(vm, v, LS_VEC, "removeElementAt", "(I)V", &a, NULL);
}

static nj_ref ls_enum(nj_vm *vm, nj_ref v)
{
    return nj_call_ref(vm, v, LS_VEC, "elements", "()Ljava/util/Enumeration;",
                       NULL);
}

static nj_ref ls_rec_new(nj_vm *vm, nj_ref store_name, nj_ref lm, nj_ref cat)
{
    nj_ref rec = nj_object_array_new(vm, "java/lang/Object", 3u);
    if (!rec) {
        return 0;
    }
    nj_array_set_ref(rec, 0u, store_name);
    nj_array_set_ref(rec, 1u, lm);
    nj_array_set_ref(rec, 2u, cat);
    return rec;
}

static int ls_rec_in_store(nj_vm *vm, nj_ref rec, nj_ref store_name)
{
    return ls_str_eq(vm, nj_array_get_ref(rec, 0u), store_name);
}

static int ls_store_exists(nj_vm *vm, nj_ref name)
{
    nj_ref vec = ls_records(vm);
    nj_s4 n;
    nj_s4 i;

    if (!vec) {
        return 0;
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        if (ls_rec_in_store(vm, ls_vec_get(vm, vec, i), name)) {
            return 1;
        }
    }
    return 0;
}

static void ls_add_store_marker(nj_vm *vm, nj_ref name)
{
    nj_ref vec = ls_records(vm);
    if (vec) {
        ls_vec_add(vm, vec, ls_rec_new(vm, name, 0, 0));
    }
}

static nj_ref ls_new_store(nj_vm *vm, nj_ref name)
{
    nj_ref store = nj_object_new(vm, NJ_C_LSTORE);
    if (!store) {
        return 0;
    }
    fput_r(vm, store, NJ_C_LSTORE, F_LS_NAME, "Ljava/lang/String;", name);
    return store;
}

/* Returns the instance field for a Landmark, validated. */
static nj_ref landmark_name(nj_vm *vm, nj_ref lm)
{
    return fget_r(vm, lm, NJ_C_LANDMARK, F_LM_NAME, "Ljava/lang/String;");
}

static int landmark_matches(nj_vm *vm, nj_ref a, nj_ref b)
{
    nj_value arg;
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    arg.r = b;
    return nj_call_int(vm, a, NJ_C_LANDMARK, "equals",
                       "(Ljava/lang/Object;)Z", &arg) != 0;
}

static nj_ref ls_landmarks_vec(nj_vm *vm, nj_ref store, nj_ref category,
                               int has_category, nj_ref name, int has_name)
{
    nj_ref vec = ls_records(vm);
    nj_ref out = ls_vec_new(vm);
    nj_s4 n;
    nj_s4 i;

    if (!vec || !out) {
        return 0;
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        nj_ref lm = nj_array_get_ref(rec, 1u);
        nj_ref cat = nj_array_get_ref(rec, 2u);
        if (!ls_rec_in_store(vm, rec, ls_store_name(vm, store)) || !lm) {
            continue;
        }
        if (has_category && !ls_str_eq(vm, cat, category)) {
            continue;
        }
        if (has_name && name && !ls_str_eq(vm, landmark_name(vm, lm), name)) {
            continue;
        }
        ls_vec_add(vm, out, lm);
    }
    return out;
}

NJFN(lm_init)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_ref desc = NJ_ARG_REF(args, 1);
    nj_ref coords = NJ_ARG_REF(args, 2);
    nj_ref addr = NJ_ARG_REF(args, 3);
    nj_ref extra = NJ_ARG_REF(args, 4);
    nj_ref self = NJ_THIS(args);

    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "landmark name must not be null");
    }
    fput_r(vm, self, NJ_C_LANDMARK, F_LM_NAME, "Ljava/lang/String;", name);
    fput_r(vm, self, NJ_C_LANDMARK, F_LM_DESC, "Ljava/lang/String;", desc);
    fput_r(vm, self, NJ_C_LANDMARK, F_LM_COORD, "L" NJ_C_QCOORD ";", coords);
    fput_r(vm, self, NJ_C_LANDMARK, F_LM_ADDR, "L" NJ_C_ADDR ";", addr);
    fput_r(vm, self, NJ_C_LANDMARK, F_LM_EXTRA, "[B", extra);
    return NJ_OK;
}

NJFN(lm_get_name)
{
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_NAME,
                    "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(lm_get_desc)
{
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_DESC,
                    "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(lm_get_coords)
{
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_COORD,
                    "L" NJ_C_QCOORD ";");
    return NJ_OK;
}

NJFN(lm_get_addr)
{
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_ADDR,
                    "L" NJ_C_ADDR ";");
    return NJ_OK;
}

NJFN(lm_get_extra)
{
    ret->r = fget_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_EXTRA, "[B");
    return NJ_OK;
}

NJFN(lm_set_name)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "landmark name must not be null");
    }
    fput_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_NAME, "Ljava/lang/String;",
           name);
    return NJ_OK;
}

NJFN(lm_set_desc)
{
    fput_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_DESC, "Ljava/lang/String;",
           NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(lm_set_coords)
{
    fput_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_COORD, "L" NJ_C_QCOORD ";",
           NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(lm_set_addr)
{
    fput_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_ADDR, "L" NJ_C_ADDR ";",
           NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(lm_set_extra)
{
    fput_r(vm, NJ_THIS(args), NJ_C_LANDMARK, F_LM_EXTRA, "[B", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(lm_equals)
{
    ret->i = landmark_matches(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(lm_hash_code)
{
    nj_ref name = landmark_name(vm, NJ_THIS(args));
    ret->i = name ? nj_call_int(vm, name, "java/lang/String", "hashCode",
                                "()I", NULL) : 0;
    return NJ_OK;
}

NJFN(ls_get_instance)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_ref store;

    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "store name must not be null");
    }
    /* JSR-179: opening a store that does not exist yet creates it. */
    if (!ls_store_exists(vm, name)) {
        ls_add_store_marker(vm, name);
    }
    store = ls_new_store(vm, name);
    ret->r = store;
    return store ? NJ_OK : NJ_ERR;
}

NJFN(ls_create)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "store name must not be null");
    }
    if (ls_store_exists(vm, name)) {
        return nj_throw(vm, NJ_C_LMEXC, "store already exists");
    }
    ls_add_store_marker(vm, name);
    return NJ_OK;
}

NJFN(ls_delete_store)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_ref vec;
    nj_s4 i;
    int removed = 0;

    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "store name must not be null");
    }
    vec = ls_records(vm);
    if (!vec) {
        return nj_throw(vm, "java/io/IOException", "no landmark store");
    }
    for (i = ls_vec_size(vm, vec) - 1; i >= 0; i--) {
        if (ls_rec_in_store(vm, ls_vec_get(vm, vec, i), name)) {
            ls_vec_remove(vm, vec, i);
            removed++;
        }
    }
    if (!removed) {
        return nj_throw(vm, "java/io/IOException", "no such landmark store");
    }
    return NJ_OK;
}

NJFN(ls_list_stores)
{
    nj_ref vec = ls_records(vm);
    nj_ref out = ls_vec_new(vm);
    nj_s4 n;
    nj_s4 i;

    if (!vec || !out) {
        return NJ_ERR;
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        nj_ref name = nj_array_get_ref(rec, 0u);
        nj_ref lm = nj_array_get_ref(rec, 1u);
        nj_ref cat = nj_array_get_ref(rec, 2u);
        int seen = 0;
        nj_s4 j;

        if (!name || lm || cat) {
            continue;               /* only store markers name a store */
        }
        for (j = 0; j < ls_vec_size(vm, out); j++) {
            if (ls_str_eq(vm, ls_vec_get(vm, out, j), name)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            ls_vec_add(vm, out, name);
        }
    }
    ret->r = ls_enum(vm, out);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_add_landmark)
{
    nj_ref lm = NJ_ARG_REF(args, 0);
    nj_ref cat = NJ_ARG_REF(args, 1);
    nj_ref store = NJ_THIS(args);
    nj_ref name = ls_store_name(vm, store);
    nj_ref vec = ls_records(vm);
    nj_s4 n;
    nj_s4 i;

    if (!lm) {
        return nj_throw(vm, "java/lang/NullPointerException", "landmark");
    }
    if (!vec) {
        return nj_throw(vm, "java/io/IOException", "no landmark store");
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (ls_rec_in_store(vm, rec, name) &&
            nj_array_get_ref(rec, 1u) == lm) {
            return nj_throw(vm, NJ_C_LMEXC, "landmark already added");
        }
    }
    ls_vec_add(vm, vec, ls_rec_new(vm, name, lm, cat));
    return NJ_OK;
}

NJFN(ls_get_landmarks)
{
    nj_ref store = NJ_THIS(args);
    nj_ref lm = ls_landmarks_vec(vm, store, 0, 0, 0, 0);
    ret->r = lm ? ls_enum(vm, lm) : 0;
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_get_landmarks_cat)
{
    nj_ref store = NJ_THIS(args);
    nj_ref lm = ls_landmarks_vec(vm, store, NJ_ARG_REF(args, 0), 1, 0, 0);
    ret->r = lm ? ls_enum(vm, lm) : 0;
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_get_landmarks_cat_name)
{
    nj_ref store = NJ_THIS(args);
    nj_ref lm = ls_landmarks_vec(vm, store, NJ_ARG_REF(args, 0), 1,
                                 NJ_ARG_REF(args, 1), 1);
    ret->r = lm ? ls_enum(vm, lm) : 0;
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_get_landmarks_area)
{
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_ref out = ls_vec_new(vm);
    nj_ref name = ls_store_name(vm, store);
    double min_lat = NJ_ARG_DOUBLE(args, 1);
    double max_lat = NJ_ARG_DOUBLE(args, 3);
    double min_lon = NJ_ARG_DOUBLE(args, 5);
    double max_lon = NJ_ARG_DOUBLE(args, 7);
    nj_s4 n;
    nj_s4 i;

    if (!vec || !out) {
        return NJ_ERR;
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        nj_ref lm = nj_array_get_ref(rec, 1u);
        nj_ref coord;
        double lat;
        double lon;

        if (!ls_rec_in_store(vm, rec, name) || !lm) {
            continue;
        }
        coord = fget_r(vm, lm, NJ_C_LANDMARK, F_LM_COORD, "L" NJ_C_QCOORD ";");
        if (!coord) {
            continue;
        }
        lat = fget_d(vm, coord, NJ_C_COORD, F_LAT);
        lon = fget_d(vm, coord, NJ_C_COORD, F_LON);
        if (lat >= min_lat && lat <= max_lat && lon >= min_lon &&
            lon <= max_lon) {
            ls_vec_add(vm, out, lm);
        }
    }
    ret->r = ls_enum(vm, out);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_remove_landmark_cat)
{
    nj_ref lm = NJ_ARG_REF(args, 0);
    nj_ref cat = NJ_ARG_REF(args, 1);
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_s4 i;
    int found = 0;

    for (i = 0; i < ls_vec_size(vm, vec); i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (ls_rec_in_store(vm, rec, ls_store_name(vm, store)) &&
            nj_array_get_ref(rec, 1u) == lm &&
            ls_str_eq(vm, nj_array_get_ref(rec, 2u), cat)) {
            /* Leave the landmark in the store, uncategorised: removing it
             * from a category is not the same as deleting it. */
            nj_array_set_ref(rec, 2u, 0);
            found = 1;
            break;
        }
    }
    if (!found) {
        return nj_throw(vm, NJ_C_LMEXC, "landmark is not in that category");
    }
    return NJ_OK;
}

NJFN(ls_update_landmark)
{
    nj_ref lm = NJ_ARG_REF(args, 0);
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_s4 i;

    for (i = 0; i < ls_vec_size(vm, vec); i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (ls_rec_in_store(vm, rec, ls_store_name(vm, store)) &&
            nj_array_get_ref(rec, 1u) == lm) {
            return NJ_OK;
        }
    }
    return nj_throw(vm, NJ_C_LMEXC, "landmark is not in the store");
}

NJFN(ls_delete_landmark)
{
    nj_ref lm = NJ_ARG_REF(args, 0);
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_s4 i;
    int found = 0;

    for (i = ls_vec_size(vm, vec) - 1; i >= 0; i--) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (ls_rec_in_store(vm, rec, ls_store_name(vm, store)) &&
            nj_array_get_ref(rec, 1u) == lm) {
            ls_vec_remove(vm, vec, i);
            found = 1;
        }
    }
    if (!found) {
        return nj_throw(vm, NJ_C_LMEXC, "landmark is not in the store");
    }
    return NJ_OK;
}

NJFN(ls_get_categories)
{
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_ref out = ls_vec_new(vm);
    nj_ref name = ls_store_name(vm, store);
    nj_s4 n;
    nj_s4 i;

    if (!vec || !out) {
        return NJ_ERR;
    }
    n = ls_vec_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        nj_ref cat = nj_array_get_ref(rec, 2u);
        int seen = 0;
        nj_s4 j;

        if (!ls_rec_in_store(vm, rec, name) || !cat) {
            continue;
        }
        for (j = 0; j < ls_vec_size(vm, out); j++) {
            if (ls_str_eq(vm, ls_vec_get(vm, out, j), cat)) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            ls_vec_add(vm, out, cat);
        }
    }
    ret->r = ls_enum(vm, out);
    return ret->r ? NJ_OK : NJ_ERR;
}

NJFN(ls_add_category)
{
    nj_ref cat = NJ_ARG_REF(args, 0);
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_ref name = ls_store_name(vm, store);
    nj_s4 i;

    if (!cat) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "category must not be null");
    }
    for (i = 0; i < ls_vec_size(vm, vec); i++) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (ls_rec_in_store(vm, rec, name) &&
            !nj_array_get_ref(rec, 1u) &&
            ls_str_eq(vm, nj_array_get_ref(rec, 2u), cat)) {
            return nj_throw(vm, NJ_C_LMEXC, "category already exists");
        }
    }
    ls_vec_add(vm, vec, ls_rec_new(vm, name, 0, cat));
    return NJ_OK;
}

NJFN(ls_delete_category)
{
    nj_ref cat = NJ_ARG_REF(args, 0);
    nj_ref store = NJ_THIS(args);
    nj_ref vec = ls_records(vm);
    nj_ref name = ls_store_name(vm, store);
    nj_s4 i;
    int found = 0;

    for (i = ls_vec_size(vm, vec) - 1; i >= 0; i--) {
        nj_ref rec = ls_vec_get(vm, vec, i);
        if (!ls_rec_in_store(vm, rec, name)) {
            continue;
        }
        if (!nj_array_get_ref(rec, 1u) &&
            ls_str_eq(vm, nj_array_get_ref(rec, 2u), cat)) {
            ls_vec_remove(vm, vec, i);
            found = 1;
        } else if (nj_array_get_ref(rec, 1u) &&
                   ls_str_eq(vm, nj_array_get_ref(rec, 2u), cat)) {
            nj_array_set_ref(rec, 2u, 0);   /* uncategorised, landmark stays */
            found = 1;
        }
    }
    if (!found) {
        return nj_throw(vm, NJ_C_LMEXC, "no such category");
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Landmark / LandmarkStore field and method tables                    */
/* ------------------------------------------------------------------ */
static const nj_builtin_field landmark_fields[] = {
    FLD(F_LM_NAME, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_LM_DESC, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_LM_COORD, "L" NJ_C_QCOORD ";", NJ_ACC_PRIVATE),
    FLD(F_LM_ADDR, "L" NJ_C_ADDR ";", NJ_ACC_PRIVATE),
    FLD(F_LM_EXTRA, "[B", NJ_ACC_PRIVATE)
};

static const nj_builtin_method landmark_methods[] = {
    M("<init>", "(Ljava/lang/String;Ljava/lang/String;L" NJ_C_QCOORD ";L" NJ_C_ADDR ";[B)V", 0),
    M("getName", "()Ljava/lang/String;", 0),
    M("getDescription", "()Ljava/lang/String;", 0),
    M("getQualifiedCoordinates", "()L" NJ_C_QCOORD ";", 0),
    M("getAddressInfo", "()L" NJ_C_ADDR ";", 0),
    M("getExtraInfo", "()[B", 0),
    M("setName", "(Ljava/lang/String;)V", 0),
    M("setDescription", "(Ljava/lang/String;)V", 0),
    M("setQualifiedCoordinates", "(L" NJ_C_QCOORD ";)V", 0),
    M("setAddressInfo", "(L" NJ_C_ADDR ";)V", 0),
    M("setExtraInfo", "([B)V", 0),
    M("equals", "(Ljava/lang/Object;)Z", 0),
    M("hashCode", "()I", 0)
};

static const nj_builtin_field lstore_fields[] = {
    FLD(F_LS_NAME, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_LS_RECORDS, "Ljava/util/Vector;", NJ_ACC_PRIVATE | NJ_ACC_STATIC)
};

static const nj_builtin_method lstore_methods[] = {
    M("getInstance", "(Ljava/lang/String;)L" NJ_C_LSTORE ";", NJ_ACC_STATIC),
    M("createLandmarkStore", "(Ljava/lang/String;)V", NJ_ACC_STATIC),
    M("deleteLandmarkStore", "(Ljava/lang/String;)V", NJ_ACC_STATIC),
    M("listLandmarkStores", "()Ljava/util/Enumeration;", NJ_ACC_STATIC),
    M("addLandmark", "(L" NJ_C_LANDMARK ";Ljava/lang/String;)V", 0),
    M("getLandmarks", "()Ljava/util/Enumeration;", 0),
    M("getLandmarks", "(Ljava/lang/String;)Ljava/util/Enumeration;", 0),
    M("getLandmarks", "(Ljava/lang/String;Ljava/lang/String;)Ljava/util/Enumeration;", 0),
    M("getLandmarks", "(Ljava/lang/String;DDDD)Ljava/util/Enumeration;", 0),
    M("removeLandmarkFromCategory", "(L" NJ_C_LANDMARK ";Ljava/lang/String;)V", 0),
    M("updateLandmark", "(L" NJ_C_LANDMARK ";)V", 0),
    M("deleteLandmark", "(L" NJ_C_LANDMARK ";)V", 0),
    M("getCategories", "()Ljava/util/Enumeration;", 0),
    M("addCategory", "(Ljava/lang/String;)V", 0),
    M("deleteCategory", "(Ljava/lang/String;)V", 0)
};

const nj_builtin_class nj_lib_location_classes[] = {
    { NJ_C_LOCEXC, "java/lang/Exception", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },
    { NJ_C_COORD, "java/lang/Object", NJ_ACC_PUBLIC,
      coord_all_fields, (nj_u2)(sizeof(coord_all_fields) / sizeof(coord_all_fields[0])),
      coord_methods, (nj_u2)(sizeof(coord_methods) / sizeof(coord_methods[0])) },
    { NJ_C_QCOORD, NJ_C_COORD, NJ_ACC_PUBLIC,
      qcoord_fields, (nj_u2)(sizeof(qcoord_fields) / sizeof(qcoord_fields[0])),
      qcoord_methods, (nj_u2)(sizeof(qcoord_methods) / sizeof(qcoord_methods[0])) },
    { NJ_C_ADDR, "java/lang/Object", NJ_ACC_PUBLIC,
      addr_all_fields, (nj_u2)(sizeof(addr_all_fields) / sizeof(addr_all_fields[0])),
      addr_methods, (nj_u2)(sizeof(addr_methods) / sizeof(addr_methods[0])) },
    { NJ_C_ORIENT, "java/lang/Object", NJ_ACC_PUBLIC,
      orient_fields, (nj_u2)(sizeof(orient_fields) / sizeof(orient_fields[0])),
      orient_methods, (nj_u2)(sizeof(orient_methods) / sizeof(orient_methods[0])) },
    { NJ_C_CRIT, "java/lang/Object", NJ_ACC_PUBLIC,
      crit_all_fields, (nj_u2)(sizeof(crit_all_fields) / sizeof(crit_all_fields[0])),
      crit_methods, (nj_u2)(sizeof(crit_methods) / sizeof(crit_methods[0])) },
    { NJ_C_LOC, "java/lang/Object", NJ_ACC_PUBLIC,
      loc_all_fields, (nj_u2)(sizeof(loc_all_fields) / sizeof(loc_all_fields[0])),
      loc_methods, (nj_u2)(sizeof(loc_methods) / sizeof(loc_methods[0])) },
    { NJ_C_LOCLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, loclistener_methods,
      (nj_u2)(sizeof(loclistener_methods) / sizeof(loclistener_methods[0])) },
    { NJ_C_PROXLISTEN, NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, proxlistener_methods,
      (nj_u2)(sizeof(proxlistener_methods) / sizeof(proxlistener_methods[0])) },
    { NJ_C_PROVIDER, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      provider_all_fields, (nj_u2)(sizeof(provider_all_fields) / sizeof(provider_all_fields[0])),
      provider_methods, (nj_u2)(sizeof(provider_methods) / sizeof(provider_methods[0])) },
    { NJ_C_PROVIDER_I, NJ_C_PROVIDER, NJ_ACC_PUBLIC,
      provider_i_fields, (nj_u2)(sizeof(provider_i_fields) / sizeof(provider_i_fields[0])),
      provider_i_methods, (nj_u2)(sizeof(provider_i_methods) / sizeof(provider_i_methods[0])) },
    { NJ_C_LMEXC, "java/lang/Exception", NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_LANDMARK, "java/lang/Object", NJ_ACC_PUBLIC,
      landmark_fields, (nj_u2)(sizeof(landmark_fields) / sizeof(landmark_fields[0])),
      landmark_methods, (nj_u2)(sizeof(landmark_methods) / sizeof(landmark_methods[0])) },
    { NJ_C_LSTORE, "java/lang/Object", NJ_ACC_PUBLIC,
      lstore_fields, (nj_u2)(sizeof(lstore_fields) / sizeof(lstore_fields[0])),
      lstore_methods, (nj_u2)(sizeof(lstore_methods) / sizeof(lstore_methods[0])) }
};
const int nj_lib_location_class_count =
    (int)(sizeof(nj_lib_location_classes) / sizeof(nj_lib_location_classes[0]));

const nj_native_entry nj_lib_location_table[] = {
    { NJ_C_COORD, "<init>", "(DDF)V", loc_coord_init },
    { NJ_C_COORD, "getLatitude", "()D", loc_coord_get_latitude },
    { NJ_C_COORD, "getLongitude", "()D", loc_coord_get_longitude },
    { NJ_C_COORD, "getAltitude", "()F", loc_coord_get_altitude },
    { NJ_C_COORD, "setLatitude", "(D)V", loc_coord_set_latitude },
    { NJ_C_COORD, "setLongitude", "(D)V", loc_coord_set_longitude },
    { NJ_C_COORD, "setAltitude", "(F)V", loc_coord_set_altitude },
    { NJ_C_COORD, "convert", "(Ljava/lang/String;)D", loc_coord_convert_in },
    { NJ_C_COORD, "convert", "(DI)Ljava/lang/String;", loc_coord_convert },
    { NJ_C_COORD, "azimuthTo", "(L" NJ_C_COORD ";)F", loc_coord_azimuth },
    { NJ_C_COORD, "distance", "(L" NJ_C_COORD ";)F", loc_coord_distance },
    { NJ_C_COORD, "equals", "(Ljava/lang/Object;)Z", loc_coord_equals },
    { NJ_C_COORD, "hashCode", "()I", loc_coord_hash_code },

    { NJ_C_QCOORD, "<init>", "(DDFFF)V", loc_qcoord_init },
    { NJ_C_QCOORD, "getHorizontalAccuracy", "()F", loc_qcoord_get_hacc },
    { NJ_C_QCOORD, "getVerticalAccuracy", "()F", loc_qcoord_get_vacc },
    { NJ_C_QCOORD, "setHorizontalAccuracy", "(F)V", loc_qcoord_set_hacc },
    { NJ_C_QCOORD, "setVerticalAccuracy", "(F)V", loc_qcoord_set_vacc },

    { NJ_C_ADDR, "<init>", "()V", loc_addr_init },
    { NJ_C_ADDR, "<init>",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
      loc_addr_init4 },
    { NJ_C_ADDR, "getField", "(I)Ljava/lang/String;", loc_addr_get_field },
    { NJ_C_ADDR, "setField", "(ILjava/lang/String;)V", loc_addr_set_field },
    { NJ_C_ADDR, "getLine1", "()Ljava/lang/String;", loc_addr_get_line1 },
    { NJ_C_ADDR, "getLine2", "()Ljava/lang/String;", loc_addr_get_line2 },
    { NJ_C_ADDR, "getPhoneNumber", "()Ljava/lang/String;", loc_addr_get_phone },
    { NJ_C_ADDR, "getEmailAddress", "()Ljava/lang/String;", loc_addr_get_email },

    { NJ_C_ORIENT, "<init>", "(FZFF)V", loc_orient_init },
    { NJ_C_ORIENT, "getCompassAzimuth", "()F", loc_orient_get_azimuth },
    { NJ_C_ORIENT, "isOrientationMagnetic", "()Z", loc_orient_is_magnetic },
    { NJ_C_ORIENT, "getPitch", "()F", loc_orient_get_pitch },
    { NJ_C_ORIENT, "getRoll", "()F", loc_orient_get_roll },
    { NJ_C_ORIENT, "getOrientation", "()L" NJ_C_ORIENT ";",
      loc_orient_get_orientation },

    { NJ_C_CRIT, "<init>", "()V", loc_crit_init },
    { NJ_C_CRIT, "getPreferredPowerConsumption", "()I", loc_crit_get_power },
    { NJ_C_CRIT, "setPreferredPowerConsumption", "(I)V", loc_crit_set_power },
    { NJ_C_CRIT, "isAllowedToCost", "()Z", loc_crit_is_cost_allowed },
    { NJ_C_CRIT, "setCostAllowed", "(Z)V", loc_crit_set_cost_allowed },
    { NJ_C_CRIT, "getHorizontalAccuracy", "()I", loc_crit_get_hacc },
    { NJ_C_CRIT, "setHorizontalAccuracy", "(I)V", loc_crit_set_hacc },
    { NJ_C_CRIT, "getVerticalAccuracy", "()I", loc_crit_get_vacc },
    { NJ_C_CRIT, "setVerticalAccuracy", "(I)V", loc_crit_set_vacc },
    { NJ_C_CRIT, "getPreferredResponseTime", "()I", loc_crit_get_response_time },
    { NJ_C_CRIT, "setPreferredResponseTime", "(I)V", loc_crit_set_response_time },
    { NJ_C_CRIT, "isSpeedAndCourseRequired", "()Z", loc_crit_is_speed_required },
    { NJ_C_CRIT, "setSpeedAndCourseRequired", "(Z)V",
      loc_crit_set_speed_required },
    { NJ_C_CRIT, "isAltitudeRequired", "()Z", loc_crit_is_alt_required },
    { NJ_C_CRIT, "setAltitudeRequired", "(Z)V", loc_crit_set_alt_required },
    { NJ_C_CRIT, "isAddressInfoRequired", "()Z", loc_crit_is_addr_required },
    { NJ_C_CRIT, "setAddressInfoRequired", "(Z)V",
      loc_crit_set_addr_required },

    { NJ_C_LOC, "isValid", "()Z", loc_loc_is_valid },
    { NJ_C_LOC, "getTimestamp", "()J", loc_loc_get_timestamp },
    { NJ_C_LOC, "getQualifiedCoordinates", "()L" NJ_C_QCOORD ";",
      loc_loc_get_qc },
    { NJ_C_LOC, "getSpeed", "()F", loc_loc_get_speed },
    { NJ_C_LOC, "getCourse", "()F", loc_loc_get_course },
    { NJ_C_LOC, "getLocationMethod", "()I", loc_loc_get_method },
    { NJ_C_LOC, "getAddressInfo", "()L" NJ_C_ADDR ";", loc_loc_get_address },
    { NJ_C_LOC, "getExtraInfo", "(Ljava/lang/String;)Ljava/lang/String;",
      loc_loc_get_extra },

    { NJ_C_PROVIDER, "getInstance", "(L" NJ_C_CRIT ";)L" NJ_C_PROVIDER ";",
      loc_provider_get_instance },
    { NJ_C_PROVIDER, "getInstance",
      "(L" NJ_C_CRIT ";Z)L" NJ_C_PROVIDER ";", loc_provider_get_instance },
    { NJ_C_PROVIDER, "getLastKnownLocation", "()L" NJ_C_LOC ";",
      loc_provider_get_last },
    { NJ_C_PROVIDER, "addProximityListener",
      "(L" NJ_C_PROXLISTEN ";" "L" NJ_C_COORD ";" "F)V",
      loc_provider_add_proximity },
    { NJ_C_PROVIDER, "removeProximityListener", "(L" NJ_C_PROXLISTEN ";)V",
      loc_provider_remove_proximity },

    { NJ_C_PROVIDER_I, "getLocation", "(I)L" NJ_C_LOC ";",
      loc_provider_get_location },
    { NJ_C_PROVIDER_I, "setLocationListener",
      "(L" NJ_C_LOCLISTEN ";" "III)V", loc_provider_set_listener },
    { NJ_C_PROVIDER_I, "getState", "()I", loc_provider_state },
    { NJ_C_PROVIDER_I, "reset", "()V", loc_provider_reset },

    { NJ_C_LANDMARK, "<init>",
      "(Ljava/lang/String;Ljava/lang/String;L" NJ_C_QCOORD ";L" NJ_C_ADDR ";[B)V",
      lm_init },
    { NJ_C_LANDMARK, "getName", "()Ljava/lang/String;", lm_get_name },
    { NJ_C_LANDMARK, "getDescription", "()Ljava/lang/String;", lm_get_desc },
    { NJ_C_LANDMARK, "getQualifiedCoordinates", "()L" NJ_C_QCOORD ";",
      lm_get_coords },
    { NJ_C_LANDMARK, "getAddressInfo", "()L" NJ_C_ADDR ";", lm_get_addr },
    { NJ_C_LANDMARK, "getExtraInfo", "()[B", lm_get_extra },
    { NJ_C_LANDMARK, "setName", "(Ljava/lang/String;)V", lm_set_name },
    { NJ_C_LANDMARK, "setDescription", "(Ljava/lang/String;)V", lm_set_desc },
    { NJ_C_LANDMARK, "setQualifiedCoordinates", "(L" NJ_C_QCOORD ";)V",
      lm_set_coords },
    { NJ_C_LANDMARK, "setAddressInfo", "(L" NJ_C_ADDR ";)V", lm_set_addr },
    { NJ_C_LANDMARK, "setExtraInfo", "([B)V", lm_set_extra },
    { NJ_C_LANDMARK, "equals", "(Ljava/lang/Object;)Z", lm_equals },
    { NJ_C_LANDMARK, "hashCode", "()I", lm_hash_code },

    { NJ_C_LSTORE, "getInstance", "(Ljava/lang/String;)L" NJ_C_LSTORE ";",
      ls_get_instance },
    { NJ_C_LSTORE, "createLandmarkStore", "(Ljava/lang/String;)V", ls_create },
    { NJ_C_LSTORE, "deleteLandmarkStore", "(Ljava/lang/String;)V",
      ls_delete_store },
    { NJ_C_LSTORE, "listLandmarkStores", "()Ljava/util/Enumeration;",
      ls_list_stores },
    { NJ_C_LSTORE, "addLandmark", "(L" NJ_C_LANDMARK ";Ljava/lang/String;)V",
      ls_add_landmark },
    { NJ_C_LSTORE, "getLandmarks", "()Ljava/util/Enumeration;",
      ls_get_landmarks },
    { NJ_C_LSTORE, "getLandmarks", "(Ljava/lang/String;)Ljava/util/Enumeration;",
      ls_get_landmarks_cat },
    { NJ_C_LSTORE, "getLandmarks",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/util/Enumeration;",
      ls_get_landmarks_cat_name },
    { NJ_C_LSTORE, "getLandmarks",
      "(Ljava/lang/String;DDDD)Ljava/util/Enumeration;", ls_get_landmarks_area },
    { NJ_C_LSTORE, "removeLandmarkFromCategory",
      "(L" NJ_C_LANDMARK ";Ljava/lang/String;)V", ls_remove_landmark_cat },
    { NJ_C_LSTORE, "updateLandmark", "(L" NJ_C_LANDMARK ";)V",
      ls_update_landmark },
    { NJ_C_LSTORE, "deleteLandmark", "(L" NJ_C_LANDMARK ";)V",
      ls_delete_landmark },
    { NJ_C_LSTORE, "getCategories", "()Ljava/util/Enumeration;",
      ls_get_categories },
    { NJ_C_LSTORE, "addCategory", "(Ljava/lang/String;)V", ls_add_category },
    { NJ_C_LSTORE, "deleteCategory", "(Ljava/lang/String;)V",
      ls_delete_category }
};
const int nj_lib_location_count =
    (int)(sizeof(nj_lib_location_table) / sizeof(nj_lib_location_table[0]));

/* ------------------------------------------------------------------ */
/* Late initialisation: static final constants                         */
/* ------------------------------------------------------------------ */
static void loc_const_int(nj_vm *vm, const char *cls, const char *name, nj_s4 v)
{
    nj_field *f = NJ_FS(vm, cls, name, "I");
    if (f) {
        nj_field_write_int(vm, NULL, f, v);
    }
}

nj_status nj_location_late_init(nj_vm *vm)
{
    if (!vm) {
        return NJ_ERR;
    }
    loc_const_int(vm, NJ_C_COORD, "DD_MM_SS", COORD_DD_MM_SS);
    loc_const_int(vm, NJ_C_COORD, "DD_MM", COORD_DD_MM);

    loc_const_int(vm, NJ_C_CRIT, "NO_REQUIREMENT", CRIT_NO_REQUIREMENT);
    loc_const_int(vm, NJ_C_CRIT, "POWER_USAGE_LOW", CRIT_POWER_LOW);
    loc_const_int(vm, NJ_C_CRIT, "POWER_USAGE_MEDIUM", CRIT_POWER_MEDIUM);
    loc_const_int(vm, NJ_C_CRIT, "POWER_USAGE_HIGH", CRIT_POWER_HIGH);

    loc_const_int(vm, NJ_C_LOC, "MTE_SATELLITE", LOC_MTE_SATELLITE);
    loc_const_int(vm, NJ_C_LOC, "MTE_TIMEDIFFERENCE", LOC_MTE_TIMEDIFFERENCE);
    loc_const_int(vm, NJ_C_LOC, "MTE_TIMEOFARRIVAL", LOC_MTE_TIMEOFARRIVAL);
    loc_const_int(vm, NJ_C_LOC, "MTE_CELLID", LOC_MTE_CELLID);
    loc_const_int(vm, NJ_C_LOC, "MTE_SHORTRANGE", LOC_MTE_SHORTRANGE);
    loc_const_int(vm, NJ_C_LOC, "MTE_ANGLEOFARRIVAL", LOC_MTE_ANGLEOFARRIVAL);
    loc_const_int(vm, NJ_C_LOC, "MTY_TERMINALBASED", LOC_MTY_TERMINALBASED);
    loc_const_int(vm, NJ_C_LOC, "MTY_NETWORKBASED", LOC_MTY_NETWORKBASED);
    loc_const_int(vm, NJ_C_LOC, "MTA_ASSISTED", LOC_MTA_ASSISTED);
    loc_const_int(vm, NJ_C_LOC, "MTA_UNASSISTED", LOC_MTA_UNASSISTED);

    loc_const_int(vm, NJ_C_PROVIDER, "AVAILABLE", LP_AVAILABLE);
    loc_const_int(vm, NJ_C_PROVIDER, "TEMPORARILY_UNAVAILABLE",
                  LP_TEMP_UNAVAILABLE);
    loc_const_int(vm, NJ_C_PROVIDER, "OUT_OF_SERVICE", LP_OUT_OF_SERVICE);

    loc_const_int(vm, NJ_C_ADDR, "EXTENSION", ADDR_EXTENSION);
    loc_const_int(vm, NJ_C_ADDR, "STREET", ADDR_STREET);
    loc_const_int(vm, NJ_C_ADDR, "POSTAL_CODE", ADDR_POSTAL_CODE);
    loc_const_int(vm, NJ_C_ADDR, "CITY", ADDR_CITY);
    loc_const_int(vm, NJ_C_ADDR, "COUNTY", ADDR_COUNTY);
    loc_const_int(vm, NJ_C_ADDR, "STATE", ADDR_STATE);
    loc_const_int(vm, NJ_C_ADDR, "COUNTRY", ADDR_COUNTRY);
    loc_const_int(vm, NJ_C_ADDR, "COUNTRY_CODE", ADDR_COUNTRY_CODE);
    loc_const_int(vm, NJ_C_ADDR, "DISTRICT", ADDR_DISTRICT);
    loc_const_int(vm, NJ_C_ADDR, "BUILDING_NAME", ADDR_BUILDING_NAME);
    loc_const_int(vm, NJ_C_ADDR, "BUILDING_FLOOR", ADDR_BUILDING_FLOOR);
    loc_const_int(vm, NJ_C_ADDR, "BUILDING_ROOM", ADDR_BUILDING_ROOM);
    loc_const_int(vm, NJ_C_ADDR, "BUILDING_ZONE", ADDR_BUILDING_ZONE);
    loc_const_int(vm, NJ_C_ADDR, "CROSSING1", ADDR_CROSSING1);
    loc_const_int(vm, NJ_C_ADDR, "CROSSING2", ADDR_CROSSING2);
    loc_const_int(vm, NJ_C_ADDR, "URL", ADDR_URL);
    loc_const_int(vm, NJ_C_ADDR, "PHONE_NUMBER", ADDR_PHONE);

    return NJ_OK;
}
