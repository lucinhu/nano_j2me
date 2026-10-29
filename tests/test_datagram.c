/*
 * javax.microedition.io.DatagramConnection: the UDP transport.
 *
 * Minecraft PE 0.6.1 opens "datagram://:19132" for LAN play and sends RakNet
 * packets through the Datagram the connection hands out.  Before this test the
 * scheme was refused by Connector and the game reported a caught
 * ConnectionNotFoundException.
 *
 * The test drives two real loopback sockets in one VM: a server bound to an
 * ephemeral port, and a client that sends to it and receives a reply.  It also
 * pins the cooperative receive: with nothing waiting, receive() records an
 * empty datagram and parks the thread (NJ_ERR_TIMEOUT) instead of blocking the
 * whole VM in C.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "nj2me/nj.h"

#include "nj_internal.h"
#include "nj_midp.h"

#define CONN "javax/microedition/io/Connector"
#define DGI  "javax/microedition/io/DatagramConnectionImpl"
#define DG   "javax/microedition/io/Datagram"
#define DGV  "javax/microedition/io/DatagramImpl"

#define OPEN_DESC "(Ljava/lang/String;)Ljavax/microedition/io/Connection;"
#define NEW_DG    "([BILjava/lang/String;)Ljavax/microedition/io/Datagram;"
#define SEND_DESC "(L" DG ";)V"
#define RECV_DESC "(L" DG ";)V"

static int g_pass;
static int g_fail;

#define OK(cond, msg)                                                          \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                         \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %d: %s\n", __LINE__, (msg));                          \
        }                                                                      \
    } while (0)

static nj_ref open_conn(nj_vm *vm, const char *url)
{
    nj_value a;
    nj_value r;

    a.r = nj_string_new(vm, url);
    r.r = NULL;
    if (nj_call_static(vm, CONN, "open", OPEN_DESC, &a, &r) != NJ_OK) {
        return NULL;
    }
    return r.r;
}

static nj_ref call_ref(nj_vm *vm, nj_ref recv, const char *cls,
                       const char *name, const char *desc, nj_value *args)
{
    nj_value r;

    r.r = NULL;
    if (nj_call(vm, recv, cls, name, desc, args, &r) != NJ_OK) {
        return NULL;
    }
    return r.r;
}

static nj_s4 call_int(nj_vm *vm, nj_ref recv, const char *cls,
                      const char *name, const char *desc)
{
    nj_value r;

    r.i = -1;
    if (nj_call(vm, recv, cls, name, desc, NULL, &r) != NJ_OK) {
        return -1;
    }
    return r.i;
}

/* Receive with the loopback delivery latency in mind.  A real VM re-runs the
 * suspended thread; here each NJ_ERR_TIMEOUT means "nothing yet" and the host
 * sleep stands in for the scheduler tick. */
