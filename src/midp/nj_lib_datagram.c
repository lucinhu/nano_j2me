/*
 * nano_j2me - nj_lib_datagram.c
 *
 * javax.microedition.io.DatagramConnection and javax.microedition.io.Datagram:
 * UDP sockets.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * MIDP 2.0 defines the datagram API, and Minecraft PE 0.6.1 is the title that
 * made it worth implementing: its LAN mode opens "datagram://:19132", sends
 * and receives RakNet packets through a DatagramConnection, and retries higher
 * ports when one is taken.  Without the scheme the game reports a caught
 * ConnectionNotFoundException and single-player continues.
 *
 * ---------------------------------------------------------------------------
 * BLOCKING RECEIVE ON A COOPERATIVE SCHEDULER
 * ---------------------------------------------------------------------------
 * receive() is specified as blocking, but this runtime's threads are
 * cooperative and a native that sleeps in C stops every Java thread, including
 * the frame pump - a network read that blocks for a second would freeze the
 * game for a second.  So receive() polls: with no datagram waiting it sets the
 * datagram's length to zero and returns through nj_thread_sleep(), which parks
 * this thread and hands the CPU to the others for a few milliseconds.  When it
 * resumes, receive() returns an empty datagram.  A MIDlet's receive loop then
 * simply asks again, and the frame pump keeps running in between.
 *
 * That is not a silent lie about the API: an empty datagram is a legal answer
 * from a datagram socket, and the titles that use this API already loop.  The
 * Minecraft receive loop in particular treats a zero-length read as nothing to
 * do and immediately calls receive() again.
 *
 * Sockets are opened with the C library, not through port hooks, for the same
 * reason javax.microedition.io.file does: every desktop port this runs on has
 * a socket API, and a freestanding port refuses the URL by name.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "nj_midp.h"

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_DGCONN    "javax/microedition/io/DatagramConnection"
#define NJ_C_DGIMPL    "javax/microedition/io/DatagramImpl"
#define NJ_C_DGRAM     "javax/microedition/io/Datagram"
#define NJ_C_DGCONNIMPL "javax/microedition/io/DatagramConnectionImpl"

/* Datagram object. */
#define F_D_DATA "dData"       /* [B  the buffer                        */
#define F_D_OFF  "dOffset"     /* I  where a read or write starts        */
#define F_D_LEN  "dLength"     /* I  bytes valid in the buffer           */
#define F_D_ADDR "dAddress"    /* Ljava/lang/String;  "datagram://h:p"   */

/* Connection object. */
#define F_C_FD     "kFd"       /* J  host socket, or -1 when closed      */
#define F_C_CLOSED "kClosed"   /* Z                                      */
#define F_C_PORT   "kPort"     /* I  the local port that was bound       */
#define F_C_TARGET "kTarget"   /* Ljava/lang/String;  default peer URL   */

#define DG_MAX_DATAGRAM 65507

/* ------------------------------------------------------------------ */
/* Native stubs when there is no C library                             */
/* ------------------------------------------------------------------ */
#if !NJ_USE_LIBC

static nj_status dg_no_libc(nj_vm *vm)
{
    return nj_throw(vm, "java/io/IOException",
                    "datagram sockets need a hosted C library");
}

NJFN(dg_unavailable) { return dg_no_libc(vm); }

nj_ref nj_datagram_new(nj_vm *vm, const char *url)
{
    (void)nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException", url);
    return NULL;
}

#else  /* NJ_USE_LIBC */

#  include <string.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>

/*
 * Strict _POSIX_C_SOURCE hides glibc's NI_MAXHOST/NI_MAXSERV (they live behind
 * __USE_MISC).  The values are fixed by getnameinfo()'s contract, so spell them
 * out rather than relax the feature-test macros for the whole file.
 */
#  ifndef NI_MAXHOST
#    define NI_MAXHOST 1025
#  endif
#  ifndef NI_MAXSERV
#    define NI_MAXSERV 32
#  endif

