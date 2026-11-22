/*
 * nano_j2me - src/midp/nj_lib_pki.c
 *
 * JSR-177 (SATS) javax.microedition.pki: the certificate model and the
 * CertificateException type.
 *
 * WHY THIS EXISTS
 * ---------------
 * The round-179 corpus audit over 1048 JARs found exactly two JARs with a
 * class-linkage gap: Doom3 references three JSR-226 SVG symbols it never
 * loads, and IstanBUL guards its HTTPS bootstrap with a catch clause typed
 * javax.microedition.pki.CertificateException.  A catch clause is resolved
 * while the exception is being dispatched, so without the class an unrelated
 * exception escaping that region turns into a confusing NoClassDefFoundError
 * instead of the IOException branch the title expects.
 *
 * WHAT THIS DOES NOT DO
 * ---------------------
 * No secure connections, keystores or certificate parsing: the runtime has no
 * JSR-177 implementation, and https:// URLs are answered by Connector as
 * unsupported.  Certificate exists only so the exception type can refer to it
 * and so code that declares the interface links.  Nothing here invents a real
 * certificate; the accessors are surface only.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_PKICERT "javax/microedition/pki/Certificate"
#define NJ_C_PKIEXC  "javax/microedition/pki/CertificateException"

#define DESC_CERT "L" NJ_C_PKICERT ";"

/* ------------------------------------------------------------------ */
/* CertificateException                                                */
/* ------------------------------------------------------------------ */

static nj_field *pki_exc_field(nj_vm *vm, const char *name, const char *desc,
                               int is_static)
{
    return nj_field_lookup(vm, NJ_C_PKIEXC, name, desc, is_static);
}

/* Store the message in Throwable's field the way the built-in Throwable does,
 * so getMessage() answers what the middleweight ctors were given. */
static void pki_store_message(nj_vm *vm, nj_ref self, nj_ref msg)
{
    nj_field *f = nj_field_lookup(vm, "java/lang/Throwable", "detailMessage",
                                  "Ljava/lang/String;", 0);
    if (f) {
        nj_rput(vm, self, f, msg);
    }
}

NJFN(pki_exc_init0)
{
    NJ_UNUSED(args);
    pki_store_message(vm, NJ_THIS(args), NULL);
    return NJ_OK;
}

NJFN(pki_exc_init_msg)
{
    pki_store_message(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
    return NJ_OK;
}

static void pki_store_reason_cert(nj_vm *vm, nj_ref self, nj_s4 reason,
                                  nj_ref cert)
{
    nj_field *f = pki_exc_field(vm, "reason", "I", 0);
    if (f) {
        nj_iput(vm, self, f, reason);
    }
    f = pki_exc_field(vm, "certificate", DESC_CERT, 0);
    if (f) {
        nj_rput(vm, self, f, cert);
    }
}

NJFN(pki_exc_init_reason_cert)
{
    pki_store_message(vm, NJ_THIS(args), NULL);
    pki_store_reason_cert(vm, NJ_THIS(args), NJ_ARG_INT(args, 0),
                          NJ_ARG_REF(args, 1));
    return NJ_OK;
}

NJFN(pki_exc_init_msg_reason_cert)
{
    pki_store_message(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
    pki_store_reason_cert(vm, NJ_THIS(args), NJ_ARG_INT(args, 1),
                          NJ_ARG_REF(args, 2));
    return NJ_OK;
}

NJFN(pki_exc_reason)
{
    nj_field *f = pki_exc_field(vm, "reason", "I", 0);
    ret->i = f ? nj_iget(vm, NJ_THIS(args), f) : 0;
    return NJ_OK;
}

NJFN(pki_exc_cert)
{
    nj_field *f = pki_exc_field(vm, "certificate", DESC_CERT, 0);
    ret->r = f ? nj_rget(vm, NJ_THIS(args), f) : NULL;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define F(name, desc, flags) { name, desc, flags }

static const nj_builtin_method pki_cert_methods[] = {
    M("getType", "()B", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getVersion", "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getSerialNumber", "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getIssuer", "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getSubject", "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getSigAlgName", "()Ljava/lang/String;", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getNotBefore", "()J", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT),
    M("getNotAfter", "()J", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_field pki_exc_fields[] = {
    F("reason", "I", NJ_ACC_PRIVATE),
    F("certificate", DESC_CERT, NJ_ACC_PRIVATE)
};

static const nj_builtin_method pki_exc_methods[] = {
    M("<init>", "()V", 0),
    M("<init>", "(Ljava/lang/String;)V", 0),
    M("<init>", "(I" DESC_CERT ")V", 0),
    M("<init>", "(Ljava/lang/String;I" DESC_CERT ")V", 0),
    M("getReason", "()I", NJ_ACC_PUBLIC),
    M("getCertificate", "()" DESC_CERT, NJ_ACC_PUBLIC)
};

const nj_builtin_class nj_lib_pki_classes[] = {
    { NJ_C_PKICERT, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, pki_cert_methods,
      (nj_u2)(sizeof(pki_cert_methods) / sizeof(pki_cert_methods[0])) },
    { NJ_C_PKIEXC, "java/lang/Exception", NJ_ACC_PUBLIC,
      pki_exc_fields,
      (nj_u2)(sizeof(pki_exc_fields) / sizeof(pki_exc_fields[0])),
      pki_exc_methods,
      (nj_u2)(sizeof(pki_exc_methods) / sizeof(pki_exc_methods[0])) }
};
const int nj_lib_pki_class_count =
    (int)(sizeof(nj_lib_pki_classes) / sizeof(nj_lib_pki_classes[0]));

const nj_native_entry nj_lib_pki_table[] = {
    { NJ_C_PKIEXC, "<init>", "()V", pki_exc_init0 },
    { NJ_C_PKIEXC, "<init>", "(Ljava/lang/String;)V", pki_exc_init_msg },
    { NJ_C_PKIEXC, "<init>", "(I" DESC_CERT ")V", pki_exc_init_reason_cert },
    { NJ_C_PKIEXC, "<init>", "(Ljava/lang/String;I" DESC_CERT ")V",
      pki_exc_init_msg_reason_cert },
    { NJ_C_PKIEXC, "getReason", "()I", pki_exc_reason },
    { NJ_C_PKIEXC, "getCertificate", "()" DESC_CERT, pki_exc_cert }
};
const int nj_lib_pki_count =
    (int)(sizeof(nj_lib_pki_table) / sizeof(nj_lib_pki_table[0]));
