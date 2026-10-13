/*
 * javax.bluetooth - JSR 82, the part of it a game actually uses.
 *
 * Why this exists: nine of the 436 titles in the test corpus name a class from
 * this package, and every one of them is a game with a two-player mode over
 * the radio ("蓝牙对战", Bluetooth battle).  What they ask for is a narrow
 * slice of the specification - find the local device, ask for its friendly
 * name, start an inquiry, search a device for services, open the btspp:// URL
 * that comes back - and that is exactly what is implemented here.  The parts
 * that need a real stack on the other end (authentication, OBEX, L2CAP
 * options, the SDP attribute database as a writable store) answer the way the
 * specification says an unsupported feature answers: with an exception that
 * names it.
 *
 * The radio belongs to the port.  Nine optional hooks describe it (see
 * nj_port.h): the local device, discoverability, inquiry, service search, and
 * a listen/accept/read/write/close family for servers.  A CLIENT connection
 * needs no Bluetooth hook at all - Connector.open("btspp://001122334455:3")
 * hands the URL to socket_connect(), which is the same byte stream over a
 * different radio - so a port that has TCP and Bluetooth writes that one
 * function and gets both schemes.
 *
 * With no hooks the package is still present: LocalDevice.getLocalDevice()
 * throws BluetoothStateException("this device has no Bluetooth radio"), and
 * Connector.open("btspp://...") throws ConnectionNotFoundException naming the
 * URL.  A missing class would say nothing at all.
 *
 * Threading: this runtime's threads are cooperative, so inquiry and service
 * search cannot run on a thread of their own.  They are jobs that the frame
 * pump (nj_bt_frame, beside nj_media_frame and nj_sensor_frame) advances once
 * per frame and then hands to the MIDlet's DiscoveryListener.  That is later
 * than a handset would call it and never earlier than the next frame, which is
 * the same trade the sensor package makes.
 */
#include "nj_midp.h"

#include <string.h>

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_UUID     "javax/bluetooth/UUID"
#define NJ_C_DEVCLASS "javax/bluetooth/DeviceClass"
#define NJ_C_BTSTATE  "javax/bluetooth/BluetoothStateException"
#define NJ_C_DELEM    "javax/bluetooth/DataElement"
#define NJ_C_SVCREC   "javax/bluetooth/ServiceRecord"
#define NJ_C_REMOTE   "javax/bluetooth/RemoteDevice"
#define NJ_C_LOCAL    "javax/bluetooth/LocalDevice"
#define NJ_C_AGENT    "javax/bluetooth/DiscoveryAgent"
#define NJ_C_LISTENER "javax/bluetooth/DiscoveryListener"
#define NJ_C_L2CAP    "javax/bluetooth/L2CAPConnection"
#define NJ_C_L2CAPN   "javax/bluetooth/L2CAPConnectionNotifier"
#define NJ_C_JOB      "javax/bluetooth/BtJob"

/* The runtime's own classes: the objects a MIDlet really holds. */
#define NJ_C_BTSTREAM "javax/bluetooth/BtStream"
#define NJ_C_BTSERVER "javax/bluetooth/BtNotifier"
#define NJ_C_BTL2     "javax/bluetooth/BtL2cap"
#define NJ_C_BTL2N    "javax/bluetooth/BtL2Notifier"
#define NJ_C_BTIN     "javax/bluetooth/BtInputStream"
#define NJ_C_BTOUT    "javax/bluetooth/BtOutputStream"

#define NJ_C_STREAM    "javax/microedition/io/StreamConnection"
#define NJ_C_STREAMNOT "javax/microedition/io/StreamConnectionNotifier"
#define NJ_C_ISTREAM   "java/io/InputStream"
#define NJ_C_OSTREAM   "java/io/OutputStream"
#define NJ_C_VECTOR    "java/util/Vector"

/* UUID: the 128 bits, as two longs so a 16-bit UUID reads naturally. */
#define F_UHI  "uHi"      /* J */
#define F_ULO  "uLo"      /* J */

/* DeviceClass */
#define F_DVAL "dVal"     /* I */

/* DataElement */
#define F_ETYPE "eType"   /* I  one of the constants below */
#define F_ENUM  "eNum"    /* J  a number, when the type is a number */
#define F_EOBJ  "eObj"    /* Ljava/lang/Object;  a String, UUID or byte[] */
#define F_ESEQ  "eSeq"    /* Ljava/util/Vector;  DATSEQ/DATALT children */

/* ServiceRecord: what a service search found, or what a server registered. */
#define F_RHOST "rHost"   /* LRemoteDevice;  the device it lives on */
#define F_RURL  "rUrl"    /* Ljava/lang/String;  the connection URL */
#define F_RNAME "rName"   /* Ljava/lang/String;  the service name */
#define F_RUUID "rUuid"   /* LUUID;  the service class */
#define F_RIDS  "rIds"    /* [I  attribute ids, parallel to rVals */
#define F_RVALS "rVals"   /* [Ljava/lang/Object;  DataElements */

/* RemoteDevice */
#define F_VADDR "vAddr"   /* Ljava/lang/String;  "001122334455" */
#define F_VNAME "vName"   /* Ljava/lang/String;  friendly name, may be "" */
#define F_VCLS  "vCls"    /* I  class of device */

/* The connection object (RFCOMM stream or L2CAP channel). */
#define F_BHANDLE "bHandle"  /* J  the port's handle; 0 = closed */
#define F_BURL    "bUrl"     /* Ljava/lang/String; */
#define F_BADDR   "bAddr"    /* Ljava/lang/String;  the peer */
#define F_BIN     "bIn"      /* Ljava/io/InputStream; */
#define F_BOUT    "bOut"     /* Ljava/io/OutputStream; */
#define F_BCLOSED "bClosed"  /* Z */

/* The notifier (a server waiting for a peer). */
#define F_NHANDLE "nHandle"  /* J */
#define F_NURL    "nUrl"     /* Ljava/lang/String; */
#define F_NUUID   "nUuid"    /* Ljava/lang/String; */
#define F_NL2     "nL2"      /* Z  an L2CAP server, not RFCOMM */
#define F_NCLOSED "nClosed"  /* Z */

/* The streams share a shape with the socket ones: which connection, and
 * whether this end has been closed on its own. */
#define F_SCONN   "sConn"    /* LBtStream */
#define F_SCLOSED "sClosed"  /* Z */

/* The job the frame pump advances. */
#define F_JKIND   "jKind"    /* I  1 inquiry, 2 service search */
#define F_JLISTEN "jListener"/* LDiscoveryListener; */
#define F_JFIRED  "jFired"  /* I  devices already handed to the listener */
#define F_JACCESS "jAccess"  /* I  the inquiry access code */
#define F_JDEV    "jDev"     /* LRemoteDevice; */
#define F_JUUID   "jUuid"    /* LUUID;  what the search is looking for */
#define F_JTRANS  "jTrans"   /* I  the transaction id searchServices returned */
#define F_JSTART  "jStart"   /* J  when it started, in ms */
#define F_JDUR    "jDur"     /* I  how long it may run, in ms */
#define F_JDONE   "jDone"    /* Z */

#define BT_MAX_DEVICES   16
#define BT_MAX_SERVICES   8
#define BT_URL_MAX      512
#define BT_ADDR_MAX      32

/* How long an inquiry waits for the port at a time.  A port that can block
 * holds this long; one that cannot returns 0 immediately and is asked again -
 * the job's own deadline is what ends it. */
#define BT_SLICE_MS 250u

/* JSR 82's own numbers, repeated here because the runtime never includes the
 * stub headers. */
#define BT_GIAC 0x9E8B33
#define BT_TRANSACTION_NONE 0

/* ------------------------------------------------------------------ */
/* Field access                                                        */
/* ------------------------------------------------------------------ */

static nj_ref bget_ref(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void bset_ref(nj_vm *vm, nj_ref o, const char *cls,
                     const char *name, const char *desc, nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) nj_field_write_ref(vm, o, f, v);
}

static nj_s4 bget_int(nj_vm *vm, nj_ref o, const char *cls,
                      const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void bset_int(nj_vm *vm, nj_ref o, const char *cls,
                     const char *name, const char *desc, nj_s4 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) nj_field_write_int(vm, o, f, v);
}

static nj_s8 bget_long(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_long(vm, o, f) : 0;
}

static void bset_long(nj_vm *vm, nj_ref o, const char *cls,
                      const char *name, const char *desc, nj_s8 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) nj_field_write_long(vm, o, f, v);
}

/* A Java String from C text, or NULL when the text is NULL. */
static nj_ref bstr(nj_vm *vm, const char *s)
{
    return s ? nj_string_new(vm, s) : NULL;
}

/* C text from a Java String.  Returns 0 when there is no string. */
/*
 * A String field as text.  nj_string_to_utf8() returns the number of bytes it
 * wrote and 0 when the reference is not a string - so "the conversion worked"
 * is not "the return value is 0", which is what a first version of this
 * helper believed, and every String field in the module read as empty.
 */
static int btext(nj_ref s, char *out, nj_size cap)
{
    if (!s || cap == 0 || s->kind != NJ_OBJ_STRING) {
        return 0;
    }
    (void)nj_string_to_utf8(s, out, cap);
    return 1;
}

/* The port, or NULL when this VM has none. */
static const nj_port *bt_port(nj_vm *vm)
{
    return nj_vm_port(vm);
}

static int bt_has_hooks(nj_vm *vm)
{
    const nj_port *p = bt_port(vm);
    return p != NULL && p->bt_local != NULL;
}

/* ------------------------------------------------------------------ */
/* UUID                                                                */
/* ------------------------------------------------------------------ */

static void uuid_set(nj_vm *vm, nj_ref u, nj_u8 hi, nj_u8 lo)
{
    bset_long(vm, u, NJ_C_UUID, F_UHI, "J", (nj_s8)hi);
    bset_long(vm, u, NJ_C_UUID, F_ULO, "J", (nj_s8)lo);
}

static void uuid_get(nj_vm *vm, nj_ref u, nj_u8 *hi, nj_u8 *lo)
{
    *hi = (nj_u8)bget_long(vm, u, NJ_C_UUID, F_UHI, "J");
    *lo = (nj_u8)bget_long(vm, u, NJ_C_UUID, F_ULO, "J");
}

/* The 128 bits as bytes, most significant first - the order SDP and every
 * port hook use. */
static void uuid_bytes(nj_vm *vm, nj_ref u, nj_u1 out[16])
{
    nj_u8 hi, lo;
    int i;
    uuid_get(vm, u, &hi, &lo);
    for (i = 0; i < 8; i++) {
        out[i]     = (nj_u1)(hi >> (56 - i * 8));
        out[8 + i] = (nj_u1)(lo >> (56 - i * 8));
    }
}

static nj_ref uuid_from_bytes(nj_vm *vm, const nj_u1 b[16])
{
    nj_u8 hi = 0, lo = 0;
    int i;
    for (i = 0; i < 8; i++) hi = (hi << 8) | b[i];
    for (i = 8; i < 16; i++) lo = (lo << 8) | b[i];
    {
        nj_ref u = nj_object_new(vm, NJ_C_UUID);
        if (!u) return NULL;
        uuid_set(vm, u, hi, lo);
        return u;
    }
}

/* "00112233445566778899aabbccddeeff" or "1101" - the short form is the
 * 16-bit UUID that every service class uses, and it lives in the low bits. */
static int uuid_parse(const char *s, nj_u8 *hi, nj_u8 *lo)
{
    nj_size n = 0, i;
    nj_u8 h = 0, l = 0;
    if (!s) return 0;
    while (s[n]) n++;
    if (n != 4 && n != 8 && n != 32) return 0;
    for (i = 0; i < n; i++) {
        int c = s[i], v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return 0;
        if (i & 1) { h = (h << 4) | (nj_u8)v; }
        else       { l = (l << 4) | (nj_u8)v; }
    }
    /* n/2 hex digits: keep them right-aligned in the 128-bit value. */
    switch (n) {
    case 4:  *hi = 0; *lo = h; break;
    case 8:  *hi = 0; *lo = (h << 16) | l; break;
    default: *hi = h; *lo = l; break;
    }
    return 1;
}