/* ------------------------------------------------------------------ */
/* Field helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_field *dgf(nj_vm *vm, const char *cls, const char *name,
                     const char *desc, int is_static)
{
    return nj_field_lookup(vm, cls, name, desc, is_static);
}

static nj_ref dget_ref(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                       const char *desc)
{
    nj_field *f = dgf(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static nj_s4 dget_int(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                      const char *desc)
{
    nj_field *f = dgf(vm, cls, name, desc, 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void dset_ref(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                     const char *desc, nj_ref v)
{
    nj_field *f = dgf(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static void dset_int(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                     const char *desc, nj_s4 v)
{
    nj_field *f = dgf(vm, cls, name, desc, 0);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static nj_s8 dget_fd(nj_vm *vm, nj_ref impl)
{
    nj_field *f = dgf(vm, NJ_C_DGCONNIMPL, F_C_FD, "J", 0);
    return f ? (nj_s8)nj_field_read_long(vm, impl, f) : -1;
}

static void dset_fd(nj_vm *vm, nj_ref impl, nj_s8 fd)
{
    nj_field *f = dgf(vm, NJ_C_DGCONNIMPL, F_C_FD, "J", 0);
    if (f) {
        nj_field_write_long(vm, impl, f, fd);
    }
}

static nj_status dg_closed(nj_vm *vm)
{
    return nj_throw(vm, "java/io/IOException", "the connection is closed");
}

/* ------------------------------------------------------------------ */
/* URLs                                                                */
/* ------------------------------------------------------------------ */
/*
 * "datagram://<host>:<port>", where the host may be empty (wildcard bind) and
 * the port may be empty (port 0).  IPv6 literals arrive in brackets:
 * "datagram://[::1]:19132".  Returns 0 on success; a URL with no colon at all
 * is refused rather than guessed at.
 */
static int dg_split(const char *url, char *host, nj_size host_cap,
                    char *port, nj_size port_cap)
{
    const char *p;
    nj_size n = 0;

    if (!url || strncmp(url, "datagram://", 11u) != 0) {
        return -1;
    }
    p = url + 11;
    if (*p == '[') {
        p++;
        while (*p && *p != ']' && n + 1u < host_cap) {
            host[n++] = *p++;
        }
        host[n] = '\0';
        if (*p != ']') {
            return -1;
        }
        p++;
        if (*p != ':') {
            return -1;
        }
        p++;
    } else {
        const char *colon = strrchr(p, ':');
        if (!colon) {
            return -1;
        }
        if ((nj_size)(colon - p) + 1u > host_cap) {
            return -1;
        }
        memcpy(host, p, (nj_size)(colon - p));
        host[colon - p] = '\0';
        p = colon + 1;
    }
    n = 0;
    while (*p && n + 1u < port_cap) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        port[n++] = *p++;
    }
    port[n] = '\0';
    return 0;
}

/* Fill `ss` from host/port.  With `passive` an empty host means INADDR_ANY. */
static int dg_resolve(const char *host, const char *port, int passive,
                      struct sockaddr_storage *ss, socklen_t *slen)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *ai;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (passive) {
        hints.ai_flags = AI_PASSIVE;
    }
    rc = getaddrinfo((host && host[0]) ? host : NULL,
                     (port && port[0]) ? port : "0", &hints, &res);
    if (rc != 0 || !res) {
        return -1;
    }
    for (ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_addrlen <= sizeof(*ss)) {
            memcpy(ss, ai->ai_addr, ai->ai_addrlen);
            *slen = (socklen_t)ai->ai_addrlen;
            freeaddrinfo(res);
            return 0;
        }
    }
    freeaddrinfo(res);
    return -1;
}

/*
 * Numeric host and port for a sockaddr, written here rather than through
 * getnameinfo().  lwIP has no NI_* flags and, on ESP-IDF, no getnameinfo()
 * implementation to link against at all - and the runtime has no need for one:
 * the address a datagram came from is already an address, so the numeric form
 * is the answer, and formatting it from the bytes avoids a resolver round trip
 * (and a DNS dependency) on every packet.
 */
