/*
 * javax.microedition.pki (JSR-177): the class-linkage surface the corpus needs.
 *
 * IstanBUL catches CertificateException around its HTTPS bootstrap, so the
 * class must resolve during exception dispatch and its constructors must
 * build a Throwable whose message/reason/certificate accessors answer.  No
 * secure connection implementation is claimed; this pins what the runtime
 * promises.
 */
#include <stdio.h>
#include <string.h>

#include "nj2me/nj.h"

#include "nj_internal.h"
#include "nj_midp.h"

static int g_pass;
static int g_fail;

#define OK(cond, msg)                                                          \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %d: %s\n", __LINE__, (msg));                          \
        }                                                                      \
    } while (0)

#define PKI_CERT "javax/microedition/pki/Certificate"
#define PKI_EXC  "javax/microedition/pki/CertificateException"
#define DESC_CERT "L" PKI_CERT ";"

/* Build an exception through one of its constructors, the way Java code would
 * and the way nj_exception_new() does for the (String) shape. */
static nj_ref new_exc(nj_vm *vm, const char *desc, nj_value *args)
{
    nj_class *c = nj_class_lookup(vm, PKI_EXC);
    nj_ref obj;

    if (!c) {
        return NULL;
    }
    obj = nj_new_instance(vm, c);
    if (!obj) {
        return NULL;
    }
    if (nj_call_special(vm, obj, PKI_EXC, "<init>", desc, args, NULL) != NJ_OK) {
        return NULL;
    }
    return obj;
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_class *c;
    nj_ref exc;
    nj_value r;
    nj_value args[4];

    printf("== nano_j2me javax.microedition.pki test ==\n");

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

    c = nj_class_lookup(vm, PKI_CERT);
    OK(c != NULL, "javax.microedition.pki.Certificate exists");
    OK(c && (c->flags & NJ_ACC_INTERFACE) != 0u, "Certificate is an interface");
    OK(nj_class_lookup(vm, PKI_EXC) != NULL,
       "javax.microedition.pki.CertificateException exists");
    c = nj_class_lookup(vm, PKI_EXC);
    OK(c && c->super_name && strcmp(c->super_name, "java/lang/Exception") == 0,
       "CertificateException extends java.lang.Exception");

    /* (String) constructor, as used by nj_exception_new() and by dispatch-time
     * message enrichment. */
    memset(args, 0, sizeof args);
    args[0].r = nj_string_new(vm, "no trust anchor");
    exc = new_exc(vm, "(Ljava/lang/String;)V", args);
    OK(exc != NULL, "CertificateException(String) constructs");
    r.r = NULL;
    OK(nj_call(vm, exc, PKI_EXC, "getMessage", "()Ljava/lang/String;", NULL,
               &r) == NJ_OK,
       "getMessage() is callable");
    OK(r.r != NULL && nj_string_equals_utf8(r.r, "no trust anchor"),
       "getMessage() returns the constructor message");
    OK(nj_call(vm, exc, PKI_EXC, "getReason", "()I", NULL, &r) == NJ_OK &&
       r.i == 0, "getReason() defaults to 0");
    OK(nj_call(vm, exc, PKI_EXC, "getCertificate", "()" DESC_CERT, NULL,
               &r) == NJ_OK && r.r == NULL, "getCertificate() defaults to null");

    /* (int, Certificate) shape. */
    memset(args, 0, sizeof args);
    args[0].i = 4;
    args[1].r = NULL;
    exc = new_exc(vm, "(I" DESC_CERT ")V", args);
    OK(exc != NULL, "CertificateException(int, Certificate) constructs");
    OK(nj_call(vm, exc, PKI_EXC, "getReason", "()I", NULL, &r) == NJ_OK &&
       r.i == 4, "the reason is kept");
    OK(nj_call(vm, exc, PKI_EXC, "getMessage", "()Ljava/lang/String;", NULL,
               &r) == NJ_OK && r.r == NULL, "no message for the short shape");

    /* (String, int, Certificate) shape. */
    memset(args, 0, sizeof args);
    args[0].r = nj_string_new(vm, "expired");
    args[1].i = 7;
    args[2].r = NULL;
    exc = new_exc(vm, "(Ljava/lang/String;I" DESC_CERT ")V", args);
    OK(exc != NULL, "CertificateException(String, int, Certificate) constructs");
    OK(nj_call(vm, exc, PKI_EXC, "getReason", "()I", NULL, &r) == NJ_OK &&
       r.i == 7, "the reason from the long shape is kept");
    OK(nj_call(vm, exc, PKI_EXC, "getMessage", "()Ljava/lang/String;", NULL,
               &r) == NJ_OK &&
       r.r != NULL && nj_string_equals_utf8(r.r, "expired"),
       "the message from the long shape is kept");

    /* Raising and taking it preserves the type, which is what a catch clause
     * typed CertificateException dispatches on. */
    OK(nj_throw(vm, PKI_EXC, "raised") != NJ_OK,
       "raising it reports an exception");
    OK(nj_has_exception(vm), "the exception is pending");
    exc = nj_take_exception(vm);
    OK(exc != NULL, "it can be taken");
    OK(exc && strcmp(nj_ref_class_name(exc), PKI_EXC) == 0,
       "the taken exception keeps its class");
    OK(!nj_has_exception(vm), "taking clears the pending exception");

    /* The default constructor must also work (some libraries instantiate
     * exceptions reflectively through newInstance). */
    memset(args, 0, sizeof args);
    exc = new_exc(vm, "()V", args);
    OK(exc != NULL, "CertificateException() constructs");

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
