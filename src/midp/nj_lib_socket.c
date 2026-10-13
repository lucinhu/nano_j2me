/*
 * javax.microedition.io.SocketConnection - TCP client sockets, over one
 * optional set of port hooks.
 *
 * Why this exists at all: 183 of the 436 titles in the test corpus mention
 * javax.microedition.io.Connector and 99 of them use HttpConnection, but only
 * FOUR name SocketConnection, and none at all use ServerSocketConnection or
 * DatagramConnection.  So this is the client half only, and it is deliberately
 * the smallest thing that can be called a socket: connect, read, write, close.
 * A MIDlet that wants to be a server, or to send a datagram, gets the
 * ConnectionNotFoundException the specification asks for - by name, with the
 * URL in the message, which is a five-minute diagnosis instead of a silent
 * empty stream.
 *
 * Blocking reads are the port's business.  socket_read() is asked for a slice
 * of time at a time and the runtime keeps asking until the MIDlet's read is
 * satisfied, the peer closes, or a cap is reached.  A port that can wait
 * blocks in its own read; one that cannot returns 0 immediately and is asked
 * again.  What this runtime does NOT do is run other Java threads while a
 * native waits, because threads here are cooperative - a MIDlet that reads a
 * socket on its main thread stops drawing until the read returns, exactly as
 * it would if it read a file.  Reading on a background thread is the MIDlet's
 * answer to that, and it is the same answer as on a handset.
 */
#include "nj_midp.h"

#include <string.h>

#define NJFN(name) NJ_NATIVE_FN(name)

#define NJ_C_STREAMCONN "javax/microedition/io/StreamConnection"
#define NJ_C_SOCKCONN   "javax/microedition/io/SocketConnection"
#define NJ_C_SOCKIMPL   "javax/microedition/io/SocketImpl"
#define NJ_C_SOCKIN     "javax/microedition/io/SocketInputStream"
#define NJ_C_SOCKOUT    "javax/microedition/io/SocketOutputStream"
#define NJ_C_ISTREAM    "java/io/InputStream"
#define NJ_C_OSTREAM    "java/io/OutputStream"
#define NJ_C_DATAIN     "java/io/DataInputStream"
#define NJ_C_DATAOUT    "java/io/DataOutputStream"

/* SocketImpl: the object Connector.open() hands back. */
#define F_URL    "kUrl"        /* Ljava/lang/String;  the URL as asked */
#define F_HOST   "kHost"       /* Ljava/lang/String;  what the URL named */
#define F_PORT   "kPort"       /* I  what the URL named */
#define F_ADDR   "kAddr"       /* Ljava/lang/String;  the peer, from the port */
#define F_LADDR  "kLAddr"      /* Ljava/lang/String;  our end */
#define F_LPORT  "kLPort"      /* I  our port */
#define F_HANDLE "kHandle"     /* J  the port's handle; 0 = not connected */
#define F_IN     "kIn"         /* Ljava/io/InputStream; or NULL */
#define F_OUT    "kOut"        /* Ljava/io/OutputStream; or NULL */
#define F_CLOSED "kClosed"     /* Z */
#define F_OPTS   "kOpts"       /* [I  what setSocketOption() was given */

/* The two streams share a shape: which connection they belong to, and whether
 * they have been closed on their own. */
#define F_CONN   "sConn"       /* LSocketImpl */
#define F_SCLOSED "sClosed"    /* Z */

#define SOCKET_URL_MAX 512
#define SOCKET_HOST_MAX 256

/*
 * How long Connector.open() waits for the connect, how much of that a port is
 * asked for at a time, and how long one read() may take before it gives up
 * with an IOException.  The slice is short so that a port which can wait is
 * not asked to wait for the whole thing, and so that a port which cannot wait
 * is polled often enough to feel immediate.
 */
#define SOCK_CONNECT_MS 30000u
#define SOCK_SLICE_MS     250u
#define SOCK_READ_MS    30000u

#define SOCK_OPT_COUNT 5

/* ------------------------------------------------------------------ */
/* Field access                                                        */
/* ------------------------------------------------------------------ */