static int dg_format_addr(const struct sockaddr_storage *ss,
                          char *host, nj_size hostcap,
                          char *serv, nj_size servcap)
{
    if (ss == NULL || host == NULL || serv == NULL) {
        return -1;
    }
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)(const void *)ss;
        const nj_u1 *b = (const nj_u1 *)(const void *)&a->sin_addr.s_addr;

        snprintf(host, (size_t)hostcap, "%u.%u.%u.%u",
                 (unsigned)b[0], (unsigned)b[1], (unsigned)b[2], (unsigned)b[3]);
        snprintf(serv, (size_t)servcap, "%u", (unsigned)ntohs(a->sin_port));
        return 0;
    }
#ifdef AF_INET6
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)(const void *)ss;
        const nj_u1 *b = (const nj_u1 *)(const void *)&a->sin6_addr;
        char *at = host;
        int i;

        if (hostcap < 46u) {     /* "ffff:ffff:...:ffff\0" is 40 bytes */
            return -1;
        }
        for (i = 0; i < 8; i++) {
            int n = snprintf(at, (size_t)(hostcap - (nj_size)(at - host)),
                             i ? ":%02x%02x" : "%02x%02x", b[i * 2], b[i * 2 + 1]);
            if (n <= 0) {
                return -1;
            }
            at += n;
        }
        snprintf(serv, (size_t)servcap, "%u", (unsigned)ntohs(a->sin6_port));
        return 0;
    }
#endif
    return -1;
}

/* "datagram://<numeric host>:<port>" for the address a packet came from. */
static nj_ref dg_address_string(nj_vm *vm, const struct sockaddr_storage *ss,
                                socklen_t slen)
{
    char host[64];
    char serv[16];
    char url[64 + 16 + 16];
    int n;

    (void)slen;
    if (dg_format_addr(ss, host, (nj_size)sizeof(host),
                       serv, (nj_size)sizeof(serv)) != 0) {
        return NULL;
    }
    if (strchr(host, ':')) {
        n = snprintf(url, sizeof(url), "datagram://[%s]:%s", host, serv);
    } else {
        n = snprintf(url, sizeof(url), "datagram://%s:%s", host, serv);
    }
    if (n <= 0 || (nj_size)n >= sizeof(url)) {
        return NULL;
    }
    return nj_string_new(vm, url);
}

/* ------------------------------------------------------------------ */
/* Datagram value object                                               */
/* ------------------------------------------------------------------ */
static nj_ref dg_make(nj_vm *vm, nj_ref data, nj_s4 off, nj_s4 len, nj_ref addr)
{
    nj_ref o = nj_object_new(vm, NJ_C_DGIMPL);

    if (!o) {
        return NULL;
    }
    dset_ref(vm, o, NJ_C_DGIMPL, F_D_DATA, "[B", data);
    dset_int(vm, o, NJ_C_DGIMPL, F_D_OFF, "I", off);
    dset_int(vm, o, NJ_C_DGIMPL, F_D_LEN, "I", len);
    dset_ref(vm, o, NJ_C_DGIMPL, F_D_ADDR, "Ljava/lang/String;", addr);
    return o;
}

/* Length/offset pair that fits `data`, clamped to what the array can hold. */
static void dg_region(nj_vm *vm, nj_ref dgram, nj_ref *data,
                      nj_s4 *off, nj_s4 *len)
{
    *data = dget_ref(vm, dgram, NJ_C_DGIMPL, F_D_DATA, "[B");
    *off = dget_int(vm, dgram, NJ_C_DGIMPL, F_D_OFF, "I");
    *len = dget_int(vm, dgram, NJ_C_DGIMPL, F_D_LEN, "I");
    if (*off < 0) {
        *off = 0;
    }
    if (*len < 0) {
        *len = 0;
    }
    if (*data && (nj_u4)*off > (*data)->length) {
        *off = 0;
        *len = 0;
    }
    if (*data && (nj_u4)(*off + *len) > (*data)->length) {
        *len = (*data)->length - *off;
    }
    if (!*data) {
        *off = 0;
        *len = 0;
    }
}

