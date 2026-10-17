/* Socket InputStream.available() must support polling without consuming data. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <string.h>

struct socket_state { int bytes, queries, reads, closes; };
static void *connect_stub(void *ud, const char *url, nj_u4 timeout,
                          nj_socket_info *info, char *err, nj_size cap)
{
    (void)url; (void)timeout; (void)err; (void)cap;
    memset(info, 0, sizeof(*info));
    return ud;
}
static int available_stub(void *ud, void *handle)
{
    struct socket_state *s = (struct socket_state *)ud;
    if (handle != ud) return -1;
    s->queries++;
    return s->bytes;
}
static int read_stub(void *ud, void *handle, nj_u1 *buf, int len, nj_u4 timeout)
{
    struct socket_state *s = (struct socket_state *)ud;
    (void)timeout;
    if (handle != ud || len < 1 || s->bytes <= 0) return -1;
    s->reads++;
    s->bytes--;
    buf[0] = 0xE4;
    return 1;
}
static int write_stub(void *ud, void *handle, const nj_u1 *buf, int len)
{ (void)ud; (void)handle; (void)buf; return len; }
static void close_stub(void *ud, void *handle)
{
    struct socket_state *s = (struct socket_state *)ud;
    if (handle == ud) s->closes++;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failed++; } } while (0)
static int is_io_error(nj_vm *vm)
{
    nj_ref e = nj_take_exception(vm);
    return e && e->cls && strcmp(e->cls->name, "java/io/IOException") == 0;
}
static nj_ref open_stream(nj_vm *vm, nj_ref *conn)
{
    nj_value a, rv;
    a.r = nj_string_new(vm, "socket://localhost:1234");
    if (nj_call_static(vm, "javax/microedition/io/Connector", "open",
                       "(Ljava/lang/String;)Ljavax/microedition/io/Connection;",
                       &a, &rv) != NJ_OK) return NULL;
    *conn = rv.r;
    if (nj_call(vm, *conn, "javax/microedition/io/SocketConnection", "openInputStream",
                "()Ljava/io/InputStream;", NULL, &rv) != NJ_OK) return NULL;
    return rv.r;
}
int main(void)
{
    struct socket_state state = {3, 0, 0, 0};
    nj_port port;
    nj_vm *vm;
    nj_ref conn = NULL, stream;
    nj_value rv;
    int failed = 0;
    memset(&port, 0, sizeof(port));
    port.ud = &state;
    port.socket_connect = connect_stub;
    port.socket_read = read_stub;
    port.socket_write = write_stub;
    port.socket_close = close_stub;
    port.socket_available = available_stub;
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) return 1;
    stream = open_stream(vm, &conn);
    CHECK(stream != NULL);
    if (!stream) { nj_vm_destroy(vm); return 1; }
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) == NJ_OK && rv.i == 3);
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) == NJ_OK && rv.i == 3);
    CHECK(state.queries == 2 && state.reads == 0);
    CHECK(nj_call(vm, stream, "java/io/InputStream", "read", "()I", NULL, &rv) == NJ_OK && rv.i == 0xE4);
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) == NJ_OK && rv.i == 2);
    state.bytes = 0;
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) == NJ_OK && rv.i == 0);
    state.bytes = -1;
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) != NJ_OK && is_io_error(vm));
    CHECK(nj_call(vm, conn, "javax/microedition/io/SocketConnection", "close", "()V", NULL, NULL) == NJ_OK);
    CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) != NJ_OK && is_io_error(vm));
    CHECK(state.closes == 1);
    nj_vm_destroy(vm);

    /* Missing optional hook preserves the old conservative answer, but must
     * still reject a closed input stream without closing its connection. */
    port.socket_available = NULL;
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) return 1;
    stream = open_stream(vm, &conn);
    CHECK(stream != NULL);
    if (stream) {
        CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) == NJ_OK && rv.i == 0);
        CHECK(nj_call(vm, stream, "java/io/InputStream", "close", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(vm, stream, "java/io/InputStream", "available", "()I", NULL, &rv) != NJ_OK && is_io_error(vm));
        CHECK(nj_call(vm, conn, "javax/microedition/io/SocketConnection", "close", "()V", NULL, NULL) == NJ_OK);
    }
    nj_vm_destroy(vm);
    printf("socket available: %s (%d failures)\n", failed ? "FAIL" : "PASS", failed);
    return failed ? 1 : 0;
}