static nj_ref sget_ref(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void sset_ref(nj_vm *vm, nj_ref o, const char *cls,
                     const char *name, const char *desc, nj_ref v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static nj_s4 sget_int(nj_vm *vm, nj_ref o, const char *cls,
                      const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void sset_int(nj_vm *vm, nj_ref o, const char *cls,
                     const char *name, const char *desc, nj_s4 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_s8 sget_long(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_long(vm, o, f) : 0;
}

static void sset_long(nj_vm *vm, nj_ref o, const char *cls,
                      const char *name, const char *desc, nj_s8 v)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_long(vm, o, f, v);
    }
}

/* ------------------------------------------------------------------ */
/* The port interprets the URL, the runtime only checks its shape       */
/* ------------------------------------------------------------------ */

/*
 * "socket://host:port".  Everything after the port is refused rather than
 * ignored: a MIDlet that writes a path is expecting something this API does
 * not promise, and quietly connecting anyway is how a game spends a week
 * wondering why its request never arrived.
 *
 * The host is returned as written - a name or a literal address - and it is
 * also what getAddress() answers when the port has nothing better to say.
 */
static int socket_split(const char *url, char *host, int host_cap, int *port)
{
    const char *p = url;
    const char *colon;
    long v = 0;
    int n = 0;

    if (strncmp(p, "socket://", 9u) != 0) {
        return -1;
    }
    p += 9;
    colon = strchr(p, ':');
    if (colon == NULL || colon == p) {
        return -1;
    }
    if ((int)(colon - p) >= host_cap) {
        return -1;
    }
    while (p < colon) {
        host[n++] = *p++;
    }
    host[n] = '\0';
    p = colon + 1;
    if (*p < '0' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 65535) {
            return -1;
        }
        p++;
    }
    if (*p != '\0' || v <= 0) {
        return -1;
    }
    *port = (int)v;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Connecting                                                          */
/* ------------------------------------------------------------------ */

/*
 * The one refusal a port can cause.  When there is no hook the message names
 * the missing capability; when there is one and it declined, the port's own
 * sentence goes to the MIDlet, because only the port knows whether the host
 * was unknown, the port closed, or the radio was off.
 */
static void socket_refuse(nj_vm *vm, const char *text)
{
    (void)nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                   text);
}

nj_ref nj_socket_new(nj_vm *vm, const char *url)
{
    const nj_port *port = nj_vm_port(vm);
    nj_socket_info info;
    char err[256];
    char host[SOCKET_HOST_MAX];
    nj_ref o;
    nj_ref opts;
    int pnum = 0;
    nj_s4 *optv;
    int i;

    if (socket_split(url, host, (int)sizeof(host), &pnum) != 0) {
        socket_refuse(vm, url ? url : "(null)");
        return NULL;
    }
    if (port == NULL || port->socket_connect == NULL) {
        socket_refuse(vm, "this build has no socket support; "
                          "the port must supply it");
        return NULL;
    }

    memset(&info, 0, sizeof(info));
    err[0] = '\0';
    {
        void *h = port->socket_connect(port->ud, url, SOCK_CONNECT_MS, &info,
                                       err, (nj_size)sizeof(err));
        if (h == NULL) {
            socket_refuse(vm, err[0] ? err : url);
            return NULL;
        }
        o = nj_object_new(vm, NJ_C_SOCKIMPL);
        if (!o) {
            port->socket_close(port->ud, h);
            return NULL;
        }
        sset_long(vm, o, NJ_C_SOCKIMPL, F_HANDLE, "J", (nj_s8)(intptr_t)h);
    }

    sset_ref(vm, o, NJ_C_SOCKIMPL, F_URL, "Ljava/lang/String;",
             nj_string_new(vm, url));
    sset_ref(vm, o, NJ_C_SOCKIMPL, F_HOST, "Ljava/lang/String;",
             nj_string_new(vm, host));
    sset_int(vm, o, NJ_C_SOCKIMPL, F_PORT, "I", pnum);
    sset_ref(vm, o, NJ_C_SOCKIMPL, F_ADDR, "Ljava/lang/String;",
             nj_string_new(vm, info.address[0] ? info.address : host));
    sset_int(vm, o, NJ_C_SOCKIMPL, F_LPORT, "I", info.local_port);
    sset_ref(vm, o, NJ_C_SOCKIMPL, F_LADDR, "Ljava/lang/String;",
             nj_string_new(vm, info.local_address));
    sset_int(vm, o, NJ_C_SOCKIMPL, F_CLOSED, "Z", 0);

    /* -1 is "the MIDlet has not asked", and that is what getSocketOption()
     * answers for an option nobody set.  A value the runtime cannot pass on
     * is still remembered, so a MIDlet that sets and gets an option sees what
     * it set instead of a zero it never asked for. */
    opts = nj_array_new(vm, "I", SOCK_OPT_COUNT);
    if (opts) {
        optv = (nj_s4 *)nj_array_data(opts);
        for (i = 0; i < SOCK_OPT_COUNT; i++) {
            optv[i] = -1;
        }
        sset_ref(vm, o, NJ_C_SOCKIMPL, F_OPTS, "[I", opts);
    }
    return o;
}

/* ------------------------------------------------------------------ */
/* Reading and writing                                                 */
/* ------------------------------------------------------------------ */

static void socket_pause(nj_vm *vm, nj_u4 ms)
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

/*
 * Ask the port for up to `len` bytes, waiting `budget_ms` in total.
 *
 * Returns the byte count, -1 at end of stream, or -2 when the budget ran out.
 * The port's -1 and this function's timeout are told apart here, once, so that
 * the two callers above it do not have to guess: an end of stream reaches the
 * MIDlet as -1, a timeout as an IOException.
 */
static int socket_fill(nj_vm *vm, nj_s8 handle, nj_u1 *buf, int len,
                       nj_u4 budget_ms)
{
    const nj_port *port = nj_vm_port(vm);
    nj_u4 left = budget_ms;

    if (port == NULL || port->socket_read == NULL || handle == 0) {
        return -2;
    }
    for (;;) {
        nj_u4 slice = left < SOCK_SLICE_MS ? left : SOCK_SLICE_MS;
        int n = port->socket_read(port->ud, (void *)(intptr_t)handle, buf, len,
                                  slice);
        if (n != 0) {
            return n;               /* bytes, or -1 for "the peer closed" */
        }
        if (left <= slice) {
            return -2;
        }
        left -= slice;
        socket_pause(vm, 5u);
    }
}

/* The handle, or 0 after close(): a MIDlet that reads a closed socket gets an
 * IOException rather than a crash or a silent zero. */
static nj_s8 socket_handle(nj_vm *vm, nj_ref impl)
{
    if (impl == NULL) {
        return 0;
    }
    return sget_long(vm, impl, NJ_C_SOCKIMPL, F_HANDLE, "J");
}

static int socket_closed(nj_vm *vm, nj_ref impl)
{
    return impl != NULL && sget_int(vm, impl, NJ_C_SOCKIMPL, F_CLOSED, "Z") != 0;
}

/* The connection behind a stream object, plus a check that the stream itself
 * is still open.  0 means "raise IOException and stop". */
static nj_ref stream_conn(nj_vm *vm, const char *cls, nj_ref self,
                          const char *closed_field, const char *what)
{
    nj_ref conn = sget_ref(vm, self, cls, F_CONN, "L" NJ_C_SOCKIMPL ";");

    if (conn == NULL || sget_int(vm, self, cls, closed_field, "Z") != 0) {
        (void)nj_throw(vm, "java/io/IOException",
                       what[0] ? what : "the stream is closed");
        return NULL;
    }
    return conn;
}

static nj_status io_fail(nj_vm *vm, const char *what)
{
    return nj_throw(vm, "java/io/IOException", what);
}

/* ------------------------------------------------------------------ */
/* Natives: the connection                                             */
/* ------------------------------------------------------------------ */

static nj_ref stream_new(nj_vm *vm, const char *cls, nj_ref conn)
{
    nj_ref o = nj_object_new(vm, cls);

    if (o == NULL) {
        return NULL;
    }
    sset_ref(vm, o, cls, F_CONN, "L" NJ_C_SOCKIMPL ";", conn);
    sset_int(vm, o, cls, F_SCLOSED, "Z", 0);
    return o;
}

NJFN(socket_get_address)
{
    nj_ref s = sget_ref(vm, NJ_THIS(args), NJ_C_SOCKIMPL, F_ADDR,
                        "Ljava/lang/String;");
    ret->r = s ? s : nj_string_new(vm, "");
    return NJ_OK;
}

NJFN(socket_get_port)
{
    ret->i = sget_int(vm, NJ_THIS(args), NJ_C_SOCKIMPL, F_PORT, "I");
    return NJ_OK;
}

NJFN(socket_get_local_address)
{
    nj_ref s = sget_ref(vm, NJ_THIS(args), NJ_C_SOCKIMPL, F_LADDR,
                        "Ljava/lang/String;");
    ret->r = s ? s : nj_string_new(vm, "");
    return NJ_OK;
}

NJFN(socket_get_local_port)
{
    ret->i = sget_int(vm, NJ_THIS(args), NJ_C_SOCKIMPL, F_LPORT, "I");
    return NJ_OK;
}

NJFN(socket_set_option)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 which = NJ_ARG_INT(args, 0);
    nj_s4 value = NJ_ARG_INT(args, 1);
    nj_ref opts = sget_ref(vm, self, NJ_C_SOCKIMPL, F_OPTS, "[I");
    nj_s4 *p;

    if (which < 0 || which >= SOCK_OPT_COUNT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "no such socket option");
    }
    if (opts == NULL) {
        return NJ_OK;
    }
    p = (nj_s4 *)nj_array_data(opts);
    p[which] = value;
    /*
     * Kept, not forwarded: the hooks have no option call, and pretending to
     * set something the platform decides anyway would be a lie the MIDlet
     * cannot detect.  getSocketOption() answers what was set here.
     */
    return NJ_OK;
}