NJFN(dg_setData_range)
{
    nj_ref self = NJ_THIS(args), data = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1), len = NJ_ARG_INT(args, 2);

    if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)off > data->length ||
        (nj_u4)len > data->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "offset or length outside the buffer");
    }
    dset_ref(vm, self, NJ_C_DGIMPL, F_D_DATA, "[B", data);
    dset_int(vm, self, NJ_C_DGIMPL, F_D_OFF, "I", off);
    dset_int(vm, self, NJ_C_DGIMPL, F_D_LEN, "I", len);
    return NJ_OK;
}

NJFN(dg_setData)
{
    nj_ref data = NJ_ARG_REF(args, 0);
    nj_value a[4];

    if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    a[0].r = NJ_THIS(args);       /* the receiver sits before args[0] */
    a[1].r = data;
    a[2].i = 0;
    a[3].i = (nj_s4)data->length;
    return dg_setData_range(vm, th, &a[1], ret);
}

NJFN(dg_getData)        { ret->r = dget_ref(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_DATA, "[B"); return NJ_OK; }
NJFN(dg_getOffset)      { ret->i = dget_int(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_OFF, "I"); return NJ_OK; }
NJFN(dg_getLength)      { ret->i = dget_int(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_LEN, "I"); return NJ_OK; }
NJFN(dg_setLength)      { dset_int(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_LEN, "I", NJ_ARG_INT(args, 0)); return NJ_OK; }
NJFN(dg_getAddress)     { ret->r = dget_ref(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_ADDR, "Ljava/lang/String;"); return NJ_OK; }
NJFN(dg_setAddress)
{
    dset_ref(vm, NJ_THIS(args), NJ_C_DGIMPL, F_D_ADDR, "Ljava/lang/String;",
             NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* DatagramConnection                                                  */
/* ------------------------------------------------------------------ */

/* Every newDatagram overload funnels here; `have_addr` says whether the third
 * argument is a destination this call supplied. */
static nj_status dg_newDatagram_impl(nj_vm *vm, nj_value *args, nj_value *ret,
                                     nj_ref data, nj_s4 len, nj_s4 off,
                                     nj_ref addr)
{
    nj_ref d;

    if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)off > data->length ||
        (nj_u4)len > data->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "offset or length outside the buffer");
    }
    NJ_UNUSED(args);
    d = dg_make(vm, data, off, len, addr);
    if (!d) {
        return nj_throw_oom(vm);
    }
    ret->r = d;
    return NJ_OK;
}

/* newDatagram(byte[] buffer, int length) */
NJFN(dg_newDatagram_data_len)
{
    return dg_newDatagram_impl(vm, args, ret, NJ_ARG_REF(args, 0),
                               NJ_ARG_INT(args, 1), 0, NULL);
}

/* newDatagram(byte[] buffer, int length, String address) */
NJFN(dg_newDatagram_data_len_addr)
{
    return dg_newDatagram_impl(vm, args, ret, NJ_ARG_REF(args, 0),
                               NJ_ARG_INT(args, 1), 0, NJ_ARG_REF(args, 2));
}

/* newDatagram(int length) */
NJFN(dg_newDatagram_len)
{
    nj_ref data = nj_byte_array_new(vm, (nj_u4)NJ_ARG_INT(args, 0));

    if (!data) {
        return nj_throw_oom(vm);
    }
    return dg_newDatagram_impl(vm, args, ret, data,
                               (nj_s4)data->length, 0, NULL);
}

/* newDatagram(int length, String address) */
NJFN(dg_newDatagram_len_addr)
{
    nj_ref data = nj_byte_array_new(vm, (nj_u4)NJ_ARG_INT(args, 0));

    if (!data) {
        return nj_throw_oom(vm);
    }
    return dg_newDatagram_impl(vm, args, ret, data,
                               (nj_s4)data->length, 0, NJ_ARG_REF(args, 1));
}

