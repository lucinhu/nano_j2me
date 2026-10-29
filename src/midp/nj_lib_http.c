/*
 * nano_j2me - nj_lib_http.c
 *
 * javax.microedition.io.HttpConnection.
 *
 * WHY THIS EXISTS.  MIDP 2.0 makes HttpConnection mandatory, and it is the
 * one network API MIDlets really use: 99 of the 396 titles in tests/games
 * reference javax/microedition/io/HttpConnection - high-score tables, ads,
 * "check for updates" screens.  Until now every one of them died at
 * Connector.open() with ConnectionNotFoundException, which is correct for a
 * bare-metal target but wrong for a phone.
 *
 * WHERE THE BYTES COME FROM.  This runtime never opens a socket.  A port with
 * a network stack fills nj_port.http_request and this module calls it once per
 * request; a port without one leaves the hook NULL and "http://" is refused by
 * name, exactly as before.  The whole response is buffered because that is the
 * shape the MIDP API has anyway: a MIDlet asks for a response code and then
 * reads a stream.
 *
 * WHEN THE REQUEST GOES OUT.  The specification says the request is sent by
 * the first call that needs a response - getResponseCode(), getHeaderField(),
 * openInputStream() and friends - and that the method and headers must be set
 * before that.  This implementation follows that: setRequestMethod() and
 * setRequestProperty() throw IOException once the request has been sent, and
 * everything that reads a response calls http_send() first.
 *
 * WHAT IS DELIBERATELY MISSING.  Chunked transfer decoding, cookies, proxies,
 * HTTPS certificate policy and redirect handling all belong to the port's
 * stack, not here: the runtime sees a status code, a header block and a body.
 * getDate()/getExpiration()/getLastModified() return -1 rather than parsing
 * the header text, because a wrong date is worse than no date and nothing in
 * the corpus asks for one.
 */
#include "nj_midp.h"

#include <string.h>

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))