static void uuid_format(nj_vm *vm, nj_ref u, char *out, nj_size cap)
{
    static const char hex[] = "0123456789abcdef";
    nj_u1 b[16];
    int i, n = 0;
    if (cap < 33) { if (cap) out[0] = '\0'; return; }
    uuid_bytes(vm, u, b);
    /* The 16-bit and 32-bit forms are what a service class prints; a real
     * 128-bit UUID prints in full with dashes, as the specification says. */
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0) {
        if (b[4] == 0 && b[5] == 0) {
            for (i = 6; i < 16; i++) {
                out[n++] = hex[(b[i] >> 4) & 0xF];
                out[n++] = hex[b[i] & 0xF];
            }
            /* drop leading zeros of the short form */
            {
                int z = 0;
                while (z < n - 1 && out[z] == '0') z++;
                if (z) { memmove(out, out + z, (nj_size)(n - z)); n -= z; }
            }
            out[n] = '\0';
            return;
        }
    }
    for (i = 0; i < 16; i++) {
        out[n++] = hex[(b[i] >> 4) & 0xF];
        out[n++] = hex[b[i] & 0xF];
        if (i == 3 || i == 5 || i == 7 || i == 9) out[n++] = '-';
    }
    out[n] = '\0';
    (void)vm;
}

static int uuid_same(nj_vm *vm, nj_ref a, nj_ref b)
{
    nj_u8 ah, al, bh, bl;
    uuid_get(vm, a, &ah, &al);
    uuid_get(vm, b, &bh, &bl);
    return ah == bh && al == bl;
}

/* ------------------------------------------------------------------ */
/* BluetoothStateException: the runtime's "no radio" answer            */
/* ------------------------------------------------------------------ */

static nj_status bt_state(nj_vm *vm, const char *what)
{
    return nj_throw(vm, NJ_C_BTSTATE, what);
}

/* ------------------------------------------------------------------ */
/* The job registry, held on the VM like the media players             */
/* ------------------------------------------------------------------ */

static nj_ref bt_jobs(nj_vm *vm)
{
    return vm->bt_jobs;
}

static void bt_jobs_add(nj_vm *vm, nj_ref job)
{
    nj_value a[1];
    if (!vm->bt_jobs) {
        nj_ref v = nj_object_new(vm, NJ_C_VECTOR);
        if (!v) return;
        vm->bt_jobs = v;
    }
    if (!job) return;
    a[0].r = job;
    (void)nj_call(vm, vm->bt_jobs, NJ_C_VECTOR, "addElement",
                  "(Ljava/lang/Object;)V", a, NULL);
}

static nj_s4 bt_jobs_size(nj_vm *vm, nj_ref vec)
{
    nj_value ret;
    if (!vec) return 0;
    if (nj_call(vm, vec, NJ_C_VECTOR, "size", "()I", NULL, &ret) != NJ_OK)
        return 0;
    return ret.i;
}

static nj_ref bt_jobs_at(nj_vm *vm, nj_ref vec, nj_s4 i)
{
    nj_value a[1], ret;
    if (!vec) return NULL;
    a[0].i = i;
    ret.r = NULL;
    if (nj_call(vm, vec, NJ_C_VECTOR, "elementAt",
                "(I)Ljava/lang/Object;", a, &ret) != NJ_OK)
        return NULL;
    return ret.r;
}

static void bt_jobs_remove(nj_vm *vm, nj_ref vec, nj_s4 i)
{
    nj_value a[1];
    if (!vec) return;
    a[0].i = i;
    (void)nj_call(vm, vec, NJ_C_VECTOR, "removeElementAt", "(I)V", a, NULL);
}

/* ------------------------------------------------------------------ */
/* Local device and remote devices                                     */
/* ------------------------------------------------------------------ */

static nj_ref bt_remote_new(nj_vm *vm, const char *addr, const char *name,
                            nj_u4 cls)
{
    nj_ref d = nj_object_new(vm, NJ_C_REMOTE);
    if (!d) return NULL;
    bset_ref(vm, d, NJ_C_REMOTE, F_VADDR, "Ljava/lang/String;", bstr(vm, addr));
    bset_ref(vm, d, NJ_C_REMOTE, F_VNAME, "Ljava/lang/String;", bstr(vm, name));
    bset_int(vm, d, NJ_C_REMOTE, F_VCLS, "I", (nj_s4)cls);
    return d;
}

/* A ServiceRecord out of one port hook entry. */
static nj_ref bt_record_new(nj_vm *vm, const nj_bt_service *s)
{
    nj_ref r = nj_object_new(vm, NJ_C_SVCREC);
    nj_ref ru;
    if (!r) return NULL;
    ru = uuid_from_bytes(vm, s->uuid);
    bset_ref(vm, r, NJ_C_SVCREC, F_RUUID, "Ljavax/bluetooth/UUID;", ru);
    bset_ref(vm, r, NJ_C_SVCREC, F_RURL, "Ljava/lang/String;", bstr(vm, s->url));
    bset_ref(vm, r, NJ_C_SVCREC, F_RNAME, "Ljava/lang/String;", bstr(vm, s->name));
    bset_ref(vm, r, NJ_C_SVCREC, F_RHOST, "Ljavax/bluetooth/RemoteDevice;",
             bt_remote_new(vm, s->address, "", 0));
    return r;
}

/* ------------------------------------------------------------------ */
/* The frame pump: inquiry and service search, one slice per frame     */
/* ------------------------------------------------------------------ */

/*
 * The two completion callbacks do not have the same shape:
 *
 *     inquiryCompleted(int discType)
 *     serviceSearchCompleted(int transID, int respCode)
 *
 * so the helper reads the transaction id from the job for the second one.
 * Passing "(I)V" for both is what a first version did, and the VM answered
 * NoSuchMethodError on the interface - which is how this comment got here.
 */
static void bt_call_completed(nj_vm *vm, nj_ref listener, nj_ref job,
                              const char *what, int code)
{
    nj_value cbargs[2];

    if (!listener) return;
    cbargs[0].i = code;
    if (strcmp(what, "serviceSearchCompleted") == 0) {
        cbargs[0].i = bget_int(vm, job, NJ_C_JOB, F_JTRANS, "I");
        cbargs[1].i = code;
        (void)nj_call(vm, listener, NJ_C_LISTENER, what, "(II)V", cbargs, NULL);
        return;
    }
    (void)nj_call(vm, listener, NJ_C_LISTENER, what, "(I)V", cbargs, NULL);
}