NJFN(dg_send)
{
    nj_ref self = NJ_THIS(args), dgram = NJ_ARG_REF(args, 0);
    nj_ref data, addr;
    nj_s4 off = 0, len = 0;
    nj_s8 fd;
    char url[NI_MAXHOST + NI_MAXSERV + 16];
    char host[NI_MAXHOST], port[NI_MAXSERV];
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int sent = 0;

    if (dget_int(vm, self, NJ_C_DGCONNIMPL, F_C_CLOSED, "Z")) {
        return dg_closed(vm);
    }
    if (!dgram) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    fd = dget_fd(vm, self);
    if (fd < 0) {
        return dg_closed(vm);
    }
    addr = dget_ref(vm, dgram, NJ_C_DGIMPL, F_D_ADDR, "Ljava/lang/String;");
    dg_region(vm, dgram, &data, &off, &len);
    if (nj_string_to_utf8(addr, url, (int)sizeof(url)) <= 0) {
        nj_ref target = dget_ref(vm, self, NJ_C_DGCONNIMPL, F_C_TARGET,
                                 "Ljava/lang/String;");
        if (!target || nj_string_to_utf8(target, url, (int)sizeof(url)) <= 0) {
            return nj_throw(vm, "java/io/IOException", "no destination address");
        }
    }
    if (dg_split(url, host, sizeof(host), port, sizeof(port)) != 0 ||
        dg_resolve(host, port, 0, &ss, &slen) != 0) {
        return nj_throw(vm, "java/io/IOException", "invalid destination address");
    }
    {
        struct addrinfo hints;
        struct addrinfo *res = NULL, *ai;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host[0] ? host : NULL, port, &hints, &res) != 0) {
            return nj_throw(vm, "java/io/IOException", "cannot resolve the address");
        }
        for (ai = res; ai; ai = ai->ai_next) {
            ssize_t n = sendto((int)fd,
                               data && len > 0 ? (const void *)(data->u.bytes + off) : "",
                               (size_t)(len > 0 ? len : 0), 0,
                               ai->ai_addr, ai->ai_addrlen);
            if (n >= 0) {
                sent = 1;
                break;
            }
        }
        freeaddrinfo(res);
    }
    if (!sent) {
        return nj_throw(vm, "java/io/IOException", "cannot send the datagram");
    }
    return NJ_OK;
}

NJFN(dg_receive)
{
    nj_ref self = NJ_THIS(args), dgram = NJ_ARG_REF(args, 0);
    nj_ref data = NULL, addr = NULL;
    nj_s4 off = 0, len = 0, cap = 0;
    nj_s8 fd;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    nj_u1 scratch[2048];
    ssize_t n;

    if (dget_int(vm, self, NJ_C_DGCONNIMPL, F_C_CLOSED, "Z")) {
        return dg_closed(vm);
    }
    if (!dgram) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    fd = dget_fd(vm, self);
    if (fd < 0) {
        return dg_closed(vm);
    }
    dg_region(vm, dgram, &data, &off, &len);
    cap = len;
    memset(&ss, 0, sizeof(ss));
    n = recvfrom((int)fd, cap > 0 ? (void *)(data->u.bytes + off) : (void *)scratch,
                 cap > 0 ? (size_t)cap : sizeof(scratch), 0,
                 (struct sockaddr *)&ss, &slen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            /*
             * Nothing waiting.  Report an empty datagram and let this thread
             * sleep for a few milliseconds so the frame pump and every other
             * MIDlet thread run in the meantime; on resume the MIDlet's loop
             * calls receive() again.  This is the cooperative version of the
             * blocking receive the specification describes.
             */
            dset_int(vm, dgram, NJ_C_DGIMPL, F_D_LEN, "I", 0);
            return nj_thread_sleep(vm, th, 3);
        }
        return nj_throw(vm, "java/io/IOException", "cannot receive a datagram");
    }
    if (cap <= 0) {
        dset_int(vm, dgram, NJ_C_DGIMPL, F_D_LEN, "I", 0);
    } else {
        dset_int(vm, dgram, NJ_C_DGIMPL, F_D_LEN, "I",
                 (nj_s4)((size_t)n > (size_t)cap ? cap : (nj_s4)n));
    }
    {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref droot = nj_root(vm, dgram);
        nj_ref drootdata = nj_root(vm, data);
        addr = nj_root(vm, dg_address_string(vm, &ss, slen));
        if (addr && droot && drootdata) {
            dset_ref(vm, droot, NJ_C_DGIMPL, F_D_ADDR, "Ljava/lang/String;", addr);
        }
        nj_root_release(vm, mark);
    }
    return NJ_OK;
}