NJFN(socket_get_option)
{
    nj_s4 which = NJ_ARG_INT(args, 0);
    nj_ref opts = sget_ref(vm, NJ_THIS(args), NJ_C_SOCKIMPL, F_OPTS, "[I");
    const nj_s4 *p;

    if (which < 0 || which >= SOCK_OPT_COUNT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "no such socket option");
    }
    if (opts == NULL) {
        ret->i = -1;
        return NJ_OK;
    }
    p = (const nj_s4 *)nj_array_data(opts);
    ret->i = p[which];
    return NJ_OK;
}

NJFN(socket_open_input)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = sget_ref(vm, self, NJ_C_SOCKIMPL, F_IN,
                        "Ljava/io/InputStream;");

    if (socket_closed(vm, self)) {
        return io_fail(vm, "the connection is closed");
    }
    if (s == NULL) {
        s = stream_new(vm, NJ_C_SOCKIN, self);
        if (s == NULL) {
            return NJ_ERR;
        }
        sset_ref(vm, self, NJ_C_SOCKIMPL, F_IN, "Ljava/io/InputStream;", s);
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(socket_open_data_input)
{
    nj_value a[1];

    if (socket_open_input(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    a[0].r = ret->r;
    ret->r = nj_object_new(vm, NJ_C_DATAIN);
    if (ret->r == NULL) {
        return NJ_ERR;
    }
    return nj_call_special(vm, ret->r, NJ_C_DATAIN, "<init>",
                           "(Ljava/io/InputStream;)V", a, NULL);
}

NJFN(socket_open_output)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = sget_ref(vm, self, NJ_C_SOCKIMPL, F_OUT,
                        "Ljava/io/OutputStream;");

    if (socket_closed(vm, self)) {
        return io_fail(vm, "the connection is closed");
    }
    if (s == NULL) {
        s = stream_new(vm, NJ_C_SOCKOUT, self);
        if (s == NULL) {
            return NJ_ERR;
        }
        sset_ref(vm, self, NJ_C_SOCKIMPL, F_OUT, "Ljava/io/OutputStream;", s);
    }
    ret->r = s;
    return NJ_OK;
}

NJFN(socket_open_data_output)
{
    nj_value a[1];

    if (socket_open_output(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    a[0].r = ret->r;
    ret->r = nj_object_new(vm, NJ_C_DATAOUT);
    if (ret->r == NULL) {
        return NJ_ERR;
    }
    return nj_call_special(vm, ret->r, NJ_C_DATAOUT, "<init>",
                           "(Ljava/io/OutputStream;)V", a, NULL);
}

/*
 * Closing is idempotent and it closes both streams with it: a MIDlet that
 * closes the connection and then writes to a stream it kept a reference to
 * gets an IOException, not a write into a socket handle the port has already
 * released.  The port is told exactly once.
 */
NJFN(socket_close)
{
    nj_ref self = NJ_THIS(args);
    const nj_port *port = nj_vm_port(vm);
    nj_s8 h = sget_long(vm, self, NJ_C_SOCKIMPL, F_HANDLE, "J");

    if (h != 0 && port != NULL && port->socket_close != NULL) {
        port->socket_close(port->ud, (void *)(intptr_t)h);
    }
    sset_long(vm, self, NJ_C_SOCKIMPL, F_HANDLE, "J", 0);
    sset_int(vm, self, NJ_C_SOCKIMPL, F_CLOSED, "Z", 1);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Natives: the two streams                                            */
/* ------------------------------------------------------------------ */

/*
 * One read, however the MIDlet spelled it: read() wants a byte, read(b) and
 * read(b, off, len) want up to len.  They share this so that the timeout, the
 * end-of-stream answer and the exception text cannot drift apart.
 *
 * -2 is "an exception has been raised"; -1 is end of stream, which is a
 * perfectly normal answer and is passed on as it is.
 */
static int stream_read(nj_vm *vm, nj_ref self, nj_u1 *dst, int len)
{
    nj_ref conn = stream_conn(vm, NJ_C_SOCKIN, self, F_SCLOSED,
                              "the stream is closed");
    nj_s8 h;
    int n;

    if (conn == NULL) {
        return -2;                  /* exception already raised */
    }
    h = socket_handle(vm, conn);
    if (h == 0) {
        (void)io_fail(vm, "the connection is closed");
        return -2;
    }
    n = socket_fill(vm, h, dst, len, SOCK_READ_MS);
    if (n == -2) {
        (void)io_fail(vm, "the read timed out");
        return -2;
    }
    return n;
}

NJFN(socket_read_byte)
{
    nj_u1 b = 0;
    int n = stream_read(vm, NJ_THIS(args), &b, 1);

    if (n == -2) {
        return NJ_ERR;
    }
    ret->i = (n <= 0) ? -1 : (nj_s4)b;
    return NJ_OK;
}

NJFN(socket_read_bytes)
{
    nj_ref self = NJ_THIS(args);
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_u4 alen;
    int n;

    if (arr == NULL) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    alen = nj_array_length(arr);
    if (alen == 0u) {
        ret->i = 0;
        return NJ_OK;
    }
    n = stream_read(vm, self, (nj_u1 *)nj_array_data(arr), (int)alen);
    if (n == -2) {
        return NJ_ERR;
    }
    ret->i = (nj_s4)n;
    return NJ_OK;
}

NJFN(socket_read_range)
{
    nj_ref self = NJ_THIS(args);
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_u4 alen;
    nj_u1 *base;
    int n;

    if (arr == NULL) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    alen = nj_array_length(arr);
    if (off < 0 || len < 0 || (nj_u4)(off + len) > alen) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        ret->i = 0;
        return NJ_OK;
    }
    base = (nj_u1 *)nj_array_data(arr);
    n = stream_read(vm, self, base + off, len);
    if (n == -2) {
        return NJ_ERR;
    }
    ret->i = (nj_s4)n;
    return NJ_OK;
}

/*
 * skip() reads and throws away, which is the only honest implementation for a
 * socket: there is no way to move the peer's stream forward without taking
 * the bytes, and a port hook for "discard" would have to do the same thing in
 * the port.
 */
NJFN(socket_skip)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_u1 buf[256];
    nj_s8 done = 0;

    if (want <= 0) {
        ret->j = 0;
        return NJ_OK;
    }
    while (done < want) {
        int want_now = (want - done) > (nj_s8)sizeof(buf)
                           ? (int)sizeof(buf)
                           : (int)(want - done);
        int n = stream_read(vm, self, buf, want_now);
        if (n == -2) {
            return NJ_ERR;
        }
        if (n <= 0) {
            break;
        }
        done += n;
    }
    ret->j = done;
    return NJ_OK;
}

/*
 * available() is what has already arrived.  There is no hook for "how much is
 * waiting", and inventing one would mean a port that cannot answer reports a
 * number the MIDlet would believe; 0 is always true, and a MIDlet that needs
 * data calls read(), which waits.
 */
NJFN(socket_available)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(socket_stream_close)
{
    nj_ref self = NJ_THIS(args);
    nj_ref conn = sget_ref(vm, self, NJ_C_SOCKIN, F_CONN, "L" NJ_C_SOCKIMPL ";");

    if (conn == NULL) {
        conn = sget_ref(vm, self, NJ_C_SOCKOUT, F_CONN,
                        "L" NJ_C_SOCKIMPL ";");
    }
    /*
     * Closing a stream ends THAT stream.  The connection stays open, because
     * that is what the two halves of a socket are for - a MIDlet that wants
     * both ends gone closes the connection, and one that closes the input
     * half after reading a reply can still write.
     */
    (void)conn;
    if (nj_field_lookup(vm, NJ_C_SOCKIN, F_SCLOSED, "Z", 0) != NULL) {
        sset_int(vm, self, NJ_C_SOCKIN, F_SCLOSED, "Z", 1);
    }
    sset_int(vm, self, NJ_C_SOCKOUT, F_SCLOSED, "Z", 1);
    return NJ_OK;
}

static nj_status stream_write(nj_vm *vm, nj_ref self, const nj_u1 *src, int len)
{
    const nj_port *port = nj_vm_port(vm);
    nj_ref conn = stream_conn(vm, NJ_C_SOCKOUT, self, F_SCLOSED,
                              "the stream is closed");
    nj_s8 h;

    if (conn == NULL) {
        return NJ_ERR;
    }
    h = socket_handle(vm, conn);
    if (h == 0) {
        return io_fail(vm, "the connection is closed");
    }
    if (port == NULL || port->socket_write == NULL) {
        return io_fail(vm, "this build has no socket support");
    }
    if (port->socket_write(port->ud, (void *)(intptr_t)h, src, len) < 0) {
        return io_fail(vm, "the write failed");
    }
    return NJ_OK;
}

NJFN(socket_write_byte)
{
    nj_u1 b = (nj_u1)(NJ_ARG_INT(args, 0) & 0xFF);

    return stream_write(vm, NJ_THIS(args), &b, 1);
}

NJFN(socket_write_bytes)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_u4 alen;

    if (arr == NULL) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    alen = nj_array_length(arr);
    if (alen == 0u) {
        return NJ_OK;
    }
    return stream_write(vm, NJ_THIS(args), (const nj_u1 *)nj_array_data(arr),
                        (int)alen);
}

NJFN(socket_write_range)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_u4 alen;
    const nj_u1 *base;

    if (arr == NULL) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    alen = nj_array_length(arr);
    if (off < 0 || len < 0 || (nj_u4)(off + len) > alen) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        return NJ_OK;
    }
    base = (const nj_u1 *)nj_array_data(arr);
    return stream_write(vm, NJ_THIS(args), base + off, len);
}