static void bt_run_inquiry(nj_vm *vm, nj_ref job)
{
    const nj_port *p = bt_port(vm);
    nj_bt_device devs[BT_MAX_DEVICES];
    nj_ref listener = bget_ref(vm, job, NJ_C_JOB, F_JLISTEN,
                               "Ljavax/bluetooth/DiscoveryListener;");
    nj_u4 started = (nj_u4)bget_long(vm, job, NJ_C_JOB, F_JSTART, "J");
    nj_u4 dur = (nj_u4)bget_int(vm, job, NJ_C_JOB, F_JDUR, "I");
    nj_u4 now = nj_now_ms(vm);
    nj_u4 left;
    int n, i;

    if (bget_int(vm, job, NJ_C_JOB, F_JDONE, "Z")) return;
    if (now - started >= dur) {
        bt_call_completed(vm, listener, job, "inquiryCompleted", 0 /* INQUIRY_COMPLETED */);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    if (!p || !p->bt_inquiry) {
        bt_call_completed(vm, listener, job, "inquiryCompleted", 2 /* INQUIRY_ERROR */);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    left = dur - (now - started);
    if (left > BT_SLICE_MS) left = BT_SLICE_MS;
    n = p->bt_inquiry(p->ud, devs, BT_MAX_DEVICES, left);
    if (n < 0) {
        bt_call_completed(vm, listener, job, "inquiryCompleted", 2 /* INQUIRY_ERROR */);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    /*
     * Only the devices the listener has not heard about yet.  A port describes
     * its radio as a snapshot - the same peer comes back on the next call - so
     * without this the MIDlet would see the same device once per frame.  The
     * order is the port's, and a device that stops being reported is simply
     * never mentioned again, which is what the specification promises.
     */
    {
        nj_s4 fired = bget_int(vm, job, NJ_C_JOB, F_JFIRED, "I");
        if (fired > n) fired = 0;          /* the port changed its mind */
        for (i = fired; i < n; i++) {
        nj_value a[2];
        nj_ref d = bt_remote_new(vm, devs[i].address, devs[i].name,
                                 devs[i].device_class);
        nj_ref dc = nj_object_new(vm, NJ_C_DEVCLASS);
        if (!d) continue;
        if (dc) bset_int(vm, dc, NJ_C_DEVCLASS, F_DVAL, "I",
                         (nj_s4)devs[i].device_class);
        a[0].r = d;
        a[1].r = dc;
        (void)nj_call(vm, listener, NJ_C_LISTENER, "deviceDiscovered",
                      "(Ljavax/bluetooth/RemoteDevice;Ljavax/bluetooth/DeviceClass;)V",
                      a, NULL);
        }
        if (n > fired) bset_int(vm, job, NJ_C_JOB, F_JFIRED, "I", n);
    }
}

static void bt_run_search(nj_vm *vm, nj_ref job)
{
    const nj_port *p = bt_port(vm);
    nj_bt_service svcs[BT_MAX_SERVICES];
    nj_ref listener = bget_ref(vm, job, NJ_C_JOB, F_JLISTEN,
                               "Ljavax/bluetooth/DiscoveryListener;");
    nj_ref dev = bget_ref(vm, job, NJ_C_JOB, F_JDEV, "Ljavax/bluetooth/RemoteDevice;");
    nj_s4 trans = bget_int(vm, job, NJ_C_JOB, F_JTRANS, "I");
    char addr[BT_ADDR_MAX];
    nj_u1 uuid[16];
    nj_ref ru;
    int n, i;

    if (bget_int(vm, job, NJ_C_JOB, F_JDONE, "Z")) return;
    if (!btext(bget_ref(vm, dev, NJ_C_REMOTE, F_VADDR, "Ljava/lang/String;"),
               addr, sizeof addr)) {
        bt_call_completed(vm, listener, job, "serviceSearchCompleted", 3);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    if (!p || !p->bt_services) {
        bt_call_completed(vm, listener, job, "serviceSearchCompleted", 3);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    ru = bget_ref(vm, job, NJ_C_JOB, F_JUUID, "Ljavax/bluetooth/UUID;");
    if (ru) uuid_bytes(vm, ru, uuid);
    else memset(uuid, 0, sizeof uuid);
    n = p->bt_services(p->ud, addr, ru ? uuid : NULL, svcs, BT_MAX_SERVICES);
    if (n < 0) {
        bt_call_completed(vm, listener, job, "serviceSearchCompleted", 3);
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        return;
    }
    {
        nj_ref arr = nj_array_new(vm, NJ_C_SVCREC, (nj_u4)n);
        nj_value a[2];
        if (!arr) {
            bt_call_completed(vm, listener, job, "serviceSearchCompleted", 3);
            bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
            return;
        }
        for (i = 0; i < n; i++) {
            nj_ref rec = bt_record_new(vm, &svcs[i]);
            if (rec) nj_array_set_ref(arr, (nj_u4)i, rec);
        }
        a[0].i = trans;
        a[1].r = arr;
        (void)nj_call(vm, listener, NJ_C_LISTENER, "servicesDiscovered",
                      "(I[Ljavax/bluetooth/ServiceRecord;)V", a, NULL);
    }
    bt_call_completed(vm, listener, job, "serviceSearchCompleted", 1);
    bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
}

void nj_bt_frame(nj_vm *vm)
{
    nj_ref vec = bt_jobs(vm);
    nj_s4 i;
    if (!vec) return;
    for (i = bt_jobs_size(vm, vec) - 1; i >= 0; i--) {
        nj_ref job = bt_jobs_at(vm, vec, i);
        if (!job) { bt_jobs_remove(vm, vec, i); continue; }
        if (bget_int(vm, job, NJ_C_JOB, F_JKIND, "I") == 1)
            bt_run_inquiry(vm, job);
        else
            bt_run_search(vm, job);
        if (bget_int(vm, job, NJ_C_JOB, F_JDONE, "Z"))
            bt_jobs_remove(vm, vec, i);
    }
}

void nj_bt_shutdown(nj_vm *vm)
{
    nj_ref vec = bt_jobs(vm);
    nj_s4 i;
    if (!vec) return;
    for (i = bt_jobs_size(vm, vec) - 1; i >= 0; i--) {
        nj_ref job = bt_jobs_at(vm, vec, i);
        if (!job) { bt_jobs_remove(vm, vec, i); continue; }
        /* The port's radio is not the runtime's to keep running: anything the
         * MIDlet left open is closed by the port's own shutdown(), and the
         * listeners simply never hear back. */
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        bt_jobs_remove(vm, vec, i);
    }
}

/* ------------------------------------------------------------------ */
/* UUID                                                                */
/* ------------------------------------------------------------------ */

NJFN(uuid_init_str)
{
    char txt[64];
    nj_u8 hi, lo;
    nj_ref self = NJ_THIS(args);
    if (!btext(NJ_ARG_REF(args, 0), txt, sizeof txt))
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!uuid_parse(txt, &hi, &lo))
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "not a UUID: it must be 4, 8 or 32 hex digits");
    uuid_set(vm, self, hi, lo);
    return NJ_OK;
}

NJFN(uuid_init_long)
{
    uuid_set(vm, NJ_THIS(args), 0, (nj_u8)NJ_ARG_LONG(args, 0));
    return NJ_OK;
}

NJFN(uuid_equals)
{
    nj_ref o = NJ_ARG_REF(args, 1);
    if (!o) { ret->i = 0; return NJ_OK; }
    ret->i = uuid_same(vm, NJ_THIS(args), o) ? 1 : 0;
    return NJ_OK;
}

NJFN(uuid_hash)
{
    nj_u8 hi, lo;
    uuid_get(vm, NJ_THIS(args), &hi, &lo);
    ret->i = (nj_s4)((hi ^ (lo >> 32)) & 0x7FFFFFFF);
    return NJ_OK;
}

NJFN(uuid_to_string)
{
    char txt[64];
    uuid_format(vm, NJ_THIS(args), txt, sizeof txt);
    ret->r = bstr(vm, txt);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* DeviceClass                                                         */
/* ------------------------------------------------------------------ */

NJFN(devclass_init)
{
    bset_int(vm, NJ_THIS(args), NJ_C_DEVCLASS, F_DVAL, "I", NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(devclass_major)
{
    ret->i = (bget_int(vm, NJ_THIS(args), NJ_C_DEVCLASS, F_DVAL, "I") >> 8) & 0x1F;
    return NJ_OK;
}

NJFN(devclass_minor)
{
    /* The minor field is 6 bits for every major class this runtime meets, with
     * the two bits above it carrying the "limited discoverable" flags. */
    ret->i = (bget_int(vm, NJ_THIS(args), NJ_C_DEVCLASS, F_DVAL, "I") >> 2) & 0x3F;
    return NJ_OK;
}

NJFN(devclass_services)
{
    ret->i = bget_int(vm, NJ_THIS(args), NJ_C_DEVCLASS, F_DVAL, "I") & 0xFFE000;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* BluetoothStateException                                             */
/* ------------------------------------------------------------------ */

NJFN(btstate_init)
{
    (void)nj_call_special(vm, NJ_THIS(args), "java/lang/Exception", "<init>",
                          "()V", NULL, NULL);
    return NJ_OK;
}

NJFN(btstate_init_str)
{
    nj_value a[1];
    a[0].r = NJ_ARG_REF(args, 0);
    (void)nj_call_special(vm, NJ_THIS(args), "java/lang/Exception", "<init>",
                          "(Ljava/lang/String;)V", a, NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* DataElement                                                         */
/* ------------------------------------------------------------------ */

#define DE_NULL    0x00
#define DE_U_INT_1 0x08
#define DE_U_INT_2 0x09
#define DE_U_INT_4 0x0A
#define DE_U_INT_8 0x0B
#define DE_U_INT_16 0x0C
#define DE_INT_1   0x10
#define DE_INT_2   0x11
#define DE_INT_4   0x12
#define DE_INT_8   0x13
#define DE_INT_16  0x14
#define DE_UUID    0x18
#define DE_STRING  0x20
#define DE_BOOL    0x28
#define DE_DATSEQ  0x30
#define DE_DATALT  0x38
#define DE_URL     0x40

NJFN(delem_init_type)
{
    nj_s4 t = NJ_ARG_INT(args, 0);
    if (t != DE_NULL && t != DE_DATSEQ && t != DE_DATALT)
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "this constructor takes NULL, DATSEQ or DATALT");
    bset_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I", t);
    if (t == DE_DATSEQ || t == DE_DATALT) {
        nj_ref v = nj_object_new(vm, NJ_C_VECTOR);
        bset_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_ESEQ, "Ljava/util/Vector;", v);
    }
    return NJ_OK;
}

NJFN(delem_init_bool)
{
    bset_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I", DE_BOOL);
    bset_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J",
              NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

/* The integer forms, with the range check the specification asks for - a
 * wrong width is a programming error, not something to truncate. */
NJFN(delem_init_int)
{
    nj_s4 t = NJ_ARG_INT(args, 0);
    nj_s8 v = NJ_ARG_LONG(args, 1);
    int ok = 1;
    switch (t) {
    case DE_U_INT_1: ok = (v >= 0 && v <= 0xFF); break;
    case DE_U_INT_2: ok = (v >= 0 && v <= 0xFFFF); break;
    case DE_U_INT_4: ok = (v >= 0 && v <= 0xFFFFFFFFLL); break;
    case DE_INT_1:   ok = (v >= -0x80 && v <= 0x7F); break;
    case DE_INT_2:   ok = (v >= -0x8000 && v <= 0x7FFF); break;
    case DE_INT_4:   ok = (v >= -0x80000000LL && v <= 0x7FFFFFFFLL); break;
    case DE_INT_8:   ok = 1; break;
    default:         ok = 0; break;
    }
    if (!ok)
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the value does not fit the data type");
    bset_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I", t);
    bset_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J", v);
    return NJ_OK;
}

NJFN(delem_init_obj)
{
    nj_s4 t = NJ_ARG_INT(args, 0);
    nj_ref o = NJ_ARG_REF(args, 1);
    if (!o)
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    switch (t) {
    case DE_STRING:
    case DE_URL:
    case DE_UUID:
    case DE_U_INT_8:
    case DE_U_INT_16:
    case DE_INT_16:
        break;
    default:
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "this type takes no object");
    }
    bset_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I", t);
    bset_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_EOBJ, "Ljava/lang/Object;", o);
    return NJ_OK;
}

NJFN(delem_get_type)
{
    ret->i = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    return NJ_OK;
}

NJFN(delem_get_long)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    switch (t) {
    case DE_U_INT_1: case DE_U_INT_2: case DE_U_INT_4:
    case DE_INT_1: case DE_INT_2: case DE_INT_4: case DE_INT_8:
        ret->j = bget_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J");
        return NJ_OK;
    default:
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    }
}

NJFN(delem_get_boolean)
{
    if (bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I") != DE_BOOL)
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    ret->i = bget_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J") ? 1 : 0;
    return NJ_OK;
}

NJFN(delem_get_value)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    switch (t) {
    case DE_STRING:
    case DE_URL:
        ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_EOBJ,
                          "Ljava/lang/Object;");
        return NJ_OK;
    case DE_UUID:
        ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_EOBJ,
                          "Ljava/lang/Object;");
        return NJ_OK;
    case DE_U_INT_1: case DE_U_INT_2: case DE_U_INT_4:
    case DE_INT_1: case DE_INT_2: case DE_INT_4: case DE_INT_8:
        /* A number comes back as a Long: the reference implementation does the
         * same, and a MIDlet that wants an int calls intValue(). */
        {
            nj_value a[2], out;
            a[0].j = bget_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J");
            out.r = NULL;
            if (nj_call_static(vm, "java/lang/Long", "valueOf",
                               "(J)Ljava/lang/Long;", a, &out) != NJ_OK)
                return NJ_ERR;
            ret->r = out.r;
        }
        return NJ_OK;
    case DE_BOOL:
        {
            nj_value a[1], out;
            a[0].i = bget_long(vm, NJ_THIS(args), NJ_C_DELEM, F_ENUM, "J") ? 1 : 0;
            out.r = NULL;
            if (nj_call_static(vm, "java/lang/Boolean", "valueOf",
                               "(Z)Ljava/lang/Boolean;", a, &out) != NJ_OK)
                return NJ_ERR;
            ret->r = out.r;
        }
        return NJ_OK;
    default:
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    }
}

NJFN(delem_get_size)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    nj_ref seq;
    nj_value retv;
    if (t != DE_DATSEQ && t != DE_DATALT)
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    seq = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_ESEQ, "Ljava/util/Vector;");
    if (!seq) { ret->i = 0; return NJ_OK; }
    if (nj_call(vm, seq, NJ_C_VECTOR, "size", "()I", NULL, &retv) != NJ_OK)
        return NJ_ERR;
    ret->i = retv.i;
    return NJ_OK;
}

NJFN(delem_add)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    nj_ref seq;
    nj_value a[1];
    if (t != DE_DATSEQ && t != DE_DATALT)
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    seq = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_ESEQ, "Ljava/util/Vector;");
    if (!seq || !NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    a[0].r = NJ_ARG_REF(args, 0);
    (void)nj_call(vm, seq, NJ_C_VECTOR, "addElement", "(Ljava/lang/Object;)V",
                  a, NULL);
    return NJ_OK;
}

NJFN(delem_insert)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    nj_ref seq;
    nj_value a[2];
    if (t != DE_DATSEQ && t != DE_DATALT)
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    seq = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_ESEQ, "Ljava/util/Vector;");
    if (!seq || !NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    a[0].r = NJ_ARG_REF(args, 0);
    a[1].i = NJ_ARG_INT(args, 1);
    (void)nj_call(vm, seq, NJ_C_VECTOR, "insertElementAt",
                  "(Ljava/lang/Object;I)V", a, NULL);
    return NJ_OK;
}

