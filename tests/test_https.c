/*
 * nano_j2me - tests/test_https.c
 *
 * JSR 75's connection security surface: HttpsConnection, SecurityInfo,
 * SecureConnection and javax.microedition.pki.Certificate.
 *
 * Two things are asserted here, and they are different problems.
 *
 * The TYPES have to exist.  javax/microedition/io/HttpsConnection was absent,
 * so a title that names it died on a linkage error rather than on a network
 * error.  急速公路狂飙－新年版_84 runs
 *
 *     c instanceof javax.microedition.io.HttpsConnection
 *
 * on every request, and our interpreter resolves that class before it can
 * answer: NoClassDefFoundError in the middle of a working http:// connection.
 * book7's checkers sample names SecureConnection and pki.Certificate.
 *
 * The ANSWER has to be honest.  A built-in class here can carry only one super
 * (see implements_interface() in nj_class.c), so spelling both is-a
 * relationships with one implementation class would make
 * `httpConnection instanceof HttpsConnection` true - a plain socket taking a
 * title's secure-path branch.  That is why there are two implementation
 * classes, and why this test checks BOTH directions of the cast before it
 * checks that either one works.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;

/* A port HTTP stack, so a connection can really be opened and answered. */
static void *fake_alloc(void *aud, nj_u4 n)
{
    return nj_host_alloc((nj_vm *)aud, n);
}

static nj_status fake_http(void *ud, const char *url, const char *method,
                           const char *headers,
                           const nj_u1 *body, nj_u4 body_len,
                           int *status, char *rheaders, nj_size rheaders_cap,
                           nj_u1 **out_body, nj_u4 *out_len,
                           void *(*alloc)(void *aud, nj_u4 n), void *aud)
{
    static const char rh[] = "Content-Type: text/plain\r\n";

    (void)ud; (void)url; (void)method; (void)headers;
    (void)body; (void)body_len; (void)alloc; (void)aud;
    if (status) {
        *status = 200;
    }
    if (rheaders && rheaders_cap > 0u) {
        nj_size n = (nj_size)sizeof(rh);
        if (n >= rheaders_cap) {
            n = rheaders_cap - 1u;
        }
        memcpy(rheaders, rh, (size_t)n);
        rheaders[n] = '\0';
    }
    if (out_body) {
        *out_body = NULL;
    }
    if (out_len) {
        *out_len = 0u;
    }
    return NJ_OK;
}

static nj_ref open_url(nj_vm *vm, const char *url)
{
    nj_value a[1], ret;

    a[0].r = nj_string_new(vm, url);
    if (!a[0].r) {
        return NULL;
    }
    ret.bits = 0u;
    if (nj_call_static(vm, "javax/microedition/io/Connector", "open",
                       "(Ljava/lang/String;)Ljavax/microedition/io/Connection;",
                       a, &ret) != NJ_OK) {
        return NULL;
    }
    return ret.r;
}

static int read_string(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                       char *out, int cap)
{
    nj_value ret;

    out[0] = '\0';
    ret.bits = 0u;
    if (nj_call(vm, o, cls, name, "()Ljava/lang/String;", NULL, &ret) != NJ_OK ||
        ret.r == NULL) {
        return 0;
    }
    return nj_string_to_utf8(ret.r, out, cap);
}

