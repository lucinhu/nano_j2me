/* Invoked by check_posix_network.py with ephemeral loopback servers. */
#include "nj2me/nj.h"
#include "../src/core/nj_internal.h"
#include "../src/port/posix/nj_posix_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "network check failed at %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void *allocate(void *ud, nj_u4 n) { (void)ud; return malloc(n); }
static int http(nj_port *p, const char *base, const char *path, const char *method,
                const char *payload, int expected, const char *response)
{
    char url[1024], headers[1024];
    nj_u1 *body = NULL;
    nj_u4 len = 0;
    int status = 0;
    snprintf(url, sizeof(url), "%s%s", base, path);
    CHECK(p->http_request(p->ud, url, method, "X-Probe: loopback\n",
        (const nj_u1 *)payload, payload ? (nj_u4)strlen(payload) : 0,
        &status, headers, sizeof(headers), &body, &len, allocate, NULL) == NJ_OK);
    CHECK(status == expected);
    CHECK(strstr(headers, "X-Reply: loopback\n") != NULL);
    CHECK(len == strlen(response));
    CHECK(!len || !memcmp(body, response, len));
    free(body);
    return 0;
}
/* Cross the public GCF/native bridge as well as the port callbacks. */
static int java_http(nj_port *p, const char *base)
{
    static const char *paths[] = {"/get", "/post", "/missing"};
    static const char *replies[] = {"hello", "post=ok", "missing"};
    static const int codes[] = {200, 201, 404};
    const char *http_cls = "javax/microedition/io/HttpConnection";
    nj_vm *vm = nj_vm_create(p, 2u * 1024u * 1024u);
    int i, j;
    CHECK(vm);
    for (i = 0; i < 3; ++i) {
        char url[1024];
        nj_value args[2], result;
        nj_ref conn, stream;
        nj_u4 roots = nj_root_mark(vm);
        snprintf(url, sizeof(url), "%s%s", base, paths[i]);
        args[0].r = nj_string_new(vm, url);
        CHECK(args[0].r); nj_root(vm, args[0].r);
        CHECK(nj_call_static(vm, "javax/microedition/io/Connector", "open",
            "(Ljava/lang/String;)Ljavax/microedition/io/Connection;", args, &result) == NJ_OK);
        conn = result.r; CHECK(conn); nj_root(vm, conn);
        args[0].r = nj_string_new(vm, "X-Probe");
        CHECK(args[0].r); nj_root(vm, args[0].r);
        args[1].r = nj_string_new(vm, "loopback");
        CHECK(args[1].r); nj_root(vm, args[1].r);
        CHECK(nj_call(vm, conn, http_cls, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V", args, NULL) == NJ_OK);
        if (i == 1) {
            args[0].r = nj_string_new(vm, "POST");
            CHECK(args[0].r); nj_root(vm, args[0].r);
            CHECK(nj_call(vm, conn, http_cls, "setRequestMethod", "(Ljava/lang/String;)V", args, NULL) == NJ_OK);
            CHECK(nj_call(vm, conn, http_cls, "openOutputStream", "()Ljava/io/OutputStream;", NULL, &result) == NJ_OK);
            stream = result.r; CHECK(stream); nj_root(vm, stream);
            for (j = 0; replies[i][j]; ++j) {
                args[0].i = (unsigned char)replies[i][j];
                CHECK(nj_call(vm, stream, "java/io/OutputStream", "write", "(I)V", args, NULL) == NJ_OK);
            }
            CHECK(nj_call(vm, stream, "java/io/OutputStream", "close", "()V", NULL, NULL) == NJ_OK);
        }
        CHECK(nj_call(vm, conn, http_cls, "getResponseCode", "()I", NULL, &result) == NJ_OK);
        CHECK(result.i == codes[i]);
        {
            /* The two header getters the core audit reported missing, plus
             * getDate() reading its header instead of returning -1. */
            nj_value ha[2];

            ha[0].r = nj_string_new(vm, "X-Int");
            CHECK(ha[0].r); nj_root(vm, ha[0].r);
            ha[1].i = -7;
            CHECK(nj_call(vm, conn, http_cls, "getHeaderFieldInt",
                "(Ljava/lang/String;I)I", ha, &result) == NJ_OK);
            CHECK(result.i == 42);
            ha[0].r = nj_string_new(vm, "No-Such-Header");
            CHECK(ha[0].r); nj_root(vm, ha[0].r);
            CHECK(nj_call(vm, conn, http_cls, "getHeaderFieldInt",
                "(Ljava/lang/String;I)I", ha, &result) == NJ_OK);
            CHECK(result.i == -7);
            ha[0].r = nj_string_new(vm, "X-Date");
            CHECK(ha[0].r); nj_root(vm, ha[0].r);
            ha[1].j = 0;
            CHECK(nj_call(vm, conn, http_cls, "getHeaderFieldDate",
                "(Ljava/lang/String;J)J", ha, &result) == NJ_OK);
            CHECK(result.j == 784111777000LL);
            ha[0].r = nj_string_new(vm, "No-Such-Header");
            CHECK(ha[0].r); nj_root(vm, ha[0].r);
            ha[1].j = 123;
            CHECK(nj_call(vm, conn, http_cls, "getHeaderFieldDate",
                "(Ljava/lang/String;J)J", ha, &result) == NJ_OK);
            CHECK(result.j == 123);
            CHECK(nj_call(vm, conn, http_cls, "getDate", "()J", NULL, &result) == NJ_OK);
            CHECK(result.j > 0);
        }
        CHECK(nj_call(vm, conn, http_cls, "openInputStream", "()Ljava/io/InputStream;", NULL, &result) == NJ_OK);
        stream = result.r; CHECK(stream); nj_root(vm, stream);
        for (j = 0; replies[i][j]; ++j) {
            CHECK(nj_call(vm, stream, "java/io/InputStream", "read", "()I", NULL, &result) == NJ_OK);
            CHECK(result.i == (unsigned char)replies[i][j]);
        }
        CHECK(nj_call(vm, stream, "java/io/InputStream", "read", "()I", NULL, &result) == NJ_OK);
        CHECK(result.i == -1);
        CHECK(nj_call(vm, stream, "java/io/InputStream", "close", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(vm, conn, http_cls, "close", "()V", NULL, NULL) == NJ_OK);
        CHECK(vm->exception == NULL);
        nj_root_release(vm, roots);
    }
    nj_vm_destroy(vm);
    return 0;
}
int main(int argc, char **argv)
{
    nj_port p;
    nj_posix_network net = {0};
    nj_socket_info info;
    void *h;
    char err[256];
    nj_u1 buf[64], *large;
    int n, total;
    CHECK(argc == 5);
    CHECK(nj_port_sdl2(&p, "Network loopback") == NJ_OK);
    CHECK(p.http_request && p.socket_connect && p.socket_available);
    CHECK(!http(&p, argv[1], "/get", "GET", NULL, 200, "hello"));
    CHECK(!http(&p, argv[1], "/post", "POST", "a=中文", 201, "a=中文"));
    CHECK(!http(&p, argv[1], "/missing", "GET", NULL, 404, "missing"));
    CHECK(!http(&p, argv[1], "/head", "HEAD", NULL, 200, ""));
    CHECK(!http(&p, argv[1], "/chunked", "GET", NULL, 200, "chunked-body"));
    h = p.socket_connect(p.ud, argv[3], 1000, &info, err, sizeof(err));
    CHECK(h && info.port > 0 && info.local_port > 0);
    CHECK(!strcmp(info.address, "127.0.0.1"));
    CHECK(p.socket_read(p.ud, h, buf, sizeof(buf), 40) == 0);
    large = malloc(4*1024*1024);
    CHECK(large);
    memset(large, 'x', 4*1024*1024);
    CHECK(p.socket_write(p.ud, h, large, 4*1024*1024) == 4*1024*1024);
    free(large);
    total = 0;
    do {
        n = p.socket_available(p.ud, h);
        CHECK(n >= 0);
        n = p.socket_read(p.ud, h, buf + total, (int)sizeof(buf)-total, 1000);
        CHECK(n != 0);
        if (n > 0) total += n;
    } while (n > 0);
    CHECK(n == -1 && total == 7 && !memcmp(buf, "4194304", 7));
    p.socket_close(p.ud, h);
    h = p.socket_connect(p.ud, argv[3], 1000, &info, err, sizeof(err));
    CHECK(h); /* Deliberately left open: port shutdown must close it. */
    p.shutdown(p.ud);
    CHECK(nj_posix_network_init(&net) == NJ_OK);
    memset(&p, 0, sizeof(p));
    p.ud = &net; p.http_request = nj_posix_http_request;
    /* A self-signed certificate fails until explicitly trusted. */
    {
        nj_u1 *body = NULL; nj_u4 len = 0; int status = 0;
        CHECK(nj_posix_http_request(&net, argv[2], "GET", "", NULL, 0,
            &status, err, sizeof(err), &body, &len, allocate, NULL) != NJ_OK);
        CHECK(!body && !status && !len);
    }
    net.ca_file = argv[4];
    CHECK(!http(&p, argv[2], "/get", "GET", NULL, 200, "hello"));
    net.timeout_ms = 80;
    {
        nj_u1 *body = NULL; nj_u4 len = 0; int status = 0;
        char url[1024];
        snprintf(url, sizeof(url), "%s/stall", argv[1]);
        CHECK(nj_posix_http_request(&net, url, "GET", "", NULL, 0,
            &status, err, sizeof(err), &body, &len, allocate, NULL) != NJ_OK);
        CHECK(!body && !status && !len);
    }
    h = nj_posix_socket_connect(&net, argv[3], 1000, &info, err, sizeof(err));
    CHECK(h);
    large = malloc(32*1024*1024);
    CHECK(large);
    memset(large, 'y', 32*1024*1024);
    CHECK(nj_posix_socket_write(&net, h, large, 32*1024*1024) < 0);
    free(large);
    nj_posix_network_destroy(&net);
    CHECK(!net.sockets && !net.initialized);
    CHECK(nj_port_headless(&p) == NJ_OK);
    CHECK(!p.http_request && !p.socket_connect);
    CHECK(nj_port_headless_enable_network(&p) == NJ_OK);
    CHECK(p.http_request && p.socket_connect);
    CHECK(!http(&p, argv[1], "/get", "GET", NULL, 200, "hello"));
    CHECK(nj_port_headless_enable_network(&p) == NJ_OK);
    CHECK(!java_http(&p, argv[1]));
    p.shutdown(p.ud);
    puts("POSIX network: SDL HTTP/status/headers/POST/HEAD/chunked, TLS trust, timeout, TCP large write/read/EOF/reopen/cleanup, VM GCF GET/POST/404 streams passed");
    return 0;
}