NJFN(delem_remove)
{
    nj_s4 t = bget_int(vm, NJ_THIS(args), NJ_C_DELEM, F_ETYPE, "I");
    nj_ref seq;
    nj_value a[1], retv;
    if (t != DE_DATSEQ && t != DE_DATALT)
        return nj_throw(vm, "java/lang/ClassCastException", NULL);
    seq = bget_ref(vm, NJ_THIS(args), NJ_C_DELEM, F_ESEQ, "Ljava/util/Vector;");
    if (!seq || !NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    a[0].r = NJ_ARG_REF(args, 0);
    if (nj_call(vm, seq, NJ_C_VECTOR, "removeElement",
                "(Ljava/lang/Object;)Z", a, &retv) != NJ_OK)
        return NJ_ERR;
    ret->i = retv.i;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* ServiceRecord                                                       */
/* ------------------------------------------------------------------ */

NJFN(svcrec_get_url)
{
    (void)NJ_ARG_INT(args, 0);      /* the security mode: no stack to honour it */
    (void)NJ_ARG_INT(args, 1);
    ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RURL,
                      "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(svcrec_get_attr)
{
    nj_ref ids = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I");
    nj_ref vals = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                           "[Ljava/lang/Object;");
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_u4 n, i;
    ret->r = NULL;
    if (!ids || !vals) return NJ_OK;
    n = nj_array_length(ids);
    for (i = 0; i < n; i++) {
        if (nj_array_get_int(ids, i) == want) {
            ret->r = nj_array_get_ref(vals, i);
            return NJ_OK;
        }
    }
    return NJ_OK;
}

NJFN(svcrec_set_attr)
{
    nj_ref ids = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I");
    nj_ref vals = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                           "[Ljava/lang/Object;");
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_ref val = NJ_ARG_REF(args, 1);
    nj_u4 n, i;
    if (!val)
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!ids || !vals) {
        ids = nj_array_new(vm, "I", 8);
        vals = nj_array_new(vm, "Ljava/lang/Object;", 8);
        bset_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I", ids);
        bset_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                 "[Ljava/lang/Object;", vals);
    }
    n = nj_array_length(ids);
    for (i = 0; i < n; i++) {
        if (nj_array_get_int(ids, i) == want) {
            nj_array_set_ref(vals, i, val);
            return NJ_OK;
        }
        if (nj_array_get_ref(vals, i) == NULL) break;
    }
    if (i >= n) {
        /* Grow both arrays together: attribute ids are few (a service record
         * carries a handful), so doubling from eight is plenty. */
        nj_u4 bigger = n ? n * 2 : 8;
        nj_ref nids = nj_array_new(vm, "I", bigger);
        nj_ref nvals = nj_array_new(vm, "Ljava/lang/Object;", bigger);
        nj_u4 k;
        if (!nids || !nvals) return nj_throw_oom(vm);
        for (k = 0; k < n; k++) {
            nj_array_set_int(nids, k, nj_array_get_int(ids, k));
            nj_array_set_ref(nvals, k, nj_array_get_ref(vals, k));
        }
        bset_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I", nids);
        bset_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                 "[Ljava/lang/Object;", nvals);
        ids = nids; vals = nvals; i = n;
    }
    nj_array_set_int(ids, i, want);
    nj_array_set_ref(vals, i, val);
    return NJ_OK;
}

NJFN(svcrec_remove_attr)
{
    nj_ref ids = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I");
    nj_ref vals = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                           "[Ljava/lang/Object;");
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_u4 n, i;
    if (!ids || !vals) return NJ_OK;
    n = nj_array_length(ids);
    for (i = 0; i < n; i++) {
        if (nj_array_get_int(ids, i) == want) {
            nj_array_set_ref(vals, i, NULL);
            return NJ_OK;
        }
    }
    return NJ_OK;
}

NJFN(svcrec_attr_ids)
{
    nj_ref ids = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RIDS, "[I");
    nj_ref vals = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RVALS,
                           "[Ljava/lang/Object;");
    nj_u4 n, i, k = 0;
    nj_ref out;
    if (!ids || !vals) {
        ret->r = nj_array_new(vm, "I", 0);
        return NJ_OK;
    }
    n = nj_array_length(ids);
    for (i = 0; i < n; i++)
        if (nj_array_get_ref(vals, i) != NULL) k++;
    out = nj_array_new(vm, "I", k);
    if (!out) return nj_throw_oom(vm);
    k = 0;
    for (i = 0; i < n; i++)
        if (nj_array_get_ref(vals, i) != NULL)
            nj_array_set_int(out, k++, nj_array_get_int(ids, i));
    ret->r = out;
    return NJ_OK;
}

NJFN(svcrec_host)
{
    ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_SVCREC, F_RHOST,
                      "Ljavax/bluetooth/RemoteDevice;");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* RemoteDevice                                                        */
/* ------------------------------------------------------------------ */