static nj_s4 read_int(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_value ret;

    ret.bits = 0u;
    if (nj_call(vm, o, cls, name, "()I", NULL, &ret) != NJ_OK) {
        return -1;
    }
    return ret.i;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_u4 mark;
    nj_ref http = NULL, https = NULL;
    nj_class *c_http, *c_https, *c_content;
    nj_value a[1], ret;
    const char *d;
    char text[128];

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "FAIL: headless port\n");
        return 1;
    }
    port.http_request = fake_http;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (vm == NULL) {
        fprintf(stderr, "FAIL: vm\n");
        nj_port_shutdown(&port);
        return 1;
    }

    /* 1. The types a MIDlet names have to exist. */
    c_http = nj_class_lookup(vm, "javax/microedition/io/HttpConnection");
    c_https = nj_class_lookup(vm, "javax/microedition/io/HttpsConnection");
    c_content = nj_class_lookup(vm, "javax/microedition/io/ContentConnection");
    CHECK(c_http != NULL && c_https != NULL && c_content != NULL);
    CHECK(nj_class_lookup(vm, "javax/microedition/io/SecurityInfo") != NULL);
    CHECK(nj_class_lookup(vm, "javax/microedition/io/SecureConnection") != NULL);
    CHECK(nj_class_lookup(vm, "javax/microedition/pki/Certificate") != NULL);

    mark = nj_root_mark(vm);
    http = nj_root(vm, open_url(vm, "http://example.invalid/a?b=c"));
    https = nj_root(vm, open_url(vm, "https://example.invalid/secure"));
    CHECK(http != NULL && https != NULL);

    if (http != NULL && https != NULL && c_http != NULL && c_https != NULL &&
        c_content != NULL) {
        /* 2. The object each scheme hands back has the right static types.
         *    nj_class_is_assignable() is the function instanceof and checkcast
         *    use, so this is the interpreter's own answer, not a second rule. */
        CHECK(nj_class_is_assignable(http->cls, c_http));
        CHECK(!nj_class_is_assignable(http->cls, c_https));
        CHECK(nj_class_is_assignable(http->cls, c_content));
        CHECK(nj_class_is_assignable(https->cls, c_https));
        CHECK(nj_class_is_assignable(https->cls, c_http));
        CHECK(nj_class_is_assignable(https->cls, c_content));
        CHECK(strcmp(nj_ref_class_name(http),
                     "javax/microedition/io/HttpImpl") == 0);
        CHECK(strcmp(nj_ref_class_name(https),
                     "javax/microedition/io/HttpsImpl") == 0);

        /* 3. The state fields are per object and per class: the two classes
         *    declare the same field names, so a lookup that ignored the
         *    object's class would read the other connection's values. */
        a[0].r = nj_string_new(vm, "POST");
        CHECK(nj_call(vm, http, "javax/microedition/io/HttpImpl",
                      "setRequestMethod", "(Ljava/lang/String;)V", a, NULL)
              == NJ_OK);
        a[0].r = nj_string_new(vm, "HEAD");
        CHECK(nj_call(vm, https, "javax/microedition/io/HttpsImpl",
                      "setRequestMethod", "(Ljava/lang/String;)V", a, NULL)
              == NJ_OK);
        CHECK(read_string(vm, http, "javax/microedition/io/HttpImpl",
                          "getRequestMethod", text, (int)sizeof(text)) > 0);
        CHECK(strcmp(text, "POST") == 0);
        CHECK(read_string(vm, https, "javax/microedition/io/HttpsImpl",
                          "getRequestMethod", text, (int)sizeof(text)) > 0);
        CHECK(strcmp(text, "HEAD") == 0);

        /* 4. The accessors split the https URL correctly and both classes
         *    answer the shared connection natives. */
        CHECK(read_string(vm, https, "javax/microedition/io/HttpsImpl",
                          "getProtocol", text, (int)sizeof(text)) > 0);
        CHECK(strcmp(text, "https") == 0);
        CHECK(read_string(vm, https, "javax/microedition/io/HttpsImpl",
                          "getHost", text, (int)sizeof(text)) > 0);
        CHECK(strcmp(text, "example.invalid") == 0);
        CHECK(read_int(vm, https, "javax/microedition/io/HttpsImpl",
                       "getPort") == 443);
        CHECK(read_int(vm, http, "javax/microedition/io/HttpImpl",
                       "getPort") == 80);
        CHECK(read_int(vm, http, "javax/microedition/io/HttpImpl",
                       "getResponseCode") == 200);
        CHECK(read_int(vm, https, "javax/microedition/io/HttpsImpl",
                       "getResponseCode") == 200);

        /* 5. getSecurityInfo() has no TLS session to describe - the port's
         *    hook performs the whole request host-side - and JSR 75's own
         *    answer for that is IOException.  Anything else would be invented
         *    protocol names and a null certificate. */
        ret.bits = 0u;
        CHECK(nj_call(vm, https, "javax/microedition/io/HttpsImpl",
                      "getSecurityInfo",
                      "()Ljavax/microedition/io/SecurityInfo;", NULL, &ret)
              != NJ_OK);
        d = nj_exception_describe(vm);
        CHECK(d != NULL && strstr(d, "IOException") != NULL);
        nj_clear_exception(vm);
    }

    /* 6. A scheme this runtime cannot serve is still refused by name: the
     *    class existing must not turn into a promise that TLS is available. */
    CHECK(open_url(vm, "ssl://example.invalid:443") == NULL);
    d = nj_exception_describe(vm);
    CHECK(d != NULL && strstr(d, "ConnectionNotFoundException") != NULL);
    nj_clear_exception(vm);

    nj_root_release(vm, mark);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("test_https: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