NJFN(dg_close)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 fd = dget_fd(vm, self);

    if (fd >= 0) {
        close((int)fd);
        dset_fd(vm, self, -1);
    }
    dset_int(vm, self, NJ_C_DGCONNIMPL, F_C_CLOSED, "Z", 1);
    return NJ_OK;
}

NJFN(dg_getMaximumLength) { ret->i = DG_MAX_DATAGRAM; return NJ_OK; }
NJFN(dg_getNominalLength) { ret->i = DG_MAX_DATAGRAM; return NJ_OK; }

NJFN(dg_get_local_port)
{
    ret->i = dget_int(vm, NJ_THIS(args), NJ_C_DGCONNIMPL, F_C_PORT, "I");
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Connector.open("datagram://...")                                    */
/* ------------------------------------------------------------------ */
nj_ref nj_datagram_new(nj_vm *vm, const char *url)
{
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    int fd = -1;
    nj_ref impl, target = NULL;
    struct sockaddr_storage bound;
    socklen_t bound_len = sizeof(bound);
    int local_port = 0;

    if (!url || dg_split(url, host, sizeof(host), port, sizeof(port)) != 0 ||
        dg_resolve(host, port, 1, &ss, &slen) != 0) {
        (void)nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                       url ? url : "(null)");
        return NULL;
    }
    fd = socket(((struct sockaddr *)&ss)->sa_family, SOCK_DGRAM, 0);
    if (fd < 0) {
        (void)nj_throw(vm, "java/io/IOException", "cannot create a datagram socket");
        return NULL;
    }
    {
        int one = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        /* RakNet announces a LAN game by broadcasting its ping; a socket
         * without SO_BROADCAST is refused with EACCES on sendto(). */
        (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    }
    if (bind(fd, (struct sockaddr *)&ss, slen) != 0) {
        close(fd);
        (void)nj_throw(vm, "java/io/IOException",
                       "cannot bind the datagram socket (port in use?)");
        return NULL;
    }
    /* Non-blocking: receive() polls, it must never park the whole VM in C. */
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
    if (getsockname(fd, (struct sockaddr *)&bound, &bound_len) == 0) {
        if (bound.ss_family == AF_INET) {
            local_port = ntohs(((struct sockaddr_in *)&bound)->sin_port);
        } else if (bound.ss_family == AF_INET6) {
            local_port = ntohs(((struct sockaddr_in6 *)&bound)->sin6_port);
        }
    }
    /* A URL that names a peer is also the destination send() uses when the
     * datagram itself carries no address. */
    if (host[0] && port[0] && strcmp(port, "0") != 0) {
        char url_buf[NI_MAXHOST + NI_MAXSERV + 16];
        if (strchr(host, ':')) {
            (void)snprintf(url_buf, sizeof(url_buf), "datagram://[%s]:%s",
                           host, port);
        } else {
            (void)snprintf(url_buf, sizeof(url_buf), "datagram://%s:%s",
                           host, port);
        }
        target = nj_string_new(vm, url_buf);
    }
    impl = nj_object_new(vm, NJ_C_DGCONNIMPL);
    if (!impl) {
        close(fd);
        (void)nj_throw_oom(vm);
        return NULL;
    }
    dset_fd(vm, impl, (nj_s8)fd);
    dset_int(vm, impl, NJ_C_DGCONNIMPL, F_C_CLOSED, "Z", 0);
    dset_int(vm, impl, NJ_C_DGCONNIMPL, F_C_PORT, "I", local_port);
    if (target) {
        dset_ref(vm, impl, NJ_C_DGCONNIMPL, F_C_TARGET, "Ljava/lang/String;", target);
    }
    return impl;
}

#endif /* NJ_USE_LIBC */

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

/* The interface declares the members; DatagramImpl carries the natives.  A
 * declared-but-unbound instance method is what test_natives rejects, so the
 * interface rows are marked abstract exactly as the specification has them. */
static const nj_builtin_method dgram_iface_methods[] = {
    M("setData",   "([B)V", NJ_ACC_ABSTRACT),
    M("setData",   "([BII)V", NJ_ACC_ABSTRACT),
    M("getData",   "()[B", NJ_ACC_ABSTRACT),
    M("getOffset", "()I", NJ_ACC_ABSTRACT),
    M("getLength", "()I", NJ_ACC_ABSTRACT),
    M("setLength", "(I)V", NJ_ACC_ABSTRACT),
    M("setAddress", "(Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("getAddress", "()Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method dgram_methods[] = {
    M("setData",   "([B)V", 0),
    M("setData",   "([BII)V", 0),
    M("getData",   "()[B", 0),
    M("getOffset", "()I", 0),
    M("getLength", "()I", 0),
    M("setLength", "(I)V", 0),
    M("setAddress", "(Ljava/lang/String;)V", 0),
    M("getAddress", "()Ljava/lang/String;", 0)
};

static const nj_builtin_field dgramimpl_fields[] = {
    FLD(F_D_DATA, "[B", 0),
    FLD(F_D_OFF,  "I", 0),
    FLD(F_D_LEN,  "I", 0),
    FLD(F_D_ADDR, "Ljava/lang/String;", 0)
};

static const nj_builtin_method dgconn_methods[] = {
    M("newDatagram", "([BI)L" NJ_C_DGRAM ";", NJ_ACC_ABSTRACT),
    M("newDatagram", "([BILjava/lang/String;)L" NJ_C_DGRAM ";", NJ_ACC_ABSTRACT),
    M("newDatagram", "(I)L" NJ_C_DGRAM ";", NJ_ACC_ABSTRACT),
    M("newDatagram", "(ILjava/lang/String;)L" NJ_C_DGRAM ";", NJ_ACC_ABSTRACT),
    M("send", "(L" NJ_C_DGRAM ";)V", NJ_ACC_ABSTRACT),
    M("receive", "(L" NJ_C_DGRAM ";)V", NJ_ACC_ABSTRACT),
    M("getMaximumLength", "()I", NJ_ACC_ABSTRACT),
    M("getNominalLength", "()I", NJ_ACC_ABSTRACT),
    M("close", "()V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_field dgimpl_fields[] = {
    FLD(F_C_FD,     "J", 0),
    FLD(F_C_CLOSED, "Z", 0),
    FLD(F_C_PORT,   "I", 0),
    FLD(F_C_TARGET, "Ljava/lang/String;", 0)
};

static const nj_builtin_method dgimpl_methods[] = {
    M("newDatagram", "([BI)L" NJ_C_DGRAM ";", 0),
    M("newDatagram", "([BILjava/lang/String;)L" NJ_C_DGRAM ";", 0),
    M("newDatagram", "(I)L" NJ_C_DGRAM ";", 0),
    M("newDatagram", "(ILjava/lang/String;)L" NJ_C_DGRAM ";", 0),
    M("send", "(L" NJ_C_DGRAM ";)V", 0),
    M("receive", "(L" NJ_C_DGRAM ";)V", 0),
    M("getMaximumLength", "()I", 0),
    M("getNominalLength", "()I", 0),
    M("getLocalPort", "()I", 0),
    M("close", "()V", 0)
};

const nj_builtin_class nj_lib_datagram_classes[] = {
    { NJ_C_DGRAM, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, dgram_iface_methods,
      (nj_u2)(sizeof(dgram_iface_methods) / sizeof(dgram_iface_methods[0])) },
    { NJ_C_DGIMPL, NJ_C_DGRAM, NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      dgramimpl_fields,
      (nj_u2)(sizeof(dgramimpl_fields) / sizeof(dgramimpl_fields[0])),
      dgram_methods,
      (nj_u2)(sizeof(dgram_methods) / sizeof(dgram_methods[0])) },
    { NJ_C_DGCONN, "javax/microedition/io/Connection",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, dgconn_methods,
      (nj_u2)(sizeof(dgconn_methods) / sizeof(dgconn_methods[0])) },
    { NJ_C_DGCONNIMPL, NJ_C_DGCONN, NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      dgimpl_fields,
      (nj_u2)(sizeof(dgimpl_fields) / sizeof(dgimpl_fields[0])),
      dgimpl_methods,
      (nj_u2)(sizeof(dgimpl_methods) / sizeof(dgimpl_methods[0])) }
};
const int nj_lib_datagram_class_count =
    (int)(sizeof(nj_lib_datagram_classes) / sizeof(nj_lib_datagram_classes[0]));

const nj_native_entry nj_lib_datagram_table[] = {
#if NJ_USE_LIBC
    { NJ_C_DGCONNIMPL, "newDatagram", "([BI)L" NJ_C_DGRAM ";", dg_newDatagram_data_len },
    { NJ_C_DGCONNIMPL, "newDatagram", "([BILjava/lang/String;)L" NJ_C_DGRAM ";", dg_newDatagram_data_len_addr },
    { NJ_C_DGCONNIMPL, "newDatagram", "(I)L" NJ_C_DGRAM ";", dg_newDatagram_len },
    { NJ_C_DGCONNIMPL, "newDatagram", "(ILjava/lang/String;)L" NJ_C_DGRAM ";", dg_newDatagram_len_addr },
    { NJ_C_DGCONNIMPL, "send", "(L" NJ_C_DGRAM ";)V", dg_send },
    { NJ_C_DGCONNIMPL, "receive", "(L" NJ_C_DGRAM ";)V", dg_receive },
    { NJ_C_DGCONNIMPL, "getMaximumLength", "()I", dg_getMaximumLength },
    { NJ_C_DGCONNIMPL, "getNominalLength", "()I", dg_getNominalLength },
    { NJ_C_DGCONNIMPL, "getLocalPort", "()I", dg_get_local_port },
    { NJ_C_DGCONNIMPL, "close", "()V", dg_close },
    { NJ_C_DGIMPL, "setData", "([B)V", dg_setData },
    { NJ_C_DGIMPL, "setData", "([BII)V", dg_setData_range },
    { NJ_C_DGIMPL, "getData", "()[B", dg_getData },
    { NJ_C_DGIMPL, "getOffset", "()I", dg_getOffset },
    { NJ_C_DGIMPL, "getLength", "()I", dg_getLength },
    { NJ_C_DGIMPL, "setLength", "(I)V", dg_setLength },
    { NJ_C_DGIMPL, "setAddress", "(Ljava/lang/String;)V", dg_setAddress },
    { NJ_C_DGIMPL, "getAddress", "()Ljava/lang/String;", dg_getAddress },
#else
    { NJ_C_DGCONNIMPL, "newDatagram", "([BI)L" NJ_C_DGRAM ";", dg_unavailable },
    { NJ_C_DGCONNIMPL, "send", "(L" NJ_C_DGRAM ";)V", dg_unavailable },
    { NJ_C_DGCONNIMPL, "receive", "(L" NJ_C_DGRAM ";)V", dg_unavailable },
    { NJ_C_DGIMPL, "setData", "([B)V", dg_unavailable },
#endif
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_datagram_count =
    (int)(sizeof(nj_lib_datagram_table) / sizeof(nj_lib_datagram_table[0])) - 1;