NJFN(remote_address)
{
    ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_REMOTE, F_VADDR,
                      "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(remote_name)
{
    ret->r = bget_ref(vm, NJ_THIS(args), NJ_C_REMOTE, F_VNAME,
                      "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(remote_class)
{
    nj_ref d = nj_object_new(vm, NJ_C_DEVCLASS);
    if (!d) return nj_throw_oom(vm);
    bset_int(vm, d, NJ_C_DEVCLASS, F_DVAL, "I",
             bget_int(vm, NJ_THIS(args), NJ_C_REMOTE, F_VCLS, "I"));
    ret->r = d;
    return NJ_OK;
}

NJFN(remote_equals)
{
    nj_ref a = bget_ref(vm, NJ_THIS(args), NJ_C_REMOTE, F_VADDR,
                        "Ljava/lang/String;");
    nj_ref b = NJ_ARG_REF(args, 1)
             ? bget_ref(vm, NJ_ARG_REF(args, 1), NJ_C_REMOTE, F_VADDR,
                        "Ljava/lang/String;") : NULL;
    char ta[BT_ADDR_MAX], tb[BT_ADDR_MAX];
    if (!b) { ret->i = 0; return NJ_OK; }
    btext(a, ta, sizeof ta);
    btext(b, tb, sizeof tb);
    ret->i = strcmp(ta, tb) == 0 ? 1 : 0;
    return NJ_OK;
}

NJFN(remote_hash)
{
    char t[BT_ADDR_MAX];
    nj_u4 h = 0;
    nj_size i;
    btext(bget_ref(vm, NJ_THIS(args), NJ_C_REMOTE, F_VADDR,
                   "Ljava/lang/String;"), t, sizeof t);
    for (i = 0; t[i]; i++) h = h * 31u + (nj_u1)t[i];
    ret->i = (nj_s4)(h & 0x7FFFFFFF);
    return NJ_OK;
}

NJFN(remote_get_remote_device)
{
    char addr[BT_ADDR_MAX];
    nj_size i;
    if (!btext(NJ_ARG_REF(args, 0), addr, sizeof addr))
        return nj_throw(vm, "java/lang/IllegalArgumentException", "no address");
    /* J2ME writes the address as twelve hex digits, but a handset that was
     * told "00:11:22:33:44:55" should still work, so the colons come out. */
    {
        char clean[BT_ADDR_MAX];
        nj_size n = 0;
        for (i = 0; addr[i] && n + 1 < sizeof clean; i++)
            if (addr[i] != ':' && addr[i] != '-') clean[n++] = addr[i];
        clean[n] = '\0';
        if (n != 12)
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "a Bluetooth address is twelve hex digits");
        for (i = 0; i < n; i++) {
            int c = clean[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F')))
                return nj_throw(vm, "java/lang/IllegalArgumentException",
                                "a Bluetooth address is twelve hex digits");
        }
        ret->r = bt_remote_new(vm, clean, "", 0);
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* LocalDevice                                                         */
/* ------------------------------------------------------------------ */

static int bt_local_fill(nj_vm *vm, nj_bt_local *l)
{
    const nj_port *p = bt_port(vm);
    if (!p || !p->bt_local) return 0;
    memset(l, 0, sizeof *l);
    return p->bt_local(p->ud, l) == NJ_OK;
}

NJFN(local_get_local_device)
{
    nj_bt_local l;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    ret->r = nj_object_new(vm, NJ_C_LOCAL);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(local_friendly_name)
{
    nj_bt_local l;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    ret->r = bstr(vm, l.name);
    return NJ_OK;
}

NJFN(local_address)
{
    nj_bt_local l;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    ret->r = bstr(vm, l.address);
    return NJ_OK;
}

NJFN(local_device_class)
{
    nj_bt_local l;
    nj_ref d;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    d = nj_object_new(vm, NJ_C_DEVCLASS);
    if (!d) return nj_throw_oom(vm);
    bset_int(vm, d, NJ_C_DEVCLASS, F_DVAL, "I", (nj_s4)l.device_class);
    ret->r = d;
    return NJ_OK;
}

NJFN(local_get_agent)
{
    nj_bt_local l;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    ret->r = nj_object_new(vm, NJ_C_AGENT);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

NJFN(local_set_discoverable)
{
    const nj_port *p = bt_port(vm);
    nj_bt_local l;
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    if (!p->bt_discoverable) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = p->bt_discoverable(p->ud, NJ_ARG_INT(args, 0), 0) > 0 ? 1 : 0;
    return NJ_OK;
}

NJFN(local_get_property)
{
    nj_bt_local l;
    char key[32];
    if (!bt_local_fill(vm, &l))
        return bt_state(vm, "this device has no Bluetooth radio");
    if (!btext(NJ_ARG_REF(args, 0), key, sizeof key)) {
        ret->r = NULL;
        return NJ_OK;
    }
    /* The three properties the specification defines that have an answer here.
     * Everything else is honestly unknown, which is what NULL means. */
    if (strcmp(key, "bluetooth.sd.attr.retrievable.max") == 0)
        ret->r = bstr(vm, "64");
    else if (strcmp(key, "bluetooth.connected.devices.max") == 0)
        ret->r = bstr(vm, "7");
    else if (strcmp(key, "bluetooth.sd.trans.max") == 0)
        ret->r = bstr(vm, "1");
    else
        ret->r = NULL;
    return NJ_OK;
}

NJFN(local_get_record)
{
    /* A server registers itself by opening a URL, so the record a MIDlet
     * builds here has no stack to go to; it is remembered on the record and
     * that is all the specification's getRecord() promise can mean without an
     * SDP server of the runtime's own. */
    ret->r = NJ_ARG_REF(args, 0);
    return ret->r ? NJ_OK : nj_throw(vm, "java/lang/NullPointerException", NULL);
}

NJFN(local_update_record)
{
    if (!NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* DiscoveryAgent                                                      */
/* ------------------------------------------------------------------ */

NJFN(agent_start_inquiry)
{
    nj_ref job;
    nj_ref listener = NJ_ARG_REF(args, 1);
    if (!listener)
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!bt_has_hooks(vm))
        return bt_state(vm, "this device has no Bluetooth radio");
    job = nj_object_new(vm, NJ_C_JOB);
    if (!job) return nj_throw_oom(vm);
    bset_int(vm, job, NJ_C_JOB, F_JKIND, "I", 1);
    bset_ref(vm, job, NJ_C_JOB, F_JLISTEN, "Ljavax/bluetooth/DiscoveryListener;", listener);
    bset_int(vm, job, NJ_C_JOB, F_JACCESS, "I", NJ_ARG_INT(args, 0));
    bset_long(vm, job, NJ_C_JOB, F_JSTART, "J", (nj_s8)nj_now_ms(vm));
    {
        nj_bt_local l;
        int ms = 12000;                 /* what a real adapter needs */
        if (bt_local_fill(vm, &l) && l.inquiry_ms > 0) {
            ms = l.inquiry_ms;          /* the radio knows better */
        }
        bset_int(vm, job, NJ_C_JOB, F_JDUR, "I", ms);
    }
    bt_jobs_add(vm, job);
    ret->i = 1;
    return NJ_OK;
}

static nj_ref bt_find_job(nj_vm *vm, nj_ref listener, int kind)
{
    nj_ref vec = bt_jobs(vm);
    nj_s4 i, n = bt_jobs_size(vm, vec);
    for (i = 0; i < n; i++) {
        nj_ref job = bt_jobs_at(vm, vec, i);
        if (!job) continue;
        if (bget_int(vm, job, NJ_C_JOB, F_JDONE, "Z")) continue;
        if (kind && bget_int(vm, job, NJ_C_JOB, F_JKIND, "I") != kind) continue;
        if (listener &&
            bget_ref(vm, job, NJ_C_JOB, F_JLISTEN, "Ljavax/bluetooth/DiscoveryListener;") != listener)
            continue;
        return job;
    }
    return NULL;
}

NJFN(agent_cancel_inquiry)
{
    nj_ref job = bt_find_job(vm, NJ_ARG_REF(args, 0), 1);
    if (!job) { ret->i = 0; return NJ_OK; }
    bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
    bt_call_completed(vm, NJ_ARG_REF(args, 0), job, "inquiryCompleted",
                      1 /* INQUIRY_TERMINATED */);
    ret->i = 1;
    return NJ_OK;
}

NJFN(agent_search_services)
{
    nj_ref uuids = NJ_ARG_REF(args, 1);
    nj_ref dev = NJ_ARG_REF(args, 2);
    nj_ref listener = NJ_ARG_REF(args, 3);
    nj_ref job;
    nj_s4 trans;
    nj_u4 nu = uuids ? nj_array_length(uuids) : 0;
    if (!dev || !listener)
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!bt_has_hooks(vm))
        return bt_state(vm, "this device has no Bluetooth radio");
    (void)nu;
    job = nj_object_new(vm, NJ_C_JOB);
    if (!job) return nj_throw_oom(vm);
    bset_int(vm, job, NJ_C_JOB, F_JKIND, "I", 2);
    bset_ref(vm, job, NJ_C_JOB, F_JLISTEN, "Ljavax/bluetooth/DiscoveryListener;", listener);
    bset_ref(vm, job, NJ_C_JOB, F_JDEV, "Ljavax/bluetooth/RemoteDevice;", dev);
    if (nu > 0) {
        /* The first UUID is the one searched for: a service matches when it
         * carries that one, which is how every title in the corpus asks. */
        bset_ref(vm, job, NJ_C_JOB, F_JUUID, "Ljavax/bluetooth/UUID;", nj_array_get_ref(uuids, 0));
    }
    /*
     * The transaction id is a token the MIDlet compares in
     * servicesDiscovered() and serviceSearchCompleted(), so it only has to be
     * distinct from the other searches that are still running.  Numbering the
     * jobs the VM already holds does that without a file-scope counter, which
     * this runtime does not allow (tools/check_no_statics.sh).
     */
    trans = (nj_s4)bt_jobs_size(vm, bt_jobs(vm)) + 1;
    bset_int(vm, job, NJ_C_JOB, F_JTRANS, "I", trans);
    ret->i = trans;
    bt_jobs_add(vm, job);
    return NJ_OK;
}

NJFN(agent_cancel_search)
{
    nj_ref vec = bt_jobs(vm);
    nj_s4 i, n = bt_jobs_size(vm, vec);
    nj_s4 trans = NJ_ARG_INT(args, 0);
    for (i = 0; i < n; i++) {
        nj_ref job = bt_jobs_at(vm, vec, i);
        if (!job) continue;
        if (bget_int(vm, job, NJ_C_JOB, F_JTRANS, "I") != trans) continue;
        bset_int(vm, job, NJ_C_JOB, F_JDONE, "Z", 1);
        bt_call_completed(vm,
            bget_ref(vm, job, NJ_C_JOB, F_JLISTEN, "Ljavax/bluetooth/DiscoveryListener;"),
            job, "serviceSearchCompleted", 2 /* SERVICE_SEARCH_TERMINATED */);
        ret->i = 1;
        return NJ_OK;
    }
    ret->i = 0;
    return NJ_OK;
}

NJFN(agent_retrieve_devices)
{
    nj_ref arr;
    const nj_port *p = bt_port(vm);
    nj_bt_device devs[BT_MAX_DEVICES];
    int n = 0, i;
    if (p && p->bt_inquiry)
        n = p->bt_inquiry(p->ud, devs, BT_MAX_DEVICES, 0);
    if (n < 0) n = 0;
    arr = nj_array_new(vm, NJ_C_REMOTE, (nj_u4)n);
    if (!arr) return nj_throw_oom(vm);
    for (i = 0; i < n; i++) {
        nj_ref d = bt_remote_new(vm, devs[i].address, devs[i].name,
                                 devs[i].device_class);
        if (d) nj_array_set_ref(arr, (nj_u4)i, d);
    }
    ret->r = arr;
    return NJ_OK;
}

NJFN(agent_select_service)
{
    /* Without a discovery of its own there is nothing to prefer: the
     * specification lets this answer NULL, and a MIDlet then searches. */
    ret->r = NULL;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The connection objects                                              */
/* ------------------------------------------------------------------ */

static nj_status bt_io_fail(nj_vm *vm, const char *what)
{
    return nj_throw(vm, "java/io/IOException", what);
}

static int bt_x_closed(nj_vm *vm, const char *cls, nj_ref conn)
{
    return bget_int(vm, conn, cls, F_BCLOSED, "Z") != 0;
}

static int bt_closed(nj_vm *vm, nj_ref conn)
{
    return bt_x_closed(vm, NJ_C_BTSTREAM, conn);
}

static nj_ref bt_stream_new(nj_vm *vm, void *handle, const char *url,
                            const char *addr)
{
    nj_ref c = nj_object_new(vm, NJ_C_BTSTREAM);
    if (!c) return NULL;
    bset_long(vm, c, NJ_C_BTSTREAM, F_BHANDLE, "J", (nj_s8)(intptr_t)handle);
    bset_ref(vm, c, NJ_C_BTSTREAM, F_BURL, "Ljava/lang/String;", bstr(vm, url));
    bset_ref(vm, c, NJ_C_BTSTREAM, F_BADDR, "Ljava/lang/String;", bstr(vm, addr));
    return c;
}

/* A stream object belongs to the connection it came from; the socket module
 * declares the same two fields for the same reason. */
static nj_ref bt_stream_wrap(nj_vm *vm, const char *cls, nj_ref conn)
{
    nj_ref s = nj_object_new(vm, cls);
    if (!s) return NULL;
    bset_ref(vm, s, cls, F_SCONN, "Ljavax/bluetooth/BtStream;", conn);
    return s;
}

static nj_s8 bt_x_handle(nj_vm *vm, const char *cls, nj_ref conn)
{
    return bget_long(vm, conn, cls, F_BHANDLE, "J");
}

static const nj_port *bt_port_of(nj_vm *vm, nj_ref conn)
{
    (void)conn;
    return nj_vm_port(vm);
}

/* One read, however the MIDlet spelled it.  Returns the number of bytes, -1 at
 * end of stream, or -2 when an exception has already been raised. */
static int bt_x_read_once(nj_vm *vm, const char *cls, nj_ref conn, nj_u1 *buf,
                          int len, nj_u4 budget_ms, int *eof)
{
    const nj_port *p = bt_port_of(vm, conn);
    nj_u4 waited = 0;
    *eof = 0;
    if (bt_x_closed(vm, cls, conn) || bt_x_handle(vm, cls, conn) == 0)
        return -2;
    if (!p || !p->bt_read)
        return -2;
    /* A port that cannot wait answers 0 immediately; the runtime keeps asking
     * until the caller's own patience - the same budget - runs out. */
    for (;;) {
        int n = p->bt_read(p->ud, (void *)(intptr_t)bt_x_handle(vm, cls, conn),
                           buf, len, budget_ms > 250 ? 250 : budget_ms);
        if (n > 0) return n;
        if (n < 0) { *eof = 1; return -1; }
        if (waited >= budget_ms) return 0;
        if (p->sleep_ms) p->sleep_ms(p->ud, 10);
        else if (p->idle) p->idle(p->ud);
        waited += 10;
    }
}

static int bt_x_write_all(nj_vm *vm, const char *cls, nj_ref conn,
                          const nj_u1 *buf, int len)
{
    const nj_port *p = bt_port_of(vm, conn);
    if (bt_x_closed(vm, cls, conn) || bt_x_handle(vm, cls, conn) == 0) return -1;
    if (!p || !p->bt_write) return -1;
    return p->bt_write(p->ud, (void *)(intptr_t)bt_x_handle(vm, cls, conn),
                       buf, len);
}

static int bt_read_once(nj_vm *vm, nj_ref conn, nj_u1 *buf, int len,
                        nj_u4 budget_ms, int *eof)
{
    return bt_x_read_once(vm, NJ_C_BTSTREAM, conn, buf, len, budget_ms, eof);
}

static int bt_write_all(nj_vm *vm, nj_ref conn, const nj_u1 *buf, int len)
{
    return bt_x_write_all(vm, NJ_C_BTSTREAM, conn, buf, len);
}

static void bt_x_close(nj_vm *vm, const char *cls, nj_ref conn)
{
    const nj_port *p = bt_port_of(vm, conn);
    nj_s8 h = bt_x_handle(vm, cls, conn);
    if (h != 0 && p && p->bt_close)
        p->bt_close(p->ud, (void *)(intptr_t)h);
    bset_long(vm, conn, cls, F_BHANDLE, "J", 0);
    bset_int(vm, conn, cls, F_BCLOSED, "Z", 1);
}

static void bt_conn_close(nj_vm *vm, nj_ref conn)
{
    bt_x_close(vm, NJ_C_BTSTREAM, conn);
}

/* ------------------------------------------------------------------ */
/* Natives: the RFCOMM connection and its streams                      */
/* ------------------------------------------------------------------ */

NJFN(btstream_open_input)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = bget_ref(vm, self, NJ_C_BTSTREAM, F_BIN, "Ljava/io/InputStream;");
    if (bt_closed(vm, self))
        return bt_io_fail(vm, "the connection is closed");
    if (!s) {
        s = bt_stream_wrap(vm, NJ_C_BTIN, self);
        if (!s) return NJ_ERR;
        bset_ref(vm, self, NJ_C_BTSTREAM, F_BIN, "Ljava/io/InputStream;", s);
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(btstream_open_data_input)
{
    nj_value a[1];
    if (btstream_open_input(vm, th, args, ret) != NJ_OK) return NJ_ERR;
    a[0].r = ret->r;
    ret->r = nj_object_new(vm, "java/io/DataInputStream");
    if (!ret->r) return NJ_ERR;
    return nj_call_special(vm, ret->r, "java/io/DataInputStream", "<init>",
                           "(Ljava/io/InputStream;)V", a, NULL);
}

NJFN(btstream_open_output)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = bget_ref(vm, self, NJ_C_BTSTREAM, F_BOUT, "Ljava/io/OutputStream;");
    if (bt_closed(vm, self))
        return bt_io_fail(vm, "the connection is closed");
    if (!s) {
        s = bt_stream_wrap(vm, NJ_C_BTOUT, self);
        if (!s) return NJ_ERR;
        bset_ref(vm, self, NJ_C_BTSTREAM, F_BOUT, "Ljava/io/OutputStream;", s);
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(btstream_open_data_output)
{
    nj_value a[1];
    if (btstream_open_output(vm, th, args, ret) != NJ_OK) return NJ_ERR;
    a[0].r = ret->r;
    ret->r = nj_object_new(vm, "java/io/DataOutputStream");
    if (!ret->r) return NJ_ERR;
    return nj_call_special(vm, ret->r, "java/io/DataOutputStream", "<init>",
                           "(Ljava/io/OutputStream;)V", a, NULL);
}

NJFN(btstream_close)
{
    bt_conn_close(vm, NJ_THIS(args));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: reading and writing                                        */
/* ------------------------------------------------------------------ */

static nj_ref bt_stream_conn(nj_vm *vm, nj_ref stream)
{
    return bget_ref(vm, stream, NJ_C_BTIN, F_SCONN, "Ljavax/bluetooth/BtStream;");
}

NJFN(btin_read_byte)
{
    nj_u1 b = 0;
    int eof = 0, n;
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!c) return bt_io_fail(vm, "the stream is not open");
    n = bt_read_once(vm, c, &b, 1, 30000u, &eof);
    if (n == -2) return NJ_ERR;
    ret->i = n == 1 ? (nj_s4)b : -1;
    return NJ_OK;
}

NJFN(btin_read_bytes)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    int eof = 0, n;
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!c) return bt_io_fail(vm, "the stream is not open");
    n = bt_read_once(vm, c, (nj_u1 *)nj_array_data(arr),
                     (int)nj_array_length(arr), 30000u, &eof);
    if (n == -2) return NJ_ERR;
    ret->i = n;
    return NJ_OK;
}

NJFN(btin_read_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_u4 alen;
    int eof = 0, n;
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    alen = nj_array_length(arr);
    if (off < 0 || len < 0 || (nj_u4)(off + len) > alen || (nj_u4)off > alen)
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    if (len == 0) { ret->i = 0; return NJ_OK; }
    if (!c) return bt_io_fail(vm, "the stream is not open");
    n = bt_read_once(vm, c, (nj_u1 *)nj_array_data(arr) + off, len, 30000u, &eof);
    if (n == -2) return NJ_ERR;
    ret->i = n;
    return NJ_OK;
}

NJFN(btin_skip)
{
    nj_u1 buf[64];
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s8 got = 0;
    int eof = 0;
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!c) return bt_io_fail(vm, "the stream is not open");
    while (got < want) {
        nj_s8 left = want - got;
        int n = bt_read_once(vm, c, buf,
                             left > (nj_s8)sizeof buf ? (int)sizeof buf : (int)left,
                             1000u, &eof);
        if (n <= 0) break;
        got += n;
    }
    ret->j = got;
    return NJ_OK;
}

NJFN(btin_available)
{
    /* There is no way to ask a radio how much is waiting without reading it,
     * and answering "0" is what the specification allows: a caller that wants
     * bytes reads them. */
    ret->i = 0;
    return NJ_OK;
}

NJFN(btin_close)
{
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (c) bset_ref(vm, c, NJ_C_BTSTREAM, F_BIN, "Ljava/io/InputStream;", NULL);
    ret->i = 0;
    return NJ_OK;
}

NJFN(btout_write_byte)
{
    nj_u1 b = (nj_u1)NJ_ARG_INT(args, 0);
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!c) return bt_io_fail(vm, "the stream is not open");
    if (bt_write_all(vm, c, &b, 1) < 0)
        return bt_io_fail(vm, "the Bluetooth write failed");
    return NJ_OK;
}

NJFN(btout_write_bytes)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (!c) return bt_io_fail(vm, "the stream is not open");
    if (bt_write_all(vm, c, (const nj_u1 *)nj_array_data(arr),
                     (int)nj_array_length(arr)) < 0)
        return bt_io_fail(vm, "the Bluetooth write failed");
    return NJ_OK;
}

NJFN(btout_write_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_u4 alen;
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    alen = nj_array_length(arr);
    if (off < 0 || len < 0 || (nj_u4)(off + len) > alen || (nj_u4)off > alen)
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    if (!c) return bt_io_fail(vm, "the stream is not open");
    if (bt_write_all(vm, c, (const nj_u1 *)nj_array_data(arr) + off, len) < 0)
        return bt_io_fail(vm, "the Bluetooth write failed");
    return NJ_OK;
}

NJFN(btout_flush)
{
    return NJ_OK;
}

NJFN(btout_close)
{
    nj_ref c = bt_stream_conn(vm, NJ_THIS(args));
    if (c) bset_ref(vm, c, NJ_C_BTSTREAM, F_BOUT, "Ljava/io/OutputStream;", NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: L2CAP                                                      */
/* ------------------------------------------------------------------ */

static nj_ref bt_l2_new(nj_vm *vm, void *handle, const char *url)
{
    nj_ref c = nj_object_new(vm, NJ_C_BTL2);
    if (!c) return NULL;
    bset_long(vm, c, NJ_C_BTL2, F_BHANDLE, "J", (nj_s8)(intptr_t)handle);
    bset_ref(vm, c, NJ_C_BTL2, F_BURL, "Ljava/lang/String;", bstr(vm, url));
    return c;
}

NJFN(btl2_send)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (bt_x_write_all(vm, NJ_C_BTL2, self, (const nj_u1 *)nj_array_data(arr),
                       (int)nj_array_length(arr)) < 0)
        return bt_io_fail(vm, "the L2CAP write failed");
    return NJ_OK;
}

NJFN(btl2_receive)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    int eof = 0, n;
    if (!arr) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    n = bt_x_read_once(vm, NJ_C_BTL2, NJ_THIS(args),
                       (nj_u1 *)nj_array_data(arr),
                       (int)nj_array_length(arr), 30000u, &eof);
    if (n == -2) return NJ_ERR;
    /* The specification has receive() block until a packet arrives; here the
     * port answers when it has one, and the caller comes back for more. */
    ret->i = n < 0 ? -1 : n;
    return NJ_OK;
}

NJFN(btl2_ready)
{
    ret->i = 0;   /* a radio cannot be asked without reading it */
    return NJ_OK;
}

NJFN(btl2_recv_mtu)
{
    ret->i = 672;   /* L2CAPConnection.DEFAULT_MTU */
    return NJ_OK;
}

NJFN(btl2_trans_mtu)
{
    ret->i = 672;
    return NJ_OK;
}

NJFN(btl2_close)
{
    bt_x_close(vm, NJ_C_BTL2, NJ_THIS(args));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: the two servers                                            */
/* ------------------------------------------------------------------ */

/* Waiting for a peer is the one place this runtime blocks on the radio, and it
 * is what acceptAndOpen() means.  A port that cannot wait answers NULL and is
 * asked again; the whole wait is capped so a game that listens for ever
 * without a peer cannot wedge the frame loop without saying so. */
static void *bt_accept_wait(nj_vm *vm, nj_ref notifier, const char *cls,
                            nj_u4 cap_ms)
{
    const nj_port *p = nj_vm_port(vm);
    nj_s8 h = bget_long(vm, notifier, cls, F_NHANDLE, "J");
    nj_u4 waited = 0;
    if (h == 0 || !p || !p->bt_accept) return NULL;
    for (;;) {
        void *peer = p->bt_accept(p->ud, (void *)(intptr_t)h, 250);
        if (peer) return peer;
        if (waited >= cap_ms) return NULL;
        if (p->sleep_ms) p->sleep_ms(p->ud, 10);
        else if (p->idle) p->idle(p->ud);
        waited += 10;
    }
}

NJFN(btnotifier_accept)
{
    nj_ref self = NJ_THIS(args);
    const nj_port *p = nj_vm_port(vm);
    void *peer;
    if (bget_int(vm, self, NJ_C_BTSERVER, F_NCLOSED, "Z"))
        return bt_io_fail(vm, "the server is closed");
    peer = bt_accept_wait(vm, self, NJ_C_BTSERVER, 30000u);
    if (!peer)
        return bt_io_fail(vm, "no Bluetooth peer connected");
    ret->r = bt_stream_new(vm, peer, "", "");
    if (!ret->r) {
        if (p && p->bt_close) p->bt_close(p->ud, peer);
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(btl2notifier_accept)
{
    nj_ref self = NJ_THIS(args);
    const nj_port *p = nj_vm_port(vm);
    void *peer;
    if (bget_int(vm, self, NJ_C_BTL2N, F_NCLOSED, "Z"))
        return bt_io_fail(vm, "the server is closed");
    peer = bt_accept_wait(vm, self, NJ_C_BTL2N, 30000u);
    if (!peer)
        return bt_io_fail(vm, "no Bluetooth peer connected");
    ret->r = bt_l2_new(vm, peer, "");
    if (!ret->r) {
        if (p && p->bt_close) p->bt_close(p->ud, peer);
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(btnotifier_close)
{
    nj_ref self = NJ_THIS(args);
    const nj_port *p = nj_vm_port(vm);
    nj_s8 h = bget_long(vm, self, NJ_C_BTSERVER, F_NHANDLE, "J");
    if (h != 0 && p && p->bt_close) {
        p->bt_close(p->ud, (void *)(intptr_t)h);
        bset_long(vm, self, NJ_C_BTSERVER, F_NHANDLE, "J", 0);
    }
    bset_int(vm, self, NJ_C_BTSERVER, F_NCLOSED, "Z", 1);
    bset_int(vm, self, NJ_C_BTL2N, F_NCLOSED, "Z", 1);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The entry point Connector.open() uses                               */
/* ------------------------------------------------------------------ */

/*
 * "btspp://localhost:<uuid>;name=x" and "btl2cap://localhost:<uuid>;name=x"
 * open a server.  A client URL never reaches here: it is a stream, and
 * Connector hands it to socket_connect(), which is where every other stream
 * already goes.
 */
nj_ref nj_bt_listen(nj_vm *vm, const char *url)
{
    const nj_port *p = nj_vm_port(vm);
    char err[128];
    void *handle;
    int l2;
    nj_ref n;

    if (!url) return NULL;
    l2 = strncmp(url, "btl2cap:", 8) == 0;
    if (!p || !p->bt_listen) return NULL;
    err[0] = '\0';
    handle = p->bt_listen(p->ud, url, err, sizeof err);
    if (!handle) return NULL;
    if (l2) {
        n = nj_object_new(vm, NJ_C_BTL2N);
        if (n) {
            bset_long(vm, n, NJ_C_BTL2N, F_NHANDLE, "J", (nj_s8)(intptr_t)handle);
            bset_int(vm, n, NJ_C_BTL2N, F_NL2, "Z", 1);
        }
    } else {
        n = nj_object_new(vm, NJ_C_BTSERVER);
        if (n)
            bset_long(vm, n, NJ_C_BTSERVER, F_NHANDLE, "J",
                      (nj_s8)(intptr_t)handle);
    }
    if (!n) {
        if (p->bt_close) p->bt_close(p->ud, handle);
        return NULL;
    }
    if (l2) bset_ref(vm, n, NJ_C_BTL2N, F_NURL, "Ljava/lang/String;", bstr(vm, url));
    else bset_ref(vm, n, NJ_C_BTSERVER, F_NURL, "Ljava/lang/String;", bstr(vm, url));
    return n;
}

const char *nj_bt_listen_error(nj_vm *vm, const char *url)
{
    const nj_port *p = nj_vm_port(vm);
    if (!p || !p->bt_listen)
        return "this build has no Bluetooth support; the port must supply it";
    (void)url;
    return "the Bluetooth server could not be opened";
}

/* ------------------------------------------------------------------ */
/* The class and native tables                                         */
/* ------------------------------------------------------------------ */

#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field uuid_fields[] = {
    FLD(F_UHI, "J", NJ_ACC_PRIVATE | NJ_ACC_FINAL),
    FLD(F_ULO, "J", NJ_ACC_PRIVATE | NJ_ACC_FINAL)
};
static const nj_builtin_method uuid_methods[] = {
    M("<init>",   "(Ljava/lang/String;)V", NJ_ACC_PUBLIC),
    M("<init>",   "(J)V",                  NJ_ACC_PUBLIC),
    M("equals",   "(Ljava/lang/Object;)Z", NJ_ACC_PUBLIC),
    M("hashCode", "()I",                   NJ_ACC_PUBLIC),
    M("toString", "()Ljava/lang/String;",  NJ_ACC_PUBLIC)
};

static const nj_builtin_field devclass_fields[] = {
    FLD(F_DVAL, "I", NJ_ACC_PRIVATE | NJ_ACC_FINAL)
};
static const nj_builtin_method devclass_methods[] = {
    M("<init>",                 "(I)V", NJ_ACC_PUBLIC),
    M("getMajorDeviceClass",    "()I",  NJ_ACC_PUBLIC),
    M("getMinorDeviceClass",    "()I",  NJ_ACC_PUBLIC),
    M("getServiceClasses",      "()I",  NJ_ACC_PUBLIC)
};

static const nj_builtin_method btstate_methods[] = {
    M("<init>", "()V",                  NJ_ACC_PUBLIC),
    M("<init>", "(Ljava/lang/String;)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field delem_fields[] = {
    FLD(F_ETYPE, "I", NJ_ACC_PRIVATE),
    FLD(F_ENUM,  "J", NJ_ACC_PRIVATE),
    FLD(F_EOBJ,  "Ljava/lang/Object;", NJ_ACC_PRIVATE),
    FLD(F_ESEQ,  "Ljava/util/Vector;", NJ_ACC_PRIVATE)
};
static const nj_builtin_method delem_methods[] = {
    M("<init>",           "(I)V", NJ_ACC_PUBLIC),
    M("<init>",           "(Z)V", NJ_ACC_PUBLIC),
    M("<init>",           "(IJ)V", NJ_ACC_PUBLIC),
    M("<init>",           "(ILjava/lang/Object;)V", NJ_ACC_PUBLIC),
    M("getDataType",      "()I",  NJ_ACC_PUBLIC),
    M("getLong",          "()J",  NJ_ACC_PUBLIC),
    M("getBoolean",       "()Z",  NJ_ACC_PUBLIC),
    M("getValue",         "()Ljava/lang/Object;", NJ_ACC_PUBLIC),
    M("getSize",          "()I",  NJ_ACC_PUBLIC),
    M("addElement",       "(Ljavax/bluetooth/DataElement;)V", NJ_ACC_PUBLIC),
    M("insertElementAt",  "(Ljavax/bluetooth/DataElement;I)V", NJ_ACC_PUBLIC),
    M("removeElement",    "(Ljavax/bluetooth/DataElement;)Z", NJ_ACC_PUBLIC)
};

static const nj_builtin_field svcrec_fields[] = {
    FLD(F_RHOST, "Ljavax/bluetooth/RemoteDevice;", NJ_ACC_PRIVATE),
    FLD(F_RURL,  "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_RNAME, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_RUUID, "Ljavax/bluetooth/UUID;", NJ_ACC_PRIVATE),
    FLD(F_RIDS,  "[I", NJ_ACC_PRIVATE),
    FLD(F_RVALS, "[Ljava/lang/Object;", NJ_ACC_PRIVATE)
};
static const nj_builtin_method svcrec_methods[] = {
    M("getConnectionURL",   "(IZ)Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getAttributeValue",  "(I)Ljavax/bluetooth/DataElement;", NJ_ACC_PUBLIC),
    M("setAttributeValue",  "(ILjavax/bluetooth/DataElement;)V", NJ_ACC_PUBLIC),
    M("removeAttributeValue", "(IZ)V", NJ_ACC_PUBLIC),
    M("getAttributeIDs",    "()[I", NJ_ACC_PUBLIC),
    M("getHostDevice",      "()Ljavax/bluetooth/RemoteDevice;", NJ_ACC_PUBLIC)
};

static const nj_builtin_field remote_fields[] = {
    FLD(F_VADDR, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_VNAME, "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_VCLS,  "I", NJ_ACC_PRIVATE)
};
static const nj_builtin_method remote_methods[] = {
    M("getBluetoothAddress", "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getFriendlyName",     "(Z)Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getDeviceClass",      "()Ljavax/bluetooth/DeviceClass;", NJ_ACC_PUBLIC),
    M("equals",              "(Ljava/lang/Object;)Z", NJ_ACC_PUBLIC),
    M("hashCode",            "()I", NJ_ACC_PUBLIC),
    M("getRemoteDevice",     "(Ljava/lang/String;)Ljavax/bluetooth/RemoteDevice;",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

static const nj_builtin_method local_methods[] = {
    M("getLocalDevice",   "()Ljavax/bluetooth/LocalDevice;",
      NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("getFriendlyName",  "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getBluetoothAddress", "()Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getDeviceClass",   "()Ljavax/bluetooth/DeviceClass;", NJ_ACC_PUBLIC),
    M("getDiscoveryAgent", "()Ljavax/bluetooth/DiscoveryAgent;", NJ_ACC_PUBLIC),
    M("setDiscoverable",  "(I)Z", NJ_ACC_PUBLIC),
    M("getProperty",      "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_PUBLIC),
    M("getRecord",        "(Ljavax/bluetooth/ServiceRecord;)Ljavax/bluetooth/ServiceRecord;",
      NJ_ACC_PUBLIC),
    M("updateRecord",     "(Ljavax/bluetooth/ServiceRecord;)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_method agent_methods[] = {
    M("startInquiry",      "(ILjavax/bluetooth/DiscoveryListener;)Z", NJ_ACC_PUBLIC),
    M("cancelInquiry",     "(Ljavax/bluetooth/DiscoveryListener;)Z", NJ_ACC_PUBLIC),
    M("searchServices",    "([I[Ljavax/bluetooth/UUID;Ljavax/bluetooth/RemoteDevice;"
                           "Ljavax/bluetooth/DiscoveryListener;)I", NJ_ACC_PUBLIC),
    M("cancelServiceSearch", "(I)Z", NJ_ACC_PUBLIC),
    M("retrieveDevices",   "(I)[Ljavax/bluetooth/RemoteDevice;", NJ_ACC_PUBLIC),
    M("selectService",     "(Ljavax/bluetooth/UUID;IZ)Ljava/lang/String;", NJ_ACC_PUBLIC)
};

static const nj_builtin_method listener_methods[] = {
    M("deviceDiscovered",  "(Ljavax/bluetooth/RemoteDevice;Ljavax/bluetooth/DeviceClass;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("servicesDiscovered", "(I[Ljavax/bluetooth/ServiceRecord;)V",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("serviceSearchCompleted", "(II)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("inquiryCompleted",  "(I)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_method l2cap_methods[] = {
    M("send",           "([B)V", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("receive",        "([B)I", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("ready",          "()Z",   NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getReceiveMTU",  "()I",   NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getTransmitMTU", "()I",   NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("close",          "()V",   NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};
static const nj_builtin_method l2capn_methods[] = {
    M("acceptAndOpen", "()Ljavax/bluetooth/L2CAPConnection;",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_field conn_fields[] = {
    FLD(F_BHANDLE, "J", NJ_ACC_PRIVATE),
    FLD(F_BURL,    "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_BADDR,   "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_BIN,     "Ljava/io/InputStream;", NJ_ACC_PRIVATE),
    FLD(F_BOUT,    "Ljava/io/OutputStream;", NJ_ACC_PRIVATE),
    FLD(F_BCLOSED, "Z", NJ_ACC_PRIVATE)
};
static const nj_builtin_method btstream_methods[] = {
    M("openInputStream",     "()Ljava/io/InputStream;", NJ_ACC_PUBLIC),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", NJ_ACC_PUBLIC),
    M("openOutputStream",    "()Ljava/io/OutputStream;", NJ_ACC_PUBLIC),
    M("openDataOutputStream","()Ljava/io/DataOutputStream;", NJ_ACC_PUBLIC),
    M("close",               "()V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method btl2_methods[] = {
    M("send",           "([B)V", NJ_ACC_PUBLIC),
    M("receive",        "([B)I", NJ_ACC_PUBLIC),
    M("ready",          "()Z",   NJ_ACC_PUBLIC),
    M("getReceiveMTU",  "()I",   NJ_ACC_PUBLIC),
    M("getTransmitMTU", "()I",   NJ_ACC_PUBLIC),
    M("close",          "()V",   NJ_ACC_PUBLIC)
};

static const nj_builtin_field notifier_fields[] = {
    FLD(F_NHANDLE, "J", NJ_ACC_PRIVATE),
    FLD(F_NURL,    "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_NUUID,   "Ljava/lang/String;", NJ_ACC_PRIVATE),
    FLD(F_NL2,     "Z", NJ_ACC_PRIVATE),
    FLD(F_NCLOSED, "Z", NJ_ACC_PRIVATE)
};
static const nj_builtin_method btnotifier_methods[] = {
    M("acceptAndOpen", "()Ljavax/microedition/io/StreamConnection;", NJ_ACC_PUBLIC),
    M("close",         "()V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method btl2notifier_methods[] = {
    M("acceptAndOpen", "()Ljavax/bluetooth/L2CAPConnection;", NJ_ACC_PUBLIC),
    M("close",         "()V", NJ_ACC_PUBLIC)
};

static const nj_builtin_field stream_fields[] = {
    FLD(F_SCONN,   "Ljavax/bluetooth/BtStream;", NJ_ACC_PRIVATE),
    FLD(F_SCLOSED, "Z", NJ_ACC_PRIVATE)
};
static const nj_builtin_method btin_methods[] = {
    M("read",      "()I",    NJ_ACC_PUBLIC),
    M("read",      "([B)I",  NJ_ACC_PUBLIC),
    M("read",      "([BII)I", NJ_ACC_PUBLIC),
    M("skip",      "(J)J",   NJ_ACC_PUBLIC),
    M("available", "()I",    NJ_ACC_PUBLIC),
    M("close",     "()V",    NJ_ACC_PUBLIC)
};
static const nj_builtin_method btout_methods[] = {
    M("write", "([B)V",   NJ_ACC_PUBLIC),
    M("write", "([BII)V", NJ_ACC_PUBLIC),
    M("write", "(I)V",    NJ_ACC_PUBLIC),
    M("flush", "()V",     NJ_ACC_PUBLIC),
    M("close", "()V",     NJ_ACC_PUBLIC)
};

static const nj_builtin_field job_fields[] = {
    FLD(F_JKIND,   "I", NJ_ACC_PRIVATE),
    FLD(F_JLISTEN, "Ljavax/bluetooth/DiscoveryListener;", NJ_ACC_PRIVATE),
    FLD(F_JFIRED,  "I", NJ_ACC_PRIVATE),
    FLD(F_JACCESS, "I", NJ_ACC_PRIVATE),
    FLD(F_JDEV,    "Ljavax/bluetooth/RemoteDevice;", NJ_ACC_PRIVATE),
    FLD(F_JUUID,   "Ljavax/bluetooth/UUID;", NJ_ACC_PRIVATE),
    FLD(F_JTRANS,  "I", NJ_ACC_PRIVATE),
    FLD(F_JSTART,  "J", NJ_ACC_PRIVATE),
    FLD(F_JDUR,    "I", NJ_ACC_PRIVATE),
    FLD(F_JDONE,   "Z", NJ_ACC_PRIVATE)
};

#define NCLASS(cls, sup, fl, ff, fm) \
    { cls, sup, fl, ff, (nj_u2)(sizeof(ff) / sizeof(ff[0])), \
      fm, (nj_u2)(sizeof(fm) / sizeof(fm[0])) }
/* A class with no fields of its own, and one with no methods of its own: the
 * count has to be spelled out, because there is nothing to measure. */
#define NCLASS0(cls, sup, fl, fm) \
    { cls, sup, fl, NULL, 0, fm, (nj_u2)(sizeof(fm) / sizeof(fm[0])) }
#define NCLASSM0(cls, sup, fl, ff) \
    { cls, sup, fl, ff, (nj_u2)(sizeof(ff) / sizeof(ff[0])), NULL, 0 }

const nj_builtin_class nj_lib_bt_classes[] = {
    NCLASS(NJ_C_UUID, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
           uuid_fields, uuid_methods),
    NCLASS(NJ_C_DEVCLASS, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
           devclass_fields, devclass_methods),
    NCLASS0(NJ_C_BTSTATE, "java/io/IOException", NJ_ACC_PUBLIC,
            btstate_methods),
    NCLASS(NJ_C_DELEM, "java/lang/Object", NJ_ACC_PUBLIC,
           delem_fields, delem_methods),
    NCLASS(NJ_C_SVCREC, "java/lang/Object", NJ_ACC_PUBLIC,
           svcrec_fields, svcrec_methods),
    NCLASS(NJ_C_REMOTE, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
           remote_fields, remote_methods),
    NCLASS0(NJ_C_LOCAL, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
            local_methods),
    NCLASS0(NJ_C_AGENT, "java/lang/Object", NJ_ACC_PUBLIC,
            agent_methods),
    NCLASS0(NJ_C_LISTENER, "java/lang/Object",
            NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
            listener_methods),
    NCLASS0(NJ_C_L2CAP, "javax/microedition/io/Connection",
            NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
            l2cap_methods),
    NCLASS0(NJ_C_L2CAPN, "javax/microedition/io/Connection",
            NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
            l2capn_methods),
    NCLASS(NJ_C_BTSTREAM, NJ_C_STREAM, NJ_ACC_PUBLIC,
           conn_fields, btstream_methods),
    NCLASS(NJ_C_BTL2, NJ_C_L2CAP, NJ_ACC_PUBLIC,
           conn_fields, btl2_methods),
    NCLASS(NJ_C_BTSERVER, NJ_C_STREAMNOT, NJ_ACC_PUBLIC,
           notifier_fields, btnotifier_methods),
    NCLASS(NJ_C_BTL2N, NJ_C_L2CAPN, NJ_ACC_PUBLIC,
           notifier_fields, btl2notifier_methods),
    NCLASS(NJ_C_BTIN, NJ_C_ISTREAM, NJ_ACC_PUBLIC, stream_fields, btin_methods),
    NCLASS(NJ_C_BTOUT, NJ_C_OSTREAM, NJ_ACC_PUBLIC, stream_fields, btout_methods),
    NCLASSM0(NJ_C_JOB, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_FINAL,
             job_fields)
};
const int nj_lib_bt_class_count =
    (int)(sizeof(nj_lib_bt_classes) / sizeof(nj_lib_bt_classes[0]));

const nj_native_entry nj_lib_bt_table[] = {
    { NJ_C_UUID,     "<init>",   "(Ljava/lang/String;)V", uuid_init_str },
    { NJ_C_UUID,     "<init>",   "(J)V",                  uuid_init_long },
    { NJ_C_UUID,     "equals",   "(Ljava/lang/Object;)Z", uuid_equals },
    { NJ_C_UUID,     "hashCode", "()I",                   uuid_hash },
    { NJ_C_UUID,     "toString", "()Ljava/lang/String;",  uuid_to_string },

    { NJ_C_DEVCLASS, "<init>",              "(I)V", devclass_init },
    { NJ_C_DEVCLASS, "getMajorDeviceClass", "()I",  devclass_major },
    { NJ_C_DEVCLASS, "getMinorDeviceClass", "()I",  devclass_minor },
    { NJ_C_DEVCLASS, "getServiceClasses",   "()I",  devclass_services },

    { NJ_C_BTSTATE,  "<init>", "()V",                  btstate_init },
    { NJ_C_BTSTATE,  "<init>", "(Ljava/lang/String;)V", btstate_init_str },

    { NJ_C_DELEM, "<init>",          "(I)V", delem_init_type },
    { NJ_C_DELEM, "<init>",          "(Z)V", delem_init_bool },
    { NJ_C_DELEM, "<init>",          "(IJ)V", delem_init_int },
    { NJ_C_DELEM, "<init>",          "(ILjava/lang/Object;)V", delem_init_obj },
    { NJ_C_DELEM, "getDataType",     "()I", delem_get_type },
    { NJ_C_DELEM, "getLong",         "()J", delem_get_long },
    { NJ_C_DELEM, "getBoolean",      "()Z", delem_get_boolean },
    { NJ_C_DELEM, "getValue",        "()Ljava/lang/Object;", delem_get_value },
    { NJ_C_DELEM, "getSize",         "()I", delem_get_size },
    { NJ_C_DELEM, "addElement",      "(Ljavax/bluetooth/DataElement;)V", delem_add },
    { NJ_C_DELEM, "insertElementAt", "(Ljavax/bluetooth/DataElement;I)V", delem_insert },
    { NJ_C_DELEM, "removeElement",   "(Ljavax/bluetooth/DataElement;)Z", delem_remove },

    { NJ_C_SVCREC, "getConnectionURL",     "(IZ)Ljava/lang/String;", svcrec_get_url },
    { NJ_C_SVCREC, "getAttributeValue",    "(I)Ljavax/bluetooth/DataElement;", svcrec_get_attr },
    { NJ_C_SVCREC, "setAttributeValue",    "(ILjavax/bluetooth/DataElement;)V", svcrec_set_attr },
    { NJ_C_SVCREC, "removeAttributeValue", "(IZ)V", svcrec_remove_attr },
    { NJ_C_SVCREC, "getAttributeIDs",      "()[I", svcrec_attr_ids },
    { NJ_C_SVCREC, "getHostDevice",        "()Ljavax/bluetooth/RemoteDevice;", svcrec_host },

    { NJ_C_REMOTE, "getBluetoothAddress", "()Ljava/lang/String;", remote_address },
    { NJ_C_REMOTE, "getFriendlyName",     "(Z)Ljava/lang/String;", remote_name },
    { NJ_C_REMOTE, "getDeviceClass",      "()Ljavax/bluetooth/DeviceClass;", remote_class },
    { NJ_C_REMOTE, "equals",              "(Ljava/lang/Object;)Z", remote_equals },
    { NJ_C_REMOTE, "hashCode",            "()I", remote_hash },
    { NJ_C_REMOTE, "getRemoteDevice",     "(Ljava/lang/String;)Ljavax/bluetooth/RemoteDevice;",
      remote_get_remote_device },

    { NJ_C_LOCAL, "getLocalDevice",     "()Ljavax/bluetooth/LocalDevice;", local_get_local_device },
    { NJ_C_LOCAL, "getFriendlyName",    "()Ljava/lang/String;", local_friendly_name },
    { NJ_C_LOCAL, "getBluetoothAddress","()Ljava/lang/String;", local_address },
    { NJ_C_LOCAL, "getDeviceClass",     "()Ljavax/bluetooth/DeviceClass;", local_device_class },
    { NJ_C_LOCAL, "getDiscoveryAgent",  "()Ljavax/bluetooth/DiscoveryAgent;", local_get_agent },
    { NJ_C_LOCAL, "setDiscoverable",    "(I)Z", local_set_discoverable },
    { NJ_C_LOCAL, "getProperty",        "(Ljava/lang/String;)Ljava/lang/String;", local_get_property },
    { NJ_C_LOCAL, "getRecord",          "(Ljavax/bluetooth/ServiceRecord;)Ljavax/bluetooth/ServiceRecord;",
      local_get_record },
    { NJ_C_LOCAL, "updateRecord",       "(Ljavax/bluetooth/ServiceRecord;)V", local_update_record },

    { NJ_C_AGENT, "startInquiry",       "(ILjavax/bluetooth/DiscoveryListener;)Z", agent_start_inquiry },
    { NJ_C_AGENT, "cancelInquiry",      "(Ljavax/bluetooth/DiscoveryListener;)Z", agent_cancel_inquiry },
    { NJ_C_AGENT, "searchServices",     "([I[Ljavax/bluetooth/UUID;Ljavax/bluetooth/RemoteDevice;"
                                        "Ljavax/bluetooth/DiscoveryListener;)I", agent_search_services },
    { NJ_C_AGENT, "cancelServiceSearch","(I)Z", agent_cancel_search },
    { NJ_C_AGENT, "retrieveDevices",    "(I)[Ljavax/bluetooth/RemoteDevice;", agent_retrieve_devices },
    { NJ_C_AGENT, "selectService",      "(Ljavax/bluetooth/UUID;IZ)Ljava/lang/String;", agent_select_service },

    { NJ_C_BTSTREAM, "openInputStream",      "()Ljava/io/InputStream;", btstream_open_input },
    { NJ_C_BTSTREAM, "openDataInputStream",  "()Ljava/io/DataInputStream;", btstream_open_data_input },
    { NJ_C_BTSTREAM, "openOutputStream",     "()Ljava/io/OutputStream;", btstream_open_output },
    { NJ_C_BTSTREAM, "openDataOutputStream", "()Ljava/io/DataOutputStream;", btstream_open_data_output },
    { NJ_C_BTSTREAM, "close",                "()V", btstream_close },

    { NJ_C_BTIN,  "read",      "()I",     btin_read_byte },
    { NJ_C_BTIN,  "read",      "([B)I",   btin_read_bytes },
    { NJ_C_BTIN,  "read",      "([BII)I", btin_read_range },
    { NJ_C_BTIN,  "skip",      "(J)J",    btin_skip },
    { NJ_C_BTIN,  "available", "()I",     btin_available },
    { NJ_C_BTIN,  "close",     "()V",     btin_close },
    { NJ_C_BTOUT, "write",     "(I)V",    btout_write_byte },
    { NJ_C_BTOUT, "write",     "([B)V",   btout_write_bytes },
    { NJ_C_BTOUT, "write",     "([BII)V", btout_write_range },
    { NJ_C_BTOUT, "flush",     "()V",     btout_flush },
    { NJ_C_BTOUT, "close",     "()V",     btout_close },

    { NJ_C_BTL2, "send",           "([B)V", btl2_send },
    { NJ_C_BTL2, "receive",        "([B)I", btl2_receive },
    { NJ_C_BTL2, "ready",          "()Z",   btl2_ready },
    { NJ_C_BTL2, "getReceiveMTU",  "()I",   btl2_recv_mtu },
    { NJ_C_BTL2, "getTransmitMTU", "()I",   btl2_trans_mtu },
    { NJ_C_BTL2, "close",          "()V",   btl2_close },

    { NJ_C_BTSERVER, "acceptAndOpen", "()Ljavax/microedition/io/StreamConnection;", btnotifier_accept },
    { NJ_C_BTSERVER, "close",         "()V", btnotifier_close },
    { NJ_C_BTL2N,    "acceptAndOpen", "()Ljavax/bluetooth/L2CAPConnection;", btl2notifier_accept },
    { NJ_C_BTL2N,    "close",         "()V", btnotifier_close }
};
const int nj_lib_bt_count =
    (int)(sizeof(nj_lib_bt_table) / sizeof(nj_lib_bt_table[0]));

/* ------------------------------------------------------------------ */
/* The constants, seeded once the classes exist                        */
/* ------------------------------------------------------------------ */

nj_status nj_bt_late_init(nj_vm *vm);

nj_status nj_bt_late_init(nj_vm *vm)
{
    static const struct { const char *cls, *name; nj_s4 val; } k[] = {
        { NJ_C_AGENT,   "NOT_DISCOVERABLE", 0x0000 },
        { NJ_C_AGENT,   "GIAC",             BT_GIAC },
        { NJ_C_AGENT,   "LIAC",             0x9E8B00 },
        { NJ_C_AGENT,   "CACHED",           0x00 },
        { NJ_C_AGENT,   "PREKNOWN",         0x01 },
        { NJ_C_LISTENER,"INQUIRY_COMPLETED", 0 },
        { NJ_C_LISTENER,"INQUIRY_TERMINATED", 1 },
        { NJ_C_LISTENER,"INQUIRY_ERROR",     2 },
        { NJ_C_LISTENER,"SERVICE_SEARCH_COMPLETED", 1 },
        { NJ_C_LISTENER,"SERVICE_SEARCH_TERMINATED", 2 },
        { NJ_C_LISTENER,"SERVICE_SEARCH_ERROR", 3 },
        { NJ_C_LISTENER,"SERVICE_SEARCH_NO_RECORDS", 4 },
        { NJ_C_LISTENER,"SERVICE_SEARCH_DEVICE_NOT_REACHABLE", 6 },
        { NJ_C_SVCREC,  "NOAUTHENTICATE_NOENCRYPT", 0 },
        { NJ_C_SVCREC,  "AUTHENTICATE_NOENCRYPT", 1 },
        { NJ_C_SVCREC,  "AUTHENTICATE_ENCRYPT", 2 },
        { NJ_C_L2CAP,   "DEFAULT_MTU", 672 },
        { NJ_C_L2CAP,   "MINIMUM_MTU", 48 },
        { NJ_C_DELEM,   "NULL",    DE_NULL },
        { NJ_C_DELEM,   "U_INT_1", DE_U_INT_1 },
        { NJ_C_DELEM,   "U_INT_2", DE_U_INT_2 },
        { NJ_C_DELEM,   "U_INT_4", DE_U_INT_4 },
        { NJ_C_DELEM,   "U_INT_8", DE_U_INT_8 },
        { NJ_C_DELEM,   "U_INT_16", DE_U_INT_16 },
        { NJ_C_DELEM,   "INT_1",   DE_INT_1 },
        { NJ_C_DELEM,   "INT_2",   DE_INT_2 },
        { NJ_C_DELEM,   "INT_4",   DE_INT_4 },
        { NJ_C_DELEM,   "INT_8",   DE_INT_8 },
        { NJ_C_DELEM,   "INT_16",  DE_INT_16 },
        { NJ_C_DELEM,   "UUID",    DE_UUID },
        { NJ_C_DELEM,   "STRING",  DE_STRING },
        { NJ_C_DELEM,   "BOOL",    DE_BOOL },
        { NJ_C_DELEM,   "DATSEQ",  DE_DATSEQ },
        { NJ_C_DELEM,   "DATALT",  DE_DATALT },
        { NJ_C_DELEM,   "URL",     DE_URL }
    };
    unsigned i;

    if (!vm) return NJ_ERR;
    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        nj_field *f = nj_field_lookup(vm, k[i].cls, k[i].name, "I", 1);
        if (f) nj_field_write_int(vm, NULL, f, k[i].val);
    }
    return NJ_OK;
}