static nj_status recv_with_retry(nj_vm *vm, nj_ref conn, nj_ref dgram)
{
    nj_value a;
    nj_status st = NJ_ERR;
    int tries;

    a.r = dgram;
    for (tries = 0; tries < 200; tries++) {
        st = nj_call(vm, conn, DGI, "receive", RECV_DESC, &a, NULL);
        if (st == NJ_OK) {
            return NJ_OK;
        }
        if (st != NJ_ERR_TIMEOUT) {
            return st;
        }
        {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 2000000L;
            (void)nanosleep(&ts, NULL);
        }
    }
    return st;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref server = NULL, client = NULL, empty = NULL;
    nj_ref dgram = NULL, reply = NULL;
    nj_ref payload, reply_payload;
    nj_s4 port_no, max_len, n;
    char url[64], peer[96];
    nj_value a[3];
    nj_s4 *p;
    int i;

    printf("== nano_j2me DatagramConnection test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    /* A malformed URL is refused by name, not silently. */
    OK(open_conn(vm, "datagram://nocolon") == NULL,
       "a datagram URL without a port is refused");
    {
        const char *d = nj_exception_describe(vm);
        OK(d != NULL && strstr(d, "ConnectionNotFoundException") != NULL,
           "the refusal is ConnectionNotFoundException");
        nj_clear_exception(vm);
    }

    server = nj_root(vm, open_conn(vm, "datagram://:0"));
    OK(server != NULL, "a wildcard server socket opens");
    if (!server) {
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    port_no = call_int(vm, server, DGI, "getLocalPort", "()I");
    OK(port_no > 0, "the bound port is reported");
    max_len = call_int(vm, server, DGI, "getMaximumLength", "()I");
    OK(max_len > 0, "getMaximumLength() is positive");

    client = nj_root(vm, open_conn(vm, "datagram://:0"));
    OK(client != NULL, "a client socket opens");
    if (!client) {
        goto done;
    }

    payload = nj_root(vm, nj_byte_array_new(vm, 8u));
    OK(payload != NULL, "a payload buffer exists");
    if (!payload) {
        goto done;
    }
    p = (nj_s4 *)nj_array_data(payload);
    for (i = 0; i < 8; i++) {
        ((nj_u1 *)nj_array_data(payload))[i] = (nj_u1)(0x40 + i);
    }
    (void)p;

    snprintf(peer, sizeof(peer), "datagram://127.0.0.1:%d", (int)port_no);
    a[0].r = payload;
    a[1].i = 8;
    a[2].r = nj_root(vm, nj_string_new(vm, peer));
    dgram = nj_root(vm, call_ref(vm, client, DGI, "newDatagram", NEW_DG, a));
    OK(dgram != NULL, "newDatagram(buffer, length, address) succeeds");
    if (!dgram) {
        goto done;
    }
    OK(call_int(vm, dgram, DGV, "getLength", "()I") == 8,
       "the new datagram carries the requested length");

    a[0].r = dgram;
    OK(nj_call(vm, client, DGI, "send", SEND_DESC, a, NULL) == NJ_OK,
       "send() succeeds");
    OK(recv_with_retry(vm, server, dgram) == NJ_OK, "the server receives it");
    OK(call_int(vm, dgram, DGV, "getLength", "()I") == 8,
       "the received length is the datagram's");
    {
        nj_ref data = call_ref(vm, dgram, DGV, "getData", "()[B", NULL);
        OK(data != NULL && data->length >= 8,
           "getData() returns the receive buffer");
        if (data) {
            int same = 1;
            for (i = 0; i < 8; i++) {
                if (data->u.bytes[i] != (nj_u1)(0x40 + i)) {
                    same = 0;
                }
            }
            OK(same, "the received bytes are the ones that were sent");
        }
    }
    {
        nj_ref addr = call_ref(vm, dgram, DGV, "getAddress", "()Ljava/lang/String;", NULL);
        char buf[96];
        OK(addr != NULL, "the sender address is recorded");
        if (addr) {
            buf[0] = '\0';
            nj_string_to_utf8(addr, buf, (int)sizeof(buf));
            OK(strncmp(buf, "datagram://127.0.0.1:", 21) == 0,
               "the address is a numeric datagram URL");
        }
    }

    /* Reply to the recorded address, which is how the game answers a peer. */
    reply_payload = nj_root(vm, nj_byte_array_new(vm, 4u));
    OK(reply_payload != NULL, "a reply buffer exists");
    if (!reply_payload) {
        goto done;
    }
    for (i = 0; i < 4; i++) {
        ((nj_u1 *)nj_array_data(reply_payload))[i] = (nj_u1)(0x90 + i);
    }
    a[0].r = reply_payload;
    a[1].i = 4;
    a[2].r = call_ref(vm, dgram, DGV, "getAddress", "()Ljava/lang/String;", NULL);
    reply = nj_root(vm, call_ref(vm, server, DGI, "newDatagram", NEW_DG, a));
    OK(reply != NULL, "the server builds a reply datagram");
    if (!reply) {
        goto done;
    }
    a[0].r = reply;
    OK(nj_call(vm, server, DGI, "send", SEND_DESC, a, NULL) == NJ_OK,
       "the server sends the reply");
    OK(recv_with_retry(vm, client, reply) == NJ_OK, "the client receives it");
    OK(call_int(vm, reply, DGV, "getLength", "()I") == 4,
       "the reply length is kept");

    /* Datagram mutators. */
    a[0].r = payload;
    a[1].i = 0;
    a[2].i = 8;
    OK(nj_call(vm, dgram, DGV, "setData", "([BII)V", a, NULL) == NJ_OK,
       "setData(buffer, offset, length) is accepted");
    OK(call_int(vm, dgram, DGV, "getOffset", "()I") == 0,
       "getOffset() reports the offset");
    a[0].i = 4;
    OK(nj_call(vm, dgram, DGV, "setLength", "(I)V", a, NULL) == NJ_OK &&
       call_int(vm, dgram, DGV, "getLength", "()I") == 4,
       "setLength() is reflected by getLength()");

    /* A closed connection refuses more traffic. */
    OK(nj_call(vm, client, DGI, "close", "()V", NULL, NULL) == NJ_OK,
       "close() succeeds");
    OK(nj_call(vm, client, DGI, "close", "()V", NULL, NULL) == NJ_OK,
       "close() is idempotent");
    a[0].r = dgram;
    OK(nj_call(vm, client, DGI, "send", SEND_DESC, a, NULL) != NJ_OK,
       "a closed connection refuses send()");
    {
        const char *d = nj_exception_describe(vm);
        OK(d != NULL && strstr(d, "IOException") != NULL,
           "the refusal is an IOException");
        nj_clear_exception(vm);
    }

    /* With nothing waiting, receive() reports an empty datagram and parks the
     * thread instead of blocking the VM inside C. */
    empty = nj_root(vm, open_conn(vm, "datagram://:0"));
    OK(empty != NULL, "an idle socket opens");
    if (empty) {
        nj_ref idle = nj_root(vm, nj_byte_array_new(vm, 32u));
        OK(idle != NULL, "an idle receive buffer exists");
        if (idle) {
            a[0].r = idle;
            a[1].i = 32;
            a[2].r = NULL;
            dgram = nj_root(vm, call_ref(vm, empty, DGI, "newDatagram", NEW_DG, a));
            OK(dgram != NULL, "an idle datagram is created");
            if (dgram) {
                a[0].r = dgram;
                /*
                 * Call the native directly: the public C bridge treats a
                 * blocked native as completed (a C caller cannot be suspended),
                 * so only nj_invoke_native() can observe the cooperative park.
                 */
                {
                    nj_thread *th = nj_thread_create(vm, NULL, 0);
                    nj_class *c = nj_class_lookup(
                        vm, "javax/microedition/io/DatagramConnectionImpl");
                    nj_method *m = c ? nj_method_lookup(c, "receive", RECV_DESC)
                                     : NULL;
                    nj_value nret[2];
                    nj_status rst = NJ_ERR;

                    OK(th != NULL && m != NULL, "the receive native is reachable");
                    if (th && m) {
                        /* The argument area must live in the thread's own
                         * stack, which is where nj_frame_push() expects it. */
                        th->stack_mem[0].r = empty;
                        th->stack_mem[1].r = dgram;
                        nret[0].bits = 0u;
                        rst = nj_invoke_native(vm, th, m, th->stack_mem, 2u,
                                               nret);
                        OK(rst == NJ_ERR_TIMEOUT,
                           "receive() parks the thread when nothing is waiting");
                    }
                }
                OK(call_int(vm, dgram, DGV, "getLength", "()I") == 0,
                   "the empty datagram has length zero");
            }
        }
    }

done:
    if (!nj_has_exception(vm)) {
        OK(1, "no unexpected exception");
    } else {
        OK(0, "no unexpected exception");
        nj_clear_exception(vm);
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