/* Copy a C string into a fixed buffer, always NUL-terminating. */
static void http_copy_str(char *dst, int cap, const char *src)
{
    int n = 0;
    if (!dst || cap <= 0) {
        return;
    }
    if (src) {
        while (src[n] && n < cap - 1) {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

#define NJ_C_CONNECTION  "javax/microedition/io/Connection"
#define NJ_C_INPUTCONN   "javax/microedition/io/InputConnection"
#define NJ_C_OUTPUTCONN  "javax/microedition/io/OutputConnection"
#define NJ_C_STREAMCONN  "javax/microedition/io/StreamConnection"
#define NJ_C_CONTENTCONN "javax/microedition/io/ContentConnection"
#define NJ_C_HTTPCONN    "javax/microedition/io/HttpConnection"

/*
 * The object a MIDlet actually holds.  It is not part of MIDP - nothing in a
 * JAR names it - but the runtime needs a class it can put fields on, and the
 * interfaces above carry no state.  Its super is HttpConnection so that the
 * cast every MIDlet writes, `(HttpConnection) Connector.open(url)`, succeeds:
 * a built-in class has no constant-pool interface list, so is-a is spelled as
 * the interface being the super (see implements_interface() in nj_class.c).
 */
#define NJ_C_HTTPIMPL    "javax/microedition/io/HttpImpl"

/*
 * The HTTPS object is a SECOND implementation class, not the same one with an
 * extra interface.  This runtime cannot give a built-in class an interface
 * list, and a super chain can spell only one is-a, so sharing one class would
 * make `c instanceof HttpsConnection` true for an http:// connection -
 * 急速公路狂飙－新年版_84 performs exactly that test on every request and
 * would take its secure-path branch on a plain socket.  Two classes with one
 * chain each keep the answer honest: HttpImpl is an HttpConnection, HttpsImpl
 * is an HttpsConnection and, through HttpsConnection's super, an
 * HttpConnection as well.
 */
#define NJ_C_HTTPSIMPL   "javax/microedition/io/HttpsImpl"
#define NJ_C_HTTPS       "javax/microedition/io/HttpsConnection"
#define NJ_C_SECINFO     "javax/microedition/io/SecurityInfo"
#define NJ_C_SECURECONN  "javax/microedition/io/SecureConnection"
#define NJ_C_CERT        "javax/microedition/pki/Certificate"

#define NJ_C_BAOS        "java/io/ByteArrayOutputStream"
#define NJ_C_BAIS        "java/io/ByteArrayInputStream"
#define NJ_C_DIS         "java/io/DataInputStream"
#define NJ_C_DOS         "java/io/DataOutputStream"

/* Fields.  The names are part of this module's ABI only. */
#define F_URL     "hUrl"       /* Ljava/lang/String;  as opened */
#define F_METHOD  "hMethod"    /* Ljava/lang/String;  "GET" */
#define F_REQHDR  "hReqHdr"    /* Ljava/lang/String;  request headers as text */
#define F_OUT     "hOut"       /* Ljava/io/ByteArrayOutputStream; or NULL */
#define F_BODY    "hBody"      /* [B  the POST body while the port call runs */
#define F_SENT    "hSent"      /* Z  the request has gone out */
#define F_STATUS  "hStatus"    /* I  response code */
#define F_RESP    "hResp"      /* [B or NULL */
#define F_RESPHDR "hRespHdr"   /* Ljava/lang/String; or NULL */
#define F_STREAM  "hStream"    /* Ljava/io/InputStream; or NULL */
#define F_CLOSED  "hClosed"    /* Z */

#define HTTP_URL_MAX   512
#define HTTP_HDR_MAX   2048
#define HTTP_METHOD_MAX 16

/* ------------------------------------------------------------------ */
/* Field access                                                        */
/* ------------------------------------------------------------------ */

/*
 * The state fields are declared on BOTH implementation classes (each needs its
 * own copy, because a field index is relative to its declaring class's layout),
 * so the lookup has to follow the object's actual class.  The two names differ,
 * so one comparison decides it - a failed lookup followed by a second one would
 * put that cost on every http:// field access instead.
 */
static nj_field *hfld(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    const char *cn = o ? nj_ref_class_name(o) : NULL;

    if (cn != NULL && strcmp(cn, NJ_C_HTTPSIMPL) == 0) {
        return nj_field_lookup(vm, NJ_C_HTTPSIMPL, name, desc, 0);
    }
    return nj_field_lookup(vm, NJ_C_HTTPIMPL, name, desc, 0);
}

static nj_ref hget_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = hfld(vm, o, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void hset_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                     nj_ref v)
{
    nj_field *f = hfld(vm, o, name, desc);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static nj_s4 hget_int(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = hfld(vm, o, name, desc);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void hset_int(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                     nj_s4 v)
{
    nj_field *f = hfld(vm, o, name, desc);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

/* ------------------------------------------------------------------ */
/* Text helpers                                                        */
/* ------------------------------------------------------------------ */

/* The runtime's tracked allocator, in the shape the port hook wants. */
static void *http_alloc(void *aud, nj_u4 n)
{
    return nj_host_alloc((nj_vm *)aud, n);
}

/*
 * Copy the modified-UTF-8 form of a String into a C buffer.  Returns 0 when
 * there is no string at all, so callers can tell "empty" from "absent".
 */
static int http_text(nj_ref s, char *out, int cap)
{
    out[0] = '\0';
    if (!s) {
        return 0;
    }
    return nj_string_to_utf8(s, out, cap);
}

/*
 * Split a URL into scheme, host, port and path.  Returns 0 on success.
 *
 * The port hook gets the URL as text, so this exists for the accessors
 * (getHost, getPort, getFile, getQuery, getRef) rather than for the request
 * itself.  A URL with no scheme, or one that is not http/https, is refused -
 * the MIDlet reached this object through Connector.open("http://...") and
 * anything else is a bug worth reporting rather than guessing at.
 */
static int http_split(const char *url, char *scheme, int scap,
                      char *host, int hcap, int *port,
                      char *file, int fcap, char *query, int qcap,
                      char *ref, int rcap)
{
    const char *p = url;
    const char *slash;
    const char *q;
    const char *hash;
    int n;

    scheme[0] = host[0] = file[0] = query[0] = ref[0] = '\0';
    *port = -1;
    if (!url) {
        return -1;
    }
    slash = strstr(p, "://");
    if (!slash) {
        return -1;
    }
    n = (int)(slash - p);
    if (n <= 0 || n >= scap) {
        return -1;
    }
    memcpy(scheme, p, (size_t)n);
    scheme[n] = '\0';
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) {
        return -1;
    }
    p = slash + 3;

    /* authority ends at the first '/', '?' or '#' */
    slash = p;
    while (*slash && *slash != '/' && *slash != '?' && *slash != '#') {
        slash++;
    }
    n = (int)(slash - p);
    if (n <= 0 || n >= hcap) {
        return -1;
    }
    memcpy(host, p, (size_t)n);
    host[n] = '\0';
    p = slash;

    /* optional :port in the authority */
    {
        char *colon = strchr(host, ':');
        if (colon) {
            *colon = '\0';
            *port = 0;
            for (colon++; *colon; colon++) {
                if (*colon < '0' || *colon > '9') {
                    return -1;
                }
                *port = *port * 10 + (*colon - '0');
            }
        } else {
            *port = (strcmp(scheme, "https") == 0) ? 443 : 80;
        }
    }
    if (host[0] == '\0') {
        return -1;
    }

    /* fragment */
    hash = strchr(p, '#');
    if (hash) {
        http_copy_str(ref, rcap, hash + 1);
    }
    /* query */
    q = strchr(p, '?');
    if (q && (!hash || q < hash)) {
        int qn = (int)((hash ? hash : (q + strlen(q))) - (q + 1));
        if (qn > 0 && qn < qcap) {
            memcpy(query, q + 1, (size_t)qn);
            query[qn] = '\0';
        }
    }
    /* path */
    {
        const char *end = q ? q : (hash ? hash : (p + strlen(p)));
        int pn = (int)(end - p);
        if (pn >= fcap) {
            return -1;
        }
        memcpy(file, p, (size_t)pn);
        file[pn] = '\0';
    }
    if (file[0] == '\0') {
        http_copy_str(file, fcap, "/");
    }
    return 0;
}

/*
 * The reason phrase for a status code.  Only the codes a MIDlet is likely to
 * branch on get a real sentence; anything else reports the number, which is
 * what the specification allows a client to do.
 */
static const char *http_reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default:  return "";
    }
}

/* Case-insensitive search for one header in a "Name: value\n" block. */
static const char *http_header_find(const char *block, const char *name)
{
    size_t nlen;
    const char *p;

    if (!block || !name) {
        return NULL;
    }
    nlen = strlen(name);
    for (p = block; *p; ) {
        const char *eol = strchr(p, '\n');
        const char *colon = strchr(p, ':');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if (colon && colon < p + len) {
            size_t klen = (size_t)(colon - p);
            if (klen == nlen) {
                size_t i;
                int same = 1;
                for (i = 0u; i < klen; i++) {
                    char a = p[i];
                    char b = name[i];
                    if (a >= 'A' && a <= 'Z') {
                        a = (char)(a - 'A' + 'a');
                    }
                    if (b >= 'A' && b <= 'Z') {
                        b = (char)(b - 'A' + 'a');
                    }
                    if (a != b) {
                        same = 0;
                        break;
                    }
                }
                if (same) {
                    const char *v = colon + 1;
                    while (*v == ' ' || *v == '\t') {
                        v++;
                    }
                    return v;   /* caller copies: the block is not NUL-terminated
                                 * at the end of this value */
                }
            }
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    return NULL;
}

/* Copy one header value out of the block into a fresh String. */
static nj_ref http_header_string(nj_vm *vm, const char *block, const char *name)
{
    const char *v = http_header_find(block, name);
    const char *eol;
    char buf[256];
    size_t len;

    if (!v) {
        return NULL;
    }
    eol = strchr(v, '\n');
    len = eol ? (size_t)(eol - v) : strlen(v);
    while (len > 0u && (v[len - 1u] == '\r' || v[len - 1u] == ' ')) {
        len--;
    }
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;
    }
    memcpy(buf, v, len);
    buf[len] = '\0';
    return nj_string_new(vm, buf);
}

/* ------------------------------------------------------------------ */
/* The request                                                         */
/* ------------------------------------------------------------------ */

/*
 * Send the request if it has not been sent yet, and store everything the
 * accessors read.  Every method that can be answered only by a response calls
 * this first.
 */
static nj_status http_send(nj_vm *vm, nj_ref self)
{
    nj_ref urlr;
    nj_ref methr;
    nj_ref hdrr;
    nj_ref outr;
    nj_ref bodyref;
    char url[HTTP_URL_MAX];
    char method[HTTP_METHOD_MAX];
    char hdrs[HTTP_HDR_MAX];
    char rhdrs[HTTP_HDR_MAX];
    const nj_u1 *body = NULL;
    nj_u4 blen = 0u;
    nj_u1 *rbody = NULL;
    nj_u4 rlen = 0u;
    int status = 0;
    nj_status st;
    nj_ref arr;

    if (hget_int(vm, self, F_SENT, "Z")) {
        return NJ_OK;
    }
    if (hget_int(vm, self, F_CLOSED, "Z")) {
        return nj_throw(vm, "java/io/IOException", "the connection is closed");
    }
    if (vm->port == NULL || vm->port->http_request == NULL) {
        /*
         * A port with no HTTP stack.  The MIDlet gets the same answer it got
         * before this module existed, except that it arrives at the first
         * response call instead of at Connector.open(), which is where the
         * specification puts it.
         */
        return nj_throw(vm, "java/io/IOException",
                        "this build has no HTTP support; the port must supply it");
    }

    urlr = hget_ref(vm, self, F_URL, "Ljava/lang/String;");
    methr = hget_ref(vm, self, F_METHOD, "Ljava/lang/String;");
    hdrr = hget_ref(vm, self, F_REQHDR, "Ljava/lang/String;");
    outr = hget_ref(vm, self, F_OUT, "Ljava/io/ByteArrayOutputStream;");
    if (http_text(urlr, url, (int)sizeof(url)) <= 0) {
        return nj_throw(vm, "java/io/IOException", "the connection has no URL");
    }
    if (http_text(methr, method, (int)sizeof(method)) <= 0) {
        http_copy_str(method, (int)sizeof(method), "GET");
    }
    hdrs[0] = '\0';
    (void)http_text(hdrr, hdrs, (int)sizeof(hdrs));

    /*
     * A POST body is whatever the MIDlet wrote to openOutputStream().  The
     * array is kept in a field first so that the garbage collector can see it
     * while the port call runs - the pointer alone does not root anything.
     */
    if (outr) {
        arr = nj_call_ref(vm, outr, NJ_C_BAOS, "toByteArray", "()[B", NULL);
        if (arr) {
            /*
             * Store it on the object first: the array has to stay reachable
             * for the garbage collector while the port call runs, and a local
             * nj_ref in C roots nothing.
             */
            hset_ref(vm, self, F_BODY, "[B", arr);
            bodyref = arr;
            body = (const nj_u1 *)nj_array_data(bodyref);
            blen = nj_array_length(bodyref);
        }
    }

    rhdrs[0] = '\0';
    st = vm->port->http_request(vm->port->ud, url, method, hdrs,
                                body, blen,
                                &status, rhdrs, (nj_size)sizeof(rhdrs),
                                &rbody, &rlen,
                                http_alloc, vm);
    if (st != NJ_OK) {
        if (rbody) {
            nj_host_free(vm, rbody);
        }
        return nj_throw(vm, "java/io/IOException",
                        (st == NJ_ERR_UNSUPPORTED)
                            ? "this build has no HTTP support; the port must supply it"
                            : "the HTTP request failed");
    }

    hset_int(vm, self, F_STATUS, "I", status);
    hset_ref(vm, self, F_RESPHDR, "Ljava/lang/String;", nj_string_new(vm, rhdrs));
    if (rbody && rlen > 0u) {
        nj_ref resp = nj_byte_array_new(vm, rlen);
        if (resp) {
            memcpy(nj_array_data(resp), rbody, (size_t)rlen);
            hset_ref(vm, self, F_RESP, "[B", resp);
        }
    }
    if (rbody) {
        nj_host_free(vm, rbody);
    }
    hset_int(vm, self, F_SENT, "Z", 1);
    return NJ_OK;
}

/* The bytes of the response, or NULL. */
static nj_ref http_response_bytes(nj_vm *vm, nj_ref self)
{
    return hget_ref(vm, self, F_RESP, "[B");
}

/* Build the object Connector.open() hands back. */
nj_ref nj_http_new(nj_vm *vm, const char *url);

nj_ref nj_http_new(nj_vm *vm, const char *url)
{
    nj_ref o;
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port;

    if (!url || http_split(url, scheme, (int)sizeof(scheme),
                           host, (int)sizeof(host), &port,
                           file, (int)sizeof(file),
                           query, (int)sizeof(query),
                           ref, (int)sizeof(ref)) != 0) {
        (void)nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                       url ? url : "(null)");
        return NULL;
    }
    o = nj_object_new(vm, strcmp(scheme, "https") == 0 ? NJ_C_HTTPSIMPL
                                                       : NJ_C_HTTPIMPL);
    if (!o) {
        return NULL;
    }
    hset_ref(vm, o, F_URL, "Ljava/lang/String;", nj_string_new(vm, url));
    hset_ref(vm, o, F_METHOD, "Ljava/lang/String;", nj_string_new(vm, "GET"));
    hset_int(vm, o, F_SENT, "Z", 0);
    hset_int(vm, o, F_STATUS, "I", 0);
    hset_int(vm, o, F_CLOSED, "Z", 0);
    return o;
}

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */

NJFN(http_get_url)
{
    nj_ref s = hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    ret->r = s ? s : nj_string_new(vm, "");
    return NJ_OK;
}

NJFN(http_get_protocol)
{
    char url[HTTP_URL_MAX];
    const char *slash;
    char buf[16];
    int n;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    slash = strstr(url, "://");
    n = slash ? (int)(slash - url) : 0;
    if (n <= 0 || n >= (int)sizeof(buf)) {
        n = 0;
    }
    memcpy(buf, url, (size_t)n);
    buf[n] = '\0';
    ret->r = nj_string_new(vm, buf);
    return NJ_OK;
}

NJFN(http_get_host)
{
    char url[HTTP_URL_MAX];
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port = 0;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    if (http_split(url, scheme, (int)sizeof(scheme), host, (int)sizeof(host),
                   &port, file, (int)sizeof(file), query, (int)sizeof(query),
                   ref, (int)sizeof(ref)) != 0) {
        return nj_throw(vm, "java/io/IOException", "the URL cannot be parsed");
    }
    ret->r = nj_string_new(vm, host);
    return NJ_OK;
}

NJFN(http_get_port)
{
    char url[HTTP_URL_MAX];
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port = 0;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    if (http_split(url, scheme, (int)sizeof(scheme), host, (int)sizeof(host),
                   &port, file, (int)sizeof(file), query, (int)sizeof(query),
                   ref, (int)sizeof(ref)) != 0) {
        return nj_throw(vm, "java/io/IOException", "the URL cannot be parsed");
    }
    ret->i = port;
    return NJ_OK;
}

NJFN(http_get_file)
{
    char url[HTTP_URL_MAX];
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port = 0;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    if (http_split(url, scheme, (int)sizeof(scheme), host, (int)sizeof(host),
                   &port, file, (int)sizeof(file), query, (int)sizeof(query),
                   ref, (int)sizeof(ref)) != 0) {
        return nj_throw(vm, "java/io/IOException", "the URL cannot be parsed");
    }
    ret->r = nj_string_new(vm, file);
    return NJ_OK;
}

NJFN(http_get_query)
{
    char url[HTTP_URL_MAX];
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port = 0;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    if (http_split(url, scheme, (int)sizeof(scheme), host, (int)sizeof(host),
                   &port, file, (int)sizeof(file), query, (int)sizeof(query),
                   ref, (int)sizeof(ref)) != 0) {
        return nj_throw(vm, "java/io/IOException", "the URL cannot be parsed");
    }
    ret->r = nj_string_new(vm, query);
    return NJ_OK;
}

NJFN(http_get_ref)
{
    char url[HTTP_URL_MAX];
    char scheme[16];
    char host[256];
    char file[HTTP_URL_MAX];
    char query[256];
    char ref[128];
    int port = 0;

    (void)http_text(hget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;"),
                    url, (int)sizeof(url));
    if (http_split(url, scheme, (int)sizeof(scheme), host, (int)sizeof(host),
                   &port, file, (int)sizeof(file), query, (int)sizeof(query),
                   ref, (int)sizeof(ref)) != 0) {
        return nj_throw(vm, "java/io/IOException", "the URL cannot be parsed");
    }
    ret->r = nj_string_new(vm, ref);
    return NJ_OK;
}

NJFN(http_set_method)
{
    nj_ref self = NJ_THIS(args);
    nj_ref s = NJ_ARG_REF(args, 0);
    char m[HTTP_METHOD_MAX];

    if (hget_int(vm, self, F_SENT, "Z")) {
        return nj_throw(vm, "java/io/IOException",
                        "the request has already been sent");
    }
    if (hget_int(vm, self, F_CLOSED, "Z")) {
        return nj_throw(vm, "java/io/IOException", "the connection is closed");
    }
    if (!s || nj_string_to_utf8(s, m, (int)sizeof(m)) <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the method is empty");
    }
    if (strcmp(m, "GET") != 0 && strcmp(m, "POST") != 0 &&
        strcmp(m, "HEAD") != 0) {
        /* The specification allows a server-specific method, but a typo is
         * far more likely than a server that wants "get"; refusing here beats
         * sending a request the server will answer with 400. */
        return nj_throw(vm, "java/io/IOException",
                        "the method must be GET, POST or HEAD");
    }
    hset_ref(vm, self, F_METHOD, "Ljava/lang/String;", s);
    return NJ_OK;
}

NJFN(http_get_method)
{
    nj_ref s = hget_ref(vm, NJ_THIS(args), F_METHOD, "Ljava/lang/String;");
    ret->r = s ? s : nj_string_new(vm, "GET");
    return NJ_OK;
}

/*
 * Request headers are kept as text, "Name: value\n" per line, because that is
 * what the port hook takes and because a MIDlet sets at most a handful.  A
 * second call for the same name replaces the first: the specification says the
 * value is overwritten, and a duplicated Content-Type would confuse a server.
 */
NJFN(http_set_property)
{
    nj_ref self = NJ_THIS(args);
    nj_ref namer = NJ_ARG_REF(args, 0);
    nj_ref valr = NJ_ARG_REF(args, 1);
    char name[128];
    char value[512];
    char old[HTTP_HDR_MAX];
    char fresh[HTTP_HDR_MAX];
    const char *p;
    size_t used = 0u;

    if (hget_int(vm, self, F_SENT, "Z")) {
        return nj_throw(vm, "java/io/IOException",
                        "the request has already been sent");
    }
    if (!namer || !valr) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_to_utf8(namer, name, (int)sizeof(name)) <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the header name is empty");
    }
    (void)nj_string_to_utf8(valr, value, (int)sizeof(value));
    if (strchr(name, ':') || strchr(name, '\n')) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the header name is not valid");
    }

    old[0] = '\0';
    (void)http_text(hget_ref(vm, self, F_REQHDR, "Ljava/lang/String;"),
                    old, (int)sizeof(old));
    /* keep every line whose name differs */
    for (p = old; *p; ) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p + 1) : strlen(p);
        const char *colon = strchr(p, ':');
        size_t klen = colon ? (size_t)(colon - p) : 0u;
        int same = (klen == strlen(name));
        size_t i;
        for (i = 0u; same && i < klen; i++) {
            char a = p[i];
            char b = name[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                same = 0;
            }
        }
        if (!same && used + len < sizeof(fresh)) {
            memcpy(fresh + used, p, len);
            used += len;
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    fresh[used] = '\0';
    /* append the new one */
    {
        size_t need = strlen(name) + 2u + strlen(value) + 1u;
        if (used + need >= sizeof(fresh)) {
            return nj_throw(vm, "java/io/IOException",
                            "too many request headers");
        }
        memcpy(fresh + used, name, strlen(name));
        used += strlen(name);
        fresh[used++] = ':';
        fresh[used++] = ' ';
        memcpy(fresh + used, value, strlen(value));
        used += strlen(value);
        fresh[used++] = '\n';
        fresh[used] = '\0';
    }
    hset_ref(vm, self, F_REQHDR, "Ljava/lang/String;", nj_string_new(vm, fresh));
    return NJ_OK;
}

NJFN(http_get_property)
{
    nj_ref namer = NJ_ARG_REF(args, 0);
    char name[128];
    char block[HTTP_HDR_MAX];
    nj_ref v;

    if (!namer) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    (void)nj_string_to_utf8(namer, name, (int)sizeof(name));
    (void)http_text(hget_ref(vm, NJ_THIS(args), F_REQHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    v = http_header_string(vm, block, name);
    ret->r = v;
    return NJ_OK;
}

NJFN(http_get_response_code)
{
    if (http_send(vm, NJ_THIS(args)) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = hget_int(vm, NJ_THIS(args), F_STATUS, "I");
    return NJ_OK;
}

NJFN(http_get_response_message)
{
    if (http_send(vm, NJ_THIS(args)) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = nj_string_new(vm, http_reason(hget_int(vm, NJ_THIS(args),
                                                    F_STATUS, "I")));
    return NJ_OK;
}

NJFN(http_get_header_field)
{
    nj_ref self = NJ_THIS(args);
    nj_ref namer = NJ_ARG_REF(args, 0);
    char name[128];
    char block[HTTP_HDR_MAX];

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    if (!namer) {
        ret->r = NULL;
        return NJ_OK;
    }
    (void)nj_string_to_utf8(namer, name, (int)sizeof(name));
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    ret->r = http_header_string(vm, block, name);
    return NJ_OK;
}

/*
 * HTTP dates.  The Messaging spec requires HttpConnection to accept the three
 * HTTP/1.0 date formats, though every server in practice sends RFC 1123
 * ("Sun, 06 Nov 1994 08:49:37 GMT"), which is what this parses; anything else
 * yields the caller's default, as the spec asks.
 */
static int http_num(const char *p, int n, int *out)
{
    int v = 0, i;

    for (i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') {
            return 0;
        }
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return 1;
}

static int http_month(const char *p)
{
    static const char *const mon[12] = {
        "jan", "feb", "mar", "apr", "may", "jun",
        "jul", "aug", "sep", "oct", "nov", "dec"
    };
    int i;

    for (i = 0; i < 12; i++) {
        if ((p[0] | 0x20) == mon[i][0] && (p[1] | 0x20) == mon[i][1]
            && (p[2] | 0x20) == mon[i][2]) {
            return i + 1;
        }
    }
    return 0;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Hinnant's algorithm). */
static nj_s8 http_epoch(int y, int mo, int d, int hh, int mi, int ss)
{
    int yy = y - (mo <= 2 ? 1 : 0);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    nj_u4 yoe = (nj_u4)(yy - era * 400);
    nj_u4 doy = (nj_u4)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    nj_u4 doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    nj_s8 days = (nj_s8)era * 146097 + (nj_s8)doe - 719468;

    /* Milliseconds since the epoch, which is what HttpConnection returns. */
    return (days * 86400 + (nj_s8)hh * 3600 + (nj_s8)mi * 60 + (nj_s8)ss) * 1000l;
}

/* -1 when the text is not an RFC 1123 date. */
static nj_s8 http_parse_date(const char *text)
{
    const char *p = strchr(text, ',');
    int d = 0, y = 0, hh = 0, mi = 0, ss = 0, mo;

    if (!p) {
        return -1;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    if (!http_num(p, 2, &d)) {
        return -1;
    }
    p += 2;
    if (*p != ' ') {
        return -1;
    }
    p++;
    mo = http_month(p);
    if (mo == 0) {
        return -1;
    }
    p += 3;
    if (*p != ' ') {
        return -1;
    }
    p++;
    if (!http_num(p, 4, &y)) {
        return -1;
    }
    p += 4;
    if (*p != ' ') {
        return -1;
    }
    p++;
    if (!http_num(p, 2, &hh) || p[2] != ':') {
        return -1;
    }
    if (!http_num(p + 3, 2, &mi) || p[5] != ':') {
        return -1;
    }
    if (!http_num(p + 6, 2, &ss)) {
        return -1;
    }
    return http_epoch(y, mo, d, hh, mi, ss);
}

/* The value of one header parsed as a date, or dflt when absent/unparsable. */
static nj_s8 http_header_date(nj_vm *vm, const char *block, const char *name,
                              nj_s8 dflt)
{
    const char *v = http_header_find(block, name);
    const char *eol;
    char buf[64];
    size_t len;
    nj_s8 at;

    (void)vm;
    if (!v) {
        return dflt;
    }
    eol = strchr(v, '\n');
    len = eol ? (size_t)(eol - v) : strlen(v);
    while (len > 0u && (v[len - 1u] == '\r' || v[len - 1u] == ' ')) {
        len--;
    }
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1u;
    }
    memcpy(buf, v, len);
    buf[len] = '\0';
    at = http_parse_date(buf);
    return (at < 0) ? dflt : at;
}

/*
 * getHeaderField(int) / getHeaderFieldKey(int) walk the response headers in
 * the order the port reported them, which is the order the server sent them.
 */
NJFN(http_get_header_field_at)
{
    nj_ref self = NJ_THIS(args);
    char block[HTTP_HDR_MAX];
    int want = NJ_ARG_INT(args, 0);
    const char *p;
    int i = 0;

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    for (p = block; *p; ) {
        const char *eol = strchr(p, '\n');
        const char *colon = strchr(p, ':');
        if (i == want) {
            const char *v = colon ? colon + 1 : p;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            {
                char buf[256];
                size_t len = eol ? (size_t)(eol - v) : strlen(v);
                if (len >= sizeof(buf)) {
                    len = sizeof(buf) - 1u;
                }
                memcpy(buf, v, len);
                buf[len] = '\0';
                ret->r = nj_string_new(vm, buf);
            }
            return NJ_OK;
        }
        i++;
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    ret->r = NULL;
    return NJ_OK;
}

NJFN(http_get_header_key_at)
{
    nj_ref self = NJ_THIS(args);
    char block[HTTP_HDR_MAX];
    int want = NJ_ARG_INT(args, 0);
    const char *p;
    int i = 0;

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    for (p = block; *p; ) {
        const char *eol = strchr(p, '\n');
        const char *colon = strchr(p, ':');
        if (i == want) {
            if (!colon) {
                ret->r = NULL;
                return NJ_OK;
            }
            {
                char buf[128];
                size_t len = (size_t)(colon - p);
                if (len >= sizeof(buf)) {
                    len = sizeof(buf) - 1u;
                }
                memcpy(buf, p, len);
                buf[len] = '\0';
                ret->r = nj_string_new(vm, buf);
            }
            return NJ_OK;
        }
        i++;
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    ret->r = NULL;
    return NJ_OK;
}

NJFN(http_get_length)
{
    nj_ref self = NJ_THIS(args);
    char block[HTTP_HDR_MAX];
    const char *v;
    nj_s8 n = -1;

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    v = http_header_find(block, "Content-Length");
    if (v) {
        n = 0;
        while (*v >= '0' && *v <= '9') {
            n = n * 10 + (*v - '0');
            v++;
        }
    }
    ret->j = n;
    return NJ_OK;
}

NJFN(http_get_type)
{
    nj_ref self = NJ_THIS(args);
    char block[HTTP_HDR_MAX];

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    ret->r = http_header_string(vm, block, "Content-Type");
    return NJ_OK;
}

/*
 * getEncoding() is the charset parameter of Content-Type, not the whole type:
 * a MIDlet that calls it wants to know how to decode the bytes.
 */
NJFN(http_get_encoding)
{
    nj_ref self = NJ_THIS(args);
    char block[HTTP_HDR_MAX];
    char value[256];
    const char *v;
    const char *eol;
    const char *semi;
    size_t len;

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    v = http_header_find(block, "Content-Type");
    if (!v) {
        ret->r = NULL;
        return NJ_OK;
    }
    eol = strchr(v, '\n');
    len = eol ? (size_t)(eol - v) : strlen(v);
    if (len >= sizeof(value)) {
        len = sizeof(value) - 1u;
    }
    memcpy(value, v, len);
    value[len] = '\0';
    semi = strchr(value, ';');
    if (!semi) {
        ret->r = NULL;
        return NJ_OK;
    }
    v = semi + 1;
    while (*v == ' ') {
        v++;
    }
    if (strncmp(v, "charset=", 8u) != 0) {
        ret->r = NULL;
        return NJ_OK;
    }
    ret->r = nj_string_new(vm, v + 8);
    return NJ_OK;
}

/* Dates: see the header comment - -1 rather than a wrong date. */
/*
 * The pair of header getters the core CLDC/MIDP audit reported missing: both
 * return the caller's default when the field is absent or does not parse, as
 * the Messaging spec requires.
 */
NJFN(http_get_header_field_int)
{
    nj_ref self = NJ_THIS(args);
    nj_ref namer = NJ_ARG_REF(args, 0);
    char name[128];
    char block[HTTP_HDR_MAX];
    const char *v;
    nj_s8 acc = 0;
    int any = 0;
    int neg = 0;

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = NJ_ARG_INT(args, 1);
    if (!namer) {
        return NJ_OK;
    }
    (void)nj_string_to_utf8(namer, name, (int)sizeof(name));
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    v = http_header_find(block, name);
    if (!v) {
        return NJ_OK;
    }
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    if (*v == '-') {
        neg = 1;
        v++;
    }
    while (*v >= '0' && *v <= '9') {
        any = 1;
        acc = acc * 10 + (*v - '0');
        if (acc > 0x7FFFFFFF) {
            return NJ_OK;
        }
        v++;
    }
    if (!any) {
        return NJ_OK;
    }
    ret->i = (nj_s4)(neg ? -acc : acc);
    return NJ_OK;
}

NJFN(http_get_header_field_date)
{
    nj_ref self = NJ_THIS(args);
    nj_ref namer = NJ_ARG_REF(args, 0);
    char name[128];
    char block[HTTP_HDR_MAX];

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    ret->j = args[1].j;
    if (!namer) {
        return NJ_OK;
    }
    (void)nj_string_to_utf8(namer, name, (int)sizeof(name));
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    ret->j = http_header_date(vm, block, name, args[1].j);
    return NJ_OK;
}

/* getDate()/getExpiration()/getLastModified() read their own headers now. */
static nj_status http_named_date(nj_vm *vm, nj_value *args, nj_value *ret,
                                 const char *name)
{
    char block[HTTP_HDR_MAX];
    nj_ref self = NJ_THIS(args);

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    (void)http_text(hget_ref(vm, self, F_RESPHDR, "Ljava/lang/String;"),
                    block, (int)sizeof(block));
    ret->j = http_header_date(vm, block, name, -1);
    return NJ_OK;
}

NJFN(http_get_date)
{
    return http_named_date(vm, args, ret, "date");
}

NJFN(http_get_expiration)
{
    return http_named_date(vm, args, ret, "expires");
}

NJFN(http_get_last_modified)
{
    return http_named_date(vm, args, ret, "last-modified");
}

NJFN(http_open_input)
{
    nj_ref self = NJ_THIS(args);
    nj_ref stream = hget_ref(vm, self, F_STREAM, "Ljava/io/InputStream;");
    nj_ref bytes;
    nj_value a[1];

    if (http_send(vm, self) != NJ_OK) {
        return NJ_ERR;
    }
    if (stream) {
        ret->r = stream;
        return NJ_OK;
    }
    bytes = http_response_bytes(vm, self);
    if (!bytes) {
        bytes = nj_byte_array_new(vm, 0u);
    }
    stream = nj_object_new(vm, NJ_C_BAIS);
    if (!stream) {
        return NJ_ERR;
    }
    a[0].r = bytes;
    if (nj_call_special(vm, stream, NJ_C_BAIS, "<init>", "([B)V", a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    hset_ref(vm, self, F_STREAM, "Ljava/io/InputStream;", stream);
    ret->r = stream;
    return NJ_OK;
}

NJFN(http_open_data_input)
{
    nj_ref stream;
    nj_ref dis;
    nj_value a[1];

    ret->r = NULL;
    {
        nj_value r;
        if (NJ_CALL(http_open_input, vm, th, args, &r) != NJ_OK) {
            return NJ_ERR;
        }
        stream = r.r;
    }
    if (!stream) {
        return NJ_OK;
    }
    dis = nj_object_new(vm, NJ_C_DIS);
    if (!dis) {
        return NJ_ERR;
    }
    a[0].r = stream;
    if (nj_call_special(vm, dis, NJ_C_DIS, "<init>",
                        "(Ljava/io/InputStream;)V", a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = dis;
    return NJ_OK;
}

NJFN(http_open_output)
{
    nj_ref self = NJ_THIS(args);
    nj_ref out;
    nj_ref existing = hget_ref(vm, self, F_OUT, "Ljava/io/ByteArrayOutputStream;");

    if (hget_int(vm, self, F_SENT, "Z")) {
        return nj_throw(vm, "java/io/IOException",
                        "the request has already been sent");
    }
    if (hget_int(vm, self, F_CLOSED, "Z")) {
        return nj_throw(vm, "java/io/IOException", "the connection is closed");
    }
    if (existing) {
        ret->r = existing;
        return NJ_OK;
    }
    out = nj_object_new(vm, NJ_C_BAOS);
    if (!out) {
        return NJ_ERR;
    }
    if (nj_call_special(vm, out, NJ_C_BAOS, "<init>", "()V", NULL, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    hset_ref(vm, self, F_OUT, "Ljava/io/ByteArrayOutputStream;", out);
    ret->r = out;
    return NJ_OK;
}

NJFN(http_open_data_output)
{
    nj_ref out;
    nj_ref dos;
    nj_value a[1];

    ret->r = NULL;
    {
        nj_value r;
        if (NJ_CALL(http_open_output, vm, th, args, &r) != NJ_OK) {
            return NJ_ERR;
        }
        out = r.r;
    }
    if (!out) {
        return NJ_OK;
    }
    dos = nj_object_new(vm, NJ_C_DOS);
    if (!dos) {
        return NJ_ERR;
    }
    a[0].r = out;
    if (nj_call_special(vm, dos, NJ_C_DOS, "<init>",
                        "(Ljava/io/OutputStream;)V", a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = dos;
    return NJ_OK;
}

/*
 * HttpsConnection.getSecurityInfo().  JSR 75 says an IOException means "the
 * connection is closed or the security information is not available", and it
 * is not available here: the port's http_request hook performs a whole
 * request host-side and never hands back the TLS session.  An object full of
 * invented protocol names and a null certificate would be worse than an
 * honest exception - and a MIDlet that only casts to HttpsConnection never
 * reaches this call.
 */
NJFN(https_get_security_info)
{
    (void)args;
    (void)ret;
    return nj_throw(vm, "java/io/IOException",
                    "TLS session information is not available from this port");
}

NJFN(http_close)
{
    nj_ref self = NJ_THIS(args);
    hset_int(vm, self, F_CLOSED, "Z", 1);
    hset_ref(vm, self, F_STREAM, "Ljava/io/InputStream;", NULL);
    hset_ref(vm, self, F_OUT, "Ljava/io/ByteArrayOutputStream;", NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Late static init: HttpConnection's string constants                 */
/* ------------------------------------------------------------------ */

nj_status nj_http_late_init(nj_vm *vm);

nj_status nj_http_late_init(nj_vm *vm)
{
    static const struct { const char *n; const char *v; } k[] = {
        { "GET", "GET" }, { "POST", "POST" }, { "HEAD", "HEAD" }
    };
    unsigned i;

    if (!vm) {
        return NJ_ERR;
    }
    for (i = 0u; i < sizeof(k) / sizeof(k[0]); i++) {
        nj_field *f = nj_field_lookup(vm, NJ_C_HTTPCONN, k[i].n,
                                      "Ljava/lang/String;", 1);
        if (f) {
            nj_field_write_ref(vm, NULL, f, nj_string_new(vm, k[i].v));
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */

#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field httpconn_fields[] = {
    FLD("GET",  "Ljava/lang/String;", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("POST", "Ljava/lang/String;", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("HEAD", "Ljava/lang/String;", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_field httpimpl_fields[] = {
    FLD(F_URL,     "Ljava/lang/String;", 0),
    FLD(F_METHOD,  "Ljava/lang/String;", 0),
    FLD(F_REQHDR,  "Ljava/lang/String;", 0),
    FLD(F_OUT,     "Ljava/io/ByteArrayOutputStream;", 0),
    FLD(F_BODY,    "[B", 0),
    FLD(F_SENT,    "Z", 0),
    FLD(F_STATUS,  "I", 0),
    FLD(F_RESP,    "[B", 0),
    FLD(F_RESPHDR, "Ljava/lang/String;", 0),
    FLD(F_STREAM,  "Ljava/io/InputStream;", 0),
    FLD(F_CLOSED,  "Z", 0)
};

static const nj_builtin_method connection_methods[] = {
    M("close", "()V", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method inputconn_methods[] = {
    M("openInputStream",     "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method outputconn_methods[] = {
    M("openOutputStream",     "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT)
};

/*
 * StreamConnection has the same problem HttpConnection has below: a call
 * typed StreamConnection (which is what acceptAndOpen() returns) resolves in
 * THIS table, so the table has to carry the union of what a stream connection
 * offers even though real Java inherits it from Connection and the two
 * direction interfaces.
 */
static const nj_builtin_method streamconn_methods[] = {
    M("close",                "()V", NJ_ACC_ABSTRACT),
    M("openInputStream",      "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream",  "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT),
    M("openOutputStream",     "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method contentconn_methods[] = {
    M("getType",     "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getEncoding", "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getLength",   "()J", NJ_ACC_ABSTRACT)
};

/*
 * A MIDlet's static type is almost always HttpConnection, and this runtime
 * resolves an interface method by looking in the interface's OWN table - it
 * does not walk the interface chain the way a class hierarchy is walked, and
 * a built-in class can carry only one super.  So HttpConnection declares the
 * union of what Connection, InputConnection, OutputConnection, ContentConnection
 * and HttpConnection itself define: that is exactly the set a real MIDP
 * HttpConnection inherits, spelled out because the chain cannot hold it.
 *
 * Without this, `c.openOutputStream()` and `c.getType()` - both of which real
 * MIDlets call on an HttpConnection reference - failed with NoSuchMethodError
 * even though the implementation class answers them.
 */
static const nj_builtin_method httpconn_methods[] = {
    M("close",               "()V", NJ_ACC_ABSTRACT),
    M("openInputStream",     "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT),
    M("openOutputStream",    "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT),
    M("getType",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getEncoding",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getLength",           "()J", NJ_ACC_ABSTRACT),
    M("getURL",              "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getProtocol",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHost",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getPort",             "()I", NJ_ACC_ABSTRACT),
    M("getFile",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getQuery",            "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getRef",              "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("setRequestMethod",    "(Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("getRequestMethod",    "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getResponseCode",     "()I", NJ_ACC_ABSTRACT),
    M("getResponseMessage",  "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderField",      "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderFieldKey",   "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderFieldInt",   "(Ljava/lang/String;I)I", NJ_ACC_ABSTRACT),
    M("getHeaderFieldDate",  "(Ljava/lang/String;J)J", NJ_ACC_ABSTRACT),
    M("getDate",             "()J", NJ_ACC_ABSTRACT),
    M("getExpiration",       "()J", NJ_ACC_ABSTRACT),
    M("getLastModified",     "()J", NJ_ACC_ABSTRACT)
};

/*
 * HttpsConnection carries the same union plus its one extra member.  The rows
 * are written out again rather than shared through a macro because the API
 * audit (tools/api_audit.py) reads these tables as text: a macro hid the whole
 * HttpConnection surface from it and 21 members looked missing.
 */
static const nj_builtin_method httpsconn_methods[] = {
    M("close",               "()V", NJ_ACC_ABSTRACT),
    M("openInputStream",     "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT),
    M("openOutputStream",    "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT),
    M("getType",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getEncoding",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getLength",           "()J", NJ_ACC_ABSTRACT),
    M("getURL",              "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getProtocol",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHost",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getPort",             "()I", NJ_ACC_ABSTRACT),
    M("getFile",             "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getQuery",            "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getRef",              "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("setRequestMethod",    "(Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("getRequestMethod",    "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getResponseCode",     "()I", NJ_ACC_ABSTRACT),
    M("getResponseMessage",  "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderField",      "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderFieldKey",   "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getHeaderFieldInt",   "(Ljava/lang/String;I)I", NJ_ACC_ABSTRACT),
    M("getHeaderFieldDate",  "(Ljava/lang/String;J)J", NJ_ACC_ABSTRACT),
    M("getDate",             "()J", NJ_ACC_ABSTRACT),
    M("getExpiration",       "()J", NJ_ACC_ABSTRACT),
    M("getLastModified",     "()J", NJ_ACC_ABSTRACT),
    M("getSecurityInfo", "()Ljavax/microedition/io/SecurityInfo;", NJ_ACC_ABSTRACT),
};



static const nj_builtin_method httpimpl_methods[] = {
    M("getURL",              "()Ljava/lang/String;", 0),
    M("getProtocol",         "()Ljava/lang/String;", 0),
    M("getHost",             "()Ljava/lang/String;", 0),
    M("getPort",             "()I", 0),
    M("getFile",             "()Ljava/lang/String;", 0),
    M("getQuery",            "()Ljava/lang/String;", 0),
    M("getRef",              "()Ljava/lang/String;", 0),
    M("setRequestMethod",    "(Ljava/lang/String;)V", 0),
    M("getRequestMethod",    "()Ljava/lang/String;", 0),
    M("setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", 0),
    M("getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", 0),
    M("getResponseCode",     "()I", 0),
    M("getResponseMessage",  "()Ljava/lang/String;", 0),
    M("getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", 0),
    M("getHeaderField",      "(I)Ljava/lang/String;", 0),
    M("getHeaderFieldKey",   "(I)Ljava/lang/String;", 0),
    M("getHeaderFieldInt",   "(Ljava/lang/String;I)I", 0),
    M("getHeaderFieldDate",  "(Ljava/lang/String;J)J", 0),
    M("getLength",           "()J", 0),
    M("getType",             "()Ljava/lang/String;", 0),
    M("getEncoding",         "()Ljava/lang/String;", 0),
    M("getDate",             "()J", 0),
    M("getExpiration",       "()J", 0),
    M("getLastModified",     "()J", 0),
    M("openInputStream",     "()Ljava/io/InputStream;", 0),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", 0),
    M("openOutputStream",    "()Ljava/io/OutputStream;", 0),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", 0),
    M("close",               "()V", 0)
};

/* The https object answers the same calls; only the class differs. */
static const nj_builtin_method httpsimpl_methods[] = {
    M("getURL",              "()Ljava/lang/String;", 0),
    M("getProtocol",         "()Ljava/lang/String;", 0),
    M("getHost",             "()Ljava/lang/String;", 0),
    M("getPort",             "()I", 0),
    M("getFile",             "()Ljava/lang/String;", 0),
    M("getQuery",            "()Ljava/lang/String;", 0),
    M("getRef",              "()Ljava/lang/String;", 0),
    M("setRequestMethod",    "(Ljava/lang/String;)V", 0),
    M("getRequestMethod",    "()Ljava/lang/String;", 0),
    M("setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", 0),
    M("getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", 0),
    M("getResponseCode",     "()I", 0),
    M("getResponseMessage",  "()Ljava/lang/String;", 0),
    M("getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", 0),
    M("getHeaderField",      "(I)Ljava/lang/String;", 0),
    M("getHeaderFieldKey",   "(I)Ljava/lang/String;", 0),
    M("getHeaderFieldInt",   "(Ljava/lang/String;I)I", 0),
    M("getHeaderFieldDate",  "(Ljava/lang/String;J)J", 0),
    M("getLength",           "()J", 0),
    M("getType",             "()Ljava/lang/String;", 0),
    M("getEncoding",         "()Ljava/lang/String;", 0),
    M("getDate",             "()J", 0),
    M("getExpiration",       "()J", 0),
    M("getLastModified",     "()J", 0),
    M("openInputStream",     "()Ljava/io/InputStream;", 0),
    M("openDataInputStream", "()Ljava/io/DataInputStream;", 0),
    M("openOutputStream",    "()Ljava/io/OutputStream;", 0),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", 0),
    M("close",               "()V", 0),
    M("getSecurityInfo", "()Ljavax/microedition/io/SecurityInfo;", 0)
};


/* JSR 75's session information.  The port's http hook does not expose TLS
 * details, so getSecurityInfo() throws; these declarations are what lets a
 * MIDlet that names the type load and link. */
static const nj_builtin_method secinfo_methods[] = {
    M("getProtocolName",      "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getProtocolVersion",   "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getCipherSuite",       "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getServerCertificate", "()Ljavax/microedition/pki/Certificate;", NJ_ACC_ABSTRACT)
};

/* javax.microedition.pki.Certificate, the interface SecurityInfo returns. */
static const nj_builtin_method cert_methods[] = {
    M("getSubject",      "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getIssuer",       "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getSerialNumber", "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getSigAlgName",   "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getVersion",      "()Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

/*
 * SecureConnection is JSR 75's ssl:// type.  This runtime has no TLS, so
 * Connector.open("ssl://...") refuses by name; the interface exists so a title
 * that names it (book7's checkers sample does) loads instead of dying with
 * NoClassDefFoundError.  Its table carries the SocketConnection union plus
 * getSecurityInfo, for the same "resolve in the interface's own table" reason.
 */
static const nj_builtin_method secureconn_methods[] = {
    M("close",                "()V", NJ_ACC_ABSTRACT),
    M("openInputStream",      "()Ljava/io/InputStream;", NJ_ACC_ABSTRACT),
    M("openDataInputStream",  "()Ljava/io/DataInputStream;", NJ_ACC_ABSTRACT),
    M("openOutputStream",     "()Ljava/io/OutputStream;", NJ_ACC_ABSTRACT),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", NJ_ACC_ABSTRACT),
    M("getAddress",           "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getPort",              "()I", NJ_ACC_ABSTRACT),
    M("getLocalAddress",      "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("getLocalPort",         "()I", NJ_ACC_ABSTRACT),
    M("setSocketOption",      "(BI)V", NJ_ACC_ABSTRACT),
    M("setSocketOption",      "(II)V", NJ_ACC_ABSTRACT),
    M("getSocketOption",      "(B)I", NJ_ACC_ABSTRACT),
    M("getSocketOption",      "(I)I", NJ_ACC_ABSTRACT),
    M("getSecurityInfo",      "()Ljavax/microedition/io/SecurityInfo;", NJ_ACC_ABSTRACT)
};


const nj_builtin_class nj_lib_http_classes[] = {
    /* Connection is the root of the whole javax.microedition.io hierarchy;
     * StreamConnection and ContentConnection sit between it and
     * HttpConnection, and each is spelled as the next one's super because a
     * built-in class can carry only one. */
    { NJ_C_CONNECTION, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, connection_methods,
      (nj_u2)(sizeof(connection_methods) / sizeof(connection_methods[0])) },
    { NJ_C_INPUTCONN, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, inputconn_methods,
      (nj_u2)(sizeof(inputconn_methods) / sizeof(inputconn_methods[0])) },
    { NJ_C_OUTPUTCONN, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, outputconn_methods,
      (nj_u2)(sizeof(outputconn_methods) / sizeof(outputconn_methods[0])) },
    { NJ_C_STREAMCONN, NJ_C_CONNECTION,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, streamconn_methods,
      (nj_u2)(sizeof(streamconn_methods) / sizeof(streamconn_methods[0])) },
    { NJ_C_CONTENTCONN, NJ_C_STREAMCONN,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, contentconn_methods,
      (nj_u2)(sizeof(contentconn_methods) / sizeof(contentconn_methods[0])) },
    { NJ_C_HTTPCONN, NJ_C_CONTENTCONN,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      httpconn_fields,
      (nj_u2)(sizeof(httpconn_fields) / sizeof(httpconn_fields[0])),
      httpconn_methods,
      (nj_u2)(sizeof(httpconn_methods) / sizeof(httpconn_methods[0])) },
    { NJ_C_HTTPS, NJ_C_HTTPCONN,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, httpsconn_methods,
      (nj_u2)(sizeof(httpsconn_methods) / sizeof(httpsconn_methods[0])) },
    { NJ_C_HTTPIMPL, NJ_C_HTTPCONN, NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      httpimpl_fields,
      (nj_u2)(sizeof(httpimpl_fields) / sizeof(httpimpl_fields[0])),
      httpimpl_methods,
      (nj_u2)(sizeof(httpimpl_methods) / sizeof(httpimpl_methods[0])) },
    { NJ_C_HTTPSIMPL, NJ_C_HTTPS, NJ_ACC_PUBLIC | NJ_ACC_FINAL,
      httpimpl_fields,
      (nj_u2)(sizeof(httpimpl_fields) / sizeof(httpimpl_fields[0])),
      httpsimpl_methods,
      (nj_u2)(sizeof(httpsimpl_methods) / sizeof(httpsimpl_methods[0])) },
    { NJ_C_SECINFO, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, secinfo_methods,
      (nj_u2)(sizeof(secinfo_methods) / sizeof(secinfo_methods[0])) },
    { NJ_C_CERT, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, cert_methods,
      (nj_u2)(sizeof(cert_methods) / sizeof(cert_methods[0])) },
    { NJ_C_SECURECONN, "javax/microedition/io/SocketConnection",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, secureconn_methods,
      (nj_u2)(sizeof(secureconn_methods) / sizeof(secureconn_methods[0])) }
};
const int nj_lib_http_class_count =
    (int)(sizeof(nj_lib_http_classes) / sizeof(nj_lib_http_classes[0]));

/*
 * Both implementation classes get the whole set: HttpImpl answers an http://
 * connection, HttpsImpl an https:// one, and the C routines read the object's
 * own fields either way.  Written out per class because the native-table
 * gates (tools/check_arg_slots.py, check_wide_args.py) parse these rows as
 * text.  getSecurityInfo is the one https-only entry.
 */
const nj_native_entry nj_lib_http_table[] = {
    { NJ_C_HTTPIMPL, "getURL",              "()Ljava/lang/String;", http_get_url },
    { NJ_C_HTTPIMPL, "getProtocol",         "()Ljava/lang/String;", http_get_protocol },
    { NJ_C_HTTPIMPL, "getHost",             "()Ljava/lang/String;", http_get_host },
    { NJ_C_HTTPIMPL, "getPort",             "()I",                  http_get_port },
    { NJ_C_HTTPIMPL, "getFile",             "()Ljava/lang/String;", http_get_file },
    { NJ_C_HTTPIMPL, "getQuery",            "()Ljava/lang/String;", http_get_query },
    { NJ_C_HTTPIMPL, "getRef",              "()Ljava/lang/String;", http_get_ref },
    { NJ_C_HTTPIMPL, "setRequestMethod",    "(Ljava/lang/String;)V", http_set_method },
    { NJ_C_HTTPIMPL, "getRequestMethod",    "()Ljava/lang/String;", http_get_method },
    { NJ_C_HTTPIMPL, "setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", http_set_property },
    { NJ_C_HTTPIMPL, "getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", http_get_property },
    { NJ_C_HTTPIMPL, "getResponseCode",     "()I",                  http_get_response_code },
    { NJ_C_HTTPIMPL, "getResponseMessage",  "()Ljava/lang/String;", http_get_response_message },
    { NJ_C_HTTPIMPL, "getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", http_get_header_field },
    { NJ_C_HTTPIMPL, "getHeaderField",      "(I)Ljava/lang/String;", http_get_header_field_at },
    { NJ_C_HTTPIMPL, "getHeaderFieldKey",   "(I)Ljava/lang/String;", http_get_header_key_at },
    { NJ_C_HTTPIMPL, "getHeaderFieldInt",   "(Ljava/lang/String;I)I", http_get_header_field_int },
    { NJ_C_HTTPIMPL, "getHeaderFieldDate",  "(Ljava/lang/String;J)J", http_get_header_field_date },
    { NJ_C_HTTPIMPL, "getLength",           "()J",                  http_get_length },
    { NJ_C_HTTPIMPL, "getType",             "()Ljava/lang/String;", http_get_type },
    { NJ_C_HTTPIMPL, "getEncoding",         "()Ljava/lang/String;", http_get_encoding },
    { NJ_C_HTTPIMPL, "getDate",             "()J",                  http_get_date },
    { NJ_C_HTTPIMPL, "getExpiration",       "()J",                  http_get_expiration },
    { NJ_C_HTTPIMPL, "getLastModified",     "()J",                  http_get_last_modified },
    { NJ_C_HTTPIMPL, "openInputStream",     "()Ljava/io/InputStream;", http_open_input },
    { NJ_C_HTTPIMPL, "openDataInputStream", "()Ljava/io/DataInputStream;", http_open_data_input },
    { NJ_C_HTTPIMPL, "openOutputStream",    "()Ljava/io/OutputStream;", http_open_output },
    { NJ_C_HTTPIMPL, "openDataOutputStream", "()Ljava/io/DataOutputStream;", http_open_data_output },
    { NJ_C_HTTPIMPL, "close",               "()V",                  http_close },
    { NJ_C_HTTPSIMPL, "getURL",              "()Ljava/lang/String;", http_get_url },
    { NJ_C_HTTPSIMPL, "getProtocol",         "()Ljava/lang/String;", http_get_protocol },
    { NJ_C_HTTPSIMPL, "getHost",             "()Ljava/lang/String;", http_get_host },
    { NJ_C_HTTPSIMPL, "getPort",             "()I",                  http_get_port },
    { NJ_C_HTTPSIMPL, "getFile",             "()Ljava/lang/String;", http_get_file },
    { NJ_C_HTTPSIMPL, "getQuery",            "()Ljava/lang/String;", http_get_query },
    { NJ_C_HTTPSIMPL, "getRef",              "()Ljava/lang/String;", http_get_ref },
    { NJ_C_HTTPSIMPL, "setRequestMethod",    "(Ljava/lang/String;)V", http_set_method },
    { NJ_C_HTTPSIMPL, "getRequestMethod",    "()Ljava/lang/String;", http_get_method },
    { NJ_C_HTTPSIMPL, "setRequestProperty",  "(Ljava/lang/String;Ljava/lang/String;)V", http_set_property },
    { NJ_C_HTTPSIMPL, "getRequestProperty",  "(Ljava/lang/String;)Ljava/lang/String;", http_get_property },
    { NJ_C_HTTPSIMPL, "getResponseCode",     "()I",                  http_get_response_code },
    { NJ_C_HTTPSIMPL, "getResponseMessage",  "()Ljava/lang/String;", http_get_response_message },
    { NJ_C_HTTPSIMPL, "getHeaderField",      "(Ljava/lang/String;)Ljava/lang/String;", http_get_header_field },
    { NJ_C_HTTPSIMPL, "getHeaderField",      "(I)Ljava/lang/String;", http_get_header_field_at },
    { NJ_C_HTTPSIMPL, "getHeaderFieldKey",   "(I)Ljava/lang/String;", http_get_header_key_at },
    { NJ_C_HTTPSIMPL, "getHeaderFieldInt",   "(Ljava/lang/String;I)I", http_get_header_field_int },
    { NJ_C_HTTPSIMPL, "getHeaderFieldDate",  "(Ljava/lang/String;J)J", http_get_header_field_date },
    { NJ_C_HTTPSIMPL, "getLength",           "()J",                  http_get_length },
    { NJ_C_HTTPSIMPL, "getType",             "()Ljava/lang/String;", http_get_type },
    { NJ_C_HTTPSIMPL, "getEncoding",         "()Ljava/lang/String;", http_get_encoding },
    { NJ_C_HTTPSIMPL, "getDate",             "()J",                  http_get_date },
    { NJ_C_HTTPSIMPL, "getExpiration",       "()J",                  http_get_expiration },
    { NJ_C_HTTPSIMPL, "getLastModified",     "()J",                  http_get_last_modified },
    { NJ_C_HTTPSIMPL, "openInputStream",     "()Ljava/io/InputStream;", http_open_input },
    { NJ_C_HTTPSIMPL, "openDataInputStream", "()Ljava/io/DataInputStream;", http_open_data_input },
    { NJ_C_HTTPSIMPL, "openOutputStream",    "()Ljava/io/OutputStream;", http_open_output },
    { NJ_C_HTTPSIMPL, "openDataOutputStream", "()Ljava/io/DataOutputStream;", http_open_data_output },
    { NJ_C_HTTPSIMPL, "close",               "()V",                  http_close },
    { NJ_C_HTTPSIMPL, "getSecurityInfo", "()Ljavax/microedition/io/SecurityInfo;", https_get_security_info }
};
const int nj_lib_http_count =
    (int)(sizeof(nj_lib_http_table) / sizeof(nj_lib_http_table[0]));