/* There is nothing buffered to push: the runtime writes straight through, so
 * flush() has nothing to do and saying so is better than a fake buffer. */
NJFN(socket_flush)
{
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/*
 * The socket options are ints, so they are fields the class declares and this
 * seeds once per VM - the same shape as Connector's READ/WRITE/READ_WRITE.
 * They are also the indices setSocketOption()/getSocketOption() check, which
 * is why the values are 0..4 and not arbitrary.
 */
nj_status nj_socket_late_init(nj_vm *vm)
{
    static const struct {
        const char *name;
        nj_s4 value;
    } opts[SOCK_OPT_COUNT] = {
        { "DELAY",     0 },
        { "LINGER",    1 },
        { "KEEPALIVE", 2 },
        { "RCVBUF",    3 },
        { "SNDBUF",    4 }
    };
    int i;

    for (i = 0; i < SOCK_OPT_COUNT; i++) {
        nj_field *f = nj_field_lookup(vm, NJ_C_SOCKCONN, opts[i].name, "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, opts[i].value);
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field sockconn_fields[] = {
    FLD("DELAY",     "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("LINGER",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("KEEPALIVE", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("RCVBUF",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("SNDBUF",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL)
};

/*
 * SocketConnection declares the union of everything a MIDlet can call through
 * a SocketConnection reference, for the reason spelled out in
 * nj_lib_http.c: this runtime resolves an interface method in the interface's
 * own table and does not walk the interface chain, and a built-in class can
 * carry only one super.  A real MIDP SocketConnection inherits all of these
 * from Connection and StreamConnection.
 */
static const nj_builtin_method sockconn_methods[] = {
    M("close",                "()V", NJ_ACC_ABSTRACT),
    M("openInputStream",      "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream",  "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT),
    M("openOutputStream",     "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT),
    M("getAddress",           "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getPort",              "()I", NJ_ACC_ABSTRACT),
    M("getLocalAddress",      "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getLocalPort",         "()I", NJ_ACC_ABSTRACT),
    /* JSR 118 spells the option id as a byte: setSocketOption(byte,int)
     * and getSocketOption(byte).  Both descriptors are registered because a
     * MIDlet compiled against the int form exists in the wild (and in this
     * repository's own fixtures) and it must not become a NoSuchMethodError. */
    M("setSocketOption",      "(BI)V", NJ_ACC_ABSTRACT),
    M("setSocketOption",      "(II)V", NJ_ACC_ABSTRACT),
    M("getSocketOption",      "(B)I", NJ_ACC_ABSTRACT),
    M("getSocketOption",      "(I)I", NJ_ACC_ABSTRACT)
};

static const nj_builtin_field sockimpl_fields[] = {
    FLD(F_URL,    "Ljava/lang/String;", 0),
    FLD(F_HOST,   "Ljava/lang/String;", 0),
    FLD(F_PORT,   "I", 0),
    FLD(F_ADDR,   "Ljava/lang/String;", 0),
    FLD(F_LADDR,  "Ljava/lang/String;", 0),
    FLD(F_LPORT,  "I", 0),
    FLD(F_HANDLE, "J", 0),
    FLD(F_IN,     "Ljava/io/InputStream;", 0),
    FLD(F_OUT,    "Ljava/io/OutputStream;", 0),
    FLD(F_CLOSED, "Z", 0),
    FLD(F_OPTS,   "[I", 0)
};

static const nj_builtin_method sockimpl_methods[] = {
    M("getAddress",           "()Ljava/lang/String;", 0),
    M("getPort",              "()I", 0),
    M("getLocalAddress",      "()Ljava/lang/String;", 0),
    M("getLocalPort",         "()I", 0),
    M("setSocketOption",      "(BI)V", 0),
    M("setSocketOption",      "(II)V", 0),
    M("getSocketOption",      "(B)I", 0),
    M("getSocketOption",      "(I)I", 0),
    M("openInputStream",      "()Ljava/io/InputStream;", 0),
    M("openDataInputStream",  "()Ljava/io/DataInputStream;", 0),
    M("openOutputStream",     "()Ljava/io/OutputStream;", 0),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", 0),
    M("close",                "()V", 0)
};

static const nj_builtin_field sockstream_fields[] = {
    FLD(F_CONN,    "L" NJ_C_SOCKIMPL ";", 0),
    FLD(F_SCLOSED, "Z", 0)
};

/* The two stream classes are java.io.InputStream/OutputStream subclasses, so
 * a MIDlet can hand one to DataInputStream or to anything else that takes a
 * stream, and read() dispatches here like any other override. */
static const nj_builtin_method sockin_methods[] = {
    M("read",      "()I", 0),
    M("read",      "([B)I", 0),
    M("read",      "([BII)I", 0),
    M("skip",      "(J)J", 0),
    M("available", "()I", 0),
    M("close",     "()V", 0)
};

static const nj_builtin_method sockout_methods[] = {
    M("write", "([B)V", 0),
    M("write", "([BII)V", 0),
    M("write", "(I)V", 0),
    M("flush", "()V", 0),
    M("close", "()V", 0)
};

const nj_builtin_class nj_lib_socket_classes[] = {
    { NJ_C_SOCKCONN, NJ_C_STREAMCONN,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      sockconn_fields,
      (nj_u2)(sizeof(sockconn_fields) / sizeof(sockconn_fields[0])),
      sockconn_methods,
      (nj_u2)(sizeof(sockconn_methods) / sizeof(sockconn_methods[0])) },
    { NJ_C_SOCKIMPL, NJ_C_SOCKCONN, NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      sockimpl_fields,
      (nj_u2)(sizeof(sockimpl_fields) / sizeof(sockimpl_fields[0])),
      sockimpl_methods,
      (nj_u2)(sizeof(sockimpl_methods) / sizeof(sockimpl_methods[0])) },
    { NJ_C_SOCKIN, NJ_C_ISTREAM, NJ_ACC_PUBLIC,
      sockstream_fields,
      (nj_u2)(sizeof(sockstream_fields) / sizeof(sockstream_fields[0])),
      sockin_methods,
      (nj_u2)(sizeof(sockin_methods) / sizeof(sockin_methods[0])) },
    { NJ_C_SOCKOUT, NJ_C_OSTREAM, NJ_ACC_PUBLIC,
      sockstream_fields,
      (nj_u2)(sizeof(sockstream_fields) / sizeof(sockstream_fields[0])),
      sockout_methods,
      (nj_u2)(sizeof(sockout_methods) / sizeof(sockout_methods[0])) }
};
const int nj_lib_socket_class_count =
    (int)(sizeof(nj_lib_socket_classes) / sizeof(nj_lib_socket_classes[0]));

const nj_native_entry nj_lib_socket_table[] = {
    { NJ_C_SOCKIMPL, "getAddress",           "()Ljava/lang/String;", socket_get_address },
    { NJ_C_SOCKIMPL, "getPort",              "()I",                  socket_get_port },
    { NJ_C_SOCKIMPL, "getLocalAddress",      "()Ljava/lang/String;", socket_get_local_address },
    { NJ_C_SOCKIMPL, "getLocalPort",         "()I",                  socket_get_local_port },
    { NJ_C_SOCKIMPL, "setSocketOption",      "(BI)V",                socket_set_option },
    { NJ_C_SOCKIMPL, "setSocketOption",      "(II)V",                socket_set_option },
    { NJ_C_SOCKIMPL, "getSocketOption",      "(B)I",                 socket_get_option },
    { NJ_C_SOCKIMPL, "getSocketOption",      "(I)I",                 socket_get_option },
    { NJ_C_SOCKIMPL, "openInputStream",      "()Ljava/io/InputStream;", socket_open_input },
    { NJ_C_SOCKIMPL, "openDataInputStream",  "()Ljava/io/DataInputStream;", socket_open_data_input },
    { NJ_C_SOCKIMPL, "openOutputStream",     "()Ljava/io/OutputStream;", socket_open_output },
    { NJ_C_SOCKIMPL, "openDataOutputStream", "()Ljava/io/DataOutputStream;", socket_open_data_output },
    { NJ_C_SOCKIMPL, "close",                "()V",                  socket_close },
    { NJ_C_SOCKIN,   "read",                 "()I",                  socket_read_byte },
    { NJ_C_SOCKIN,   "read",                 "([B)I",                socket_read_bytes },
    { NJ_C_SOCKIN,   "read",                 "([BII)I",              socket_read_range },
    { NJ_C_SOCKIN,   "skip",                 "(J)J",                 socket_skip },
    { NJ_C_SOCKIN,   "available",            "()I",                  socket_available },
    { NJ_C_SOCKIN,   "close",                "()V",                  socket_stream_close },
    { NJ_C_SOCKOUT,  "write",                "(I)V",                 socket_write_byte },
    { NJ_C_SOCKOUT,  "write",                "([B)V",                socket_write_bytes },
    { NJ_C_SOCKOUT,  "write",                "([BII)V",              socket_write_range },
    { NJ_C_SOCKOUT,  "flush",                "()V",                  socket_flush },
    { NJ_C_SOCKOUT,  "close",                "()V",                  socket_stream_close }
};
const int nj_lib_socket_count =
    (int)(sizeof(nj_lib_socket_table) / sizeof(nj_lib_socket_table[0]));
