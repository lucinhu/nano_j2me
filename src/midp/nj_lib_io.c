/*
 * nano_j2me - nj_lib_io.c
 *
 * java.io and the parts of javax.microedition.io a MIDlet can actually use.
 *
 * ---------------------------------------------------------------------------
 * WHY THE STREAM CLASSES ARE MOSTLY GLUE
 * ---------------------------------------------------------------------------
 * InputStream is abstract: read() is the one method a concrete stream must
 * provide.  The derived forms - read(byte[]), read(byte[],int,int), skip() -
 * are defined in terms of it, so they are implemented here by dispatching
 * read() back into Java on the receiver rather than being reimplemented per
 * subclass.  That is what makes a game's own InputStream subclass work without
 * any co-operation from this file.
 *
 * ---------------------------------------------------------------------------
 * NETWORKING
 * ---------------------------------------------------------------------------
 * Connector.open supports file:// through the port's file hooks and raises
 * ConnectionNotFoundException for everything else.  Pretending is worse than
 * refusing: a bare-metal target has no socket stack, and a game that gets a
 * silently empty stream from an http:// URL is far harder to diagnose than one
 * that throws at the point of failure.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdio.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_ISTREAM  "java/io/InputStream"
#define NJ_C_OSTREAM  "java/io/OutputStream"

/* ------------------------------------------------------------------ */
/* java.io.InputStream                                                 */
/* ------------------------------------------------------------------ */
/*
 * read(byte[],int,int) on top of read(): the only method a subclass has to
 * implement, so everything else can be expressed through it.
 */
NJFN(is_read_bytes_range)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 i;

    if (!buf) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > buf->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        ret->i = 0;
        return NJ_OK;
    }
    /* Read as much as the subclass will give us without blocking for more. */
    for (i = 0; i < len; i++) {
        nj_s4 c = nj_call_int(vm, self, NJ_C_ISTREAM, "read", "()I", NULL);
        if (vm->exception) {
            return NJ_ERR;
        }
        if (c < 0) {
            break;
        }
        buf->u.bytes[off + i] = (nj_u1)c;
    }
    ret->i = (i == 0) ? -1 : i;
    return NJ_OK;
}

NJFN(is_read_bytes)
{
    nj_value a3[3];
    a3[0].r = NJ_ARG_REF(args, 0);
    a3[1].i = 0;
    a3[2].i = a3[0].r ? (nj_s4)a3[0].r->length : 0;
    return nj_call(vm, NJ_THIS(args), NJ_C_ISTREAM, "read", "([BII)I", a3, ret);
}

NJFN(is_available)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(is_close)
{
    return NJ_OK;
}

NJFN(is_mark_supported)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(is_skip)
{
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s8 done = 0;

    while (done < want) {
        nj_s4 c = nj_call_int(vm, NJ_THIS(args), NJ_C_ISTREAM, "read", "()I",
                              NULL);
        if (vm->exception) {
            return NJ_ERR;
        }
        if (c < 0) {
            break;
        }
        done++;
    }
    ret->j = done;
    return NJ_OK;
}

/* The abstract entry point: a subclass that forgot to override it must fail
 * with something better than a wild read. */
NJFN(is_read_abstract)
{
    NJ_UNUSED(args);
    return nj_throw(vm, "java/io/IOException",
                    "read() is not implemented by this InputStream");
}

/* ------------------------------------------------------------------ */
/* java.io.OutputStream                                                */
/* ------------------------------------------------------------------ */
NJFN(os_write_bytes_range)
{
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 i;

    if (!buf) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > buf->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    for (i = 0; i < len; i++) {
        nj_value a1[1];
        a1[0].i = (nj_s4)buf->u.bytes[off + i];
        if (nj_call(vm, NJ_THIS(args), NJ_C_OSTREAM, "write", "(I)V", a1, NULL)
            != NJ_OK) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

NJFN(os_write_bytes)
{
    nj_value a3[3];
    a3[0].r = NJ_ARG_REF(args, 0);
    a3[1].i = 0;
    a3[2].i = a3[0].r ? (nj_s4)a3[0].r->length : 0;
    return nj_call(vm, NJ_THIS(args), NJ_C_OSTREAM, "write", "([BII)V", a3, ret);
}

NJFN(os_flush)
{
    return NJ_OK;
}

NJFN(os_close)
{
    return NJ_OK;
}

NJFN(os_write_abstract)
{
    NJ_UNUSED(args);
    return nj_throw(vm, "java/io/IOException",
                    "write() is not implemented by this OutputStream");
}

/* ------------------------------------------------------------------ */
/* java.io.ByteArrayInputStream                                        */
/* ------------------------------------------------------------------ */
static nj_field *fld(nj_vm *vm, const char *cls, const char *name,
                     const char *desc)
{
    return nj_field_lookup(vm, cls, name, desc, 0);
}

#define BAIS "java/io/ByteArrayInputStream"

NJFN(bais_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = NJ_ARG_REF(args, 0);

    if (!buf && NJ_ARG_INT(args, 1) != 0) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    nj_field_write_ref(vm, self, fld(vm, BAIS, "buf", "[B"), buf);
    nj_field_write_int(vm, self, fld(vm, BAIS, "pos", "I"), 0);
    nj_field_write_int(vm, self, fld(vm, BAIS, "count", "I"),
                       buf ? (nj_s4)buf->length : 0);
    nj_field_write_int(vm, self, fld(vm, BAIS, "mark", "I"), 0);
    return NJ_OK;
}

NJFN(bais_init_range)
{
    nj_status st = NJ_CALL(bais_init, vm, th, args, ret);
    if (st != NJ_OK) {
        return st;
    }
    nj_field_write_int(vm, NJ_THIS(args), fld(vm, BAIS, "pos", "I"),
                       NJ_ARG_INT(args, 1));
    nj_field_write_int(vm, NJ_THIS(args), fld(vm, BAIS, "count", "I"),
                       NJ_ARG_INT(args, 1) + NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(bais_read)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, fld(vm, BAIS, "buf", "[B"));
    nj_s4 pos = nj_field_read_int(vm, self, fld(vm, BAIS, "pos", "I"));
    nj_s4 count = nj_field_read_int(vm, self, fld(vm, BAIS, "count", "I"));

    if (!buf || pos >= count) {
        ret->i = -1;
        return NJ_OK;
    }
    ret->i = (nj_s4)buf->u.bytes[pos];
    nj_field_write_int(vm, self, fld(vm, BAIS, "pos", "I"), pos + 1);
    return NJ_OK;
}

NJFN(bais_available)
{
    nj_ref self = NJ_THIS(args);
    ret->i = nj_field_read_int(vm, self, fld(vm, BAIS, "count", "I")) -
             nj_field_read_int(vm, self, fld(vm, BAIS, "pos", "I"));
    return NJ_OK;
}

NJFN(bais_skip)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s4 pos = nj_field_read_int(vm, self, fld(vm, BAIS, "pos", "I"));
    nj_s4 count = nj_field_read_int(vm, self, fld(vm, BAIS, "count", "I"));
    nj_s4 avail = count - pos;
    nj_s4 step = (want > avail) ? avail : (nj_s4)want;

    if (step < 0) {
        step = 0;
    }
    nj_field_write_int(vm, self, fld(vm, BAIS, "pos", "I"), pos + step);
    ret->j = step;
    return NJ_OK;
}

NJFN(bais_mark)
{
    nj_field_write_int(vm, NJ_THIS(args), fld(vm, BAIS, "mark", "I"),
                       nj_field_read_int(vm, NJ_THIS(args),
                                         fld(vm, BAIS, "pos", "I")));
    return NJ_OK;
}

NJFN(bais_reset)
{
    nj_field_write_int(vm, NJ_THIS(args), fld(vm, BAIS, "pos", "I"),
                       nj_field_read_int(vm, NJ_THIS(args),
                                         fld(vm, BAIS, "mark", "I")));
    return NJ_OK;
}

NJFN(bais_mark_supported)
{
    ret->i = 1;
    return NJ_OK;
}

NJFN(bais_close)
{
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.io.ByteArrayOutputStream                                       */
/* ------------------------------------------------------------------ */
#define BAOS "java/io/ByteArrayOutputStream"

static nj_status baos_ensure(nj_vm *vm, nj_ref self, nj_s4 need)
{
    nj_field *fb = fld(vm, BAOS, "buf", "[B");
    nj_field *fc = fld(vm, BAOS, "count", "I");
    nj_ref buf = nj_field_read_ref(vm, self, fb);
    nj_s4 count = nj_field_read_int(vm, self, fc);

    if (buf && (nj_s4)buf->length >= count + need) {
        return NJ_OK;
    }
    {
        nj_s4 cap = buf ? (nj_s4)buf->length : 32;
        nj_ref nb;
        while (cap < count + need) {
            cap = cap * 2 + 32;
        }
        nb = nj_byte_array_new(vm, (nj_u4)cap);
        if (!nb) {
            return nj_throw_oom(vm);
        }
        if (buf && count > 0) {
            memcpy(nb->u.bytes, buf->u.bytes, (nj_size)count);
        }
        nj_field_write_ref(vm, self, fb, nb);
    }
    return NJ_OK;
}

NJFN(baos_init)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 cap = (args != NULL && NJ_ARG_INT(args, 0) > 0) ? NJ_ARG_INT(args, 0)
                                                          : 32;
    nj_ref buf = nj_byte_array_new(vm, (nj_u4)cap);
    if (!buf) {
        return nj_throw_oom(vm);
    }
    nj_field_write_ref(vm, self, fld(vm, BAOS, "buf", "[B"), buf);
    nj_field_write_int(vm, self, fld(vm, BAOS, "count", "I"), 0);
    return NJ_OK;
}

NJFN(baos_write)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 b = NJ_ARG_INT(args, 0);
    nj_ref buf;

    if (baos_ensure(vm, self, 1) != NJ_OK) {
        return NJ_ERR;
    }
    buf = nj_field_read_ref(vm, self, fld(vm, BAOS, "buf", "[B"));
    {
        nj_s4 count = nj_field_read_int(vm, self, fld(vm, BAOS, "count", "I"));
        buf->u.bytes[count] = (nj_u1)b;
        nj_field_write_int(vm, self, fld(vm, BAOS, "count", "I"), count + 1);
    }
    return NJ_OK;
}

NJFN(baos_write_range)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_ref buf;
    nj_s4 count;

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (baos_ensure(vm, self, len) != NJ_OK) {
        return NJ_ERR;
    }
    buf = nj_field_read_ref(vm, self, fld(vm, BAOS, "buf", "[B"));
    count = nj_field_read_int(vm, self, fld(vm, BAOS, "count", "I"));
    if (len > 0) {
        memcpy(buf->u.bytes + count, src->u.bytes + off, (nj_size)len);
    }
    nj_field_write_int(vm, self, fld(vm, BAOS, "count", "I"), count + len);
    return NJ_OK;
}

NJFN(baos_toByteArray)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, fld(vm, BAOS, "buf", "[B"));
    nj_s4 count = nj_field_read_int(vm, self, fld(vm, BAOS, "count", "I"));
    nj_ref out = nj_byte_array_new(vm, (nj_u4)count);

    if (!out) {
        return nj_throw_oom(vm);
    }
    if (count > 0 && buf) {
        memcpy(out->u.bytes, buf->u.bytes, (nj_size)count);
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(baos_size)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               fld(vm, BAOS, "count", "I"));
    return NJ_OK;
}

NJFN(baos_reset)
{
    nj_field_write_int(vm, NJ_THIS(args), fld(vm, BAOS, "count", "I"), 0);
    return NJ_OK;
}

NJFN(baos_toString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, fld(vm, BAOS, "buf", "[B"));
    nj_s4 count = nj_field_read_int(vm, self, fld(vm, BAOS, "count", "I"));
    nj_u2 *tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)count + 1u) * sizeof(nj_u2));
    nj_ref out;
    nj_s4 i;

    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0; i < count; i++) {
        tmp[i] = (nj_u2)(buf ? buf->u.bytes[i] : 0u);
    }
    out = nj_new_string_utf16(vm, tmp, count);
    nj_host_free(vm, tmp);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.io.DataOutputStream                                            */
/* ------------------------------------------------------------------ */
#define DOS "java/io/DataOutputStream"

static nj_ref dos_out(nj_vm *vm, nj_ref self)
{
    return nj_field_read_ref(vm, self, fld(vm, DOS, "out",
                                           "Ljava/io/OutputStream;"));
}

/* Write raw bytes through the wrapped stream. */
static nj_status dos_raw(nj_vm *vm, nj_ref out, const nj_u1 *p, nj_s4 n)
{
    nj_value a3[3];
    nj_ref tmp;

    if (n <= 0) {
        return NJ_OK;
    }
    tmp = nj_byte_array_new(vm, (nj_u4)n);
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    memcpy(tmp->u.bytes, p, (nj_size)n);
    a3[0].r = tmp;
    a3[1].i = 0;
    a3[2].i = n;
    return nj_call(vm, out, NJ_C_OSTREAM, "write", "([BII)V", a3, NULL);
}

NJFN(dos_write)
{
    nj_u1 b = (nj_u1)NJ_ARG_INT(args, 0);
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), &b, 1);
}

NJFN(dos_write_range)
{
    nj_ref src = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > src->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), src->u.bytes + off, len);
}

NJFN(dos_write_short)
{
    nj_u1 b[2];
    nj_s4 v = NJ_ARG_INT(args, 0);
    b[0] = (nj_u1)((v >> 8) & 0xFF);
    b[1] = (nj_u1)(v & 0xFF);
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), b, 2);
}

NJFN(dos_write_int)
{
    nj_u1 b[4];
    nj_u4 v = (nj_u4)NJ_ARG_INT(args, 0);
    b[0] = (nj_u1)(v >> 24); b[1] = (nj_u1)(v >> 16);
    b[2] = (nj_u1)(v >> 8);  b[3] = (nj_u1)v;
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), b, 4);
}

NJFN(dos_write_long)
{
    nj_u1 b[8];
    nj_u8 v = (nj_u8)NJ_ARG_LONG(args, 0);
    int i;
    for (i = 0; i < 8; i++) {
        b[i] = (nj_u1)(v >> (56 - i * 8));
    }
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), b, 8);
}

NJFN(dos_write_boolean)
{
    nj_u1 b = (nj_u1)(NJ_ARG_INT(args, 0) ? 1 : 0);
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), &b, 1);
}

NJFN(dos_write_char)
{
    nj_u1 b[2];
    nj_s4 v = NJ_ARG_INT(args, 0);
    b[0] = (nj_u1)((v >> 8) & 0xFF);
    b[1] = (nj_u1)(v & 0xFF);
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), b, 2);
}

/*
 * writeUTF uses Java's *modified* UTF-8, which is not the same as UTF-8: a NUL
 * is encoded as two bytes (0xC0 0x80) and a supplementary character as a
 * surrogate pair, each half in three bytes.  Getting this wrong produces files
 * that other J2ME tools read back corrupted, so it is spelled out here rather
 * than delegated to a generic encoder.
 */
NJFN(dos_writeUTF)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_u1 *tmp;
    nj_u4 n = 0u;
    nj_u4 i;
    nj_status st;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    /* Worst case three bytes per UTF-16 unit, plus the two length bytes. */
    tmp = (nj_u1 *)nj_host_alloc(vm, (nj_size)s->length * 3u + 2u);
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0u; i < s->length; i++) {
        nj_u4 c = nj_string_chars(s)[i];
        if (c >= 1u && c <= 0x7Fu) {
            tmp[n++] = (nj_u1)c;
        } else if (c == 0u || c <= 0x7FFu) {
            tmp[n++] = (nj_u1)(0xC0u | (c >> 6));
            tmp[n++] = (nj_u1)(0x80u | (c & 0x3Fu));
        } else {
            tmp[n++] = (nj_u1)(0xE0u | (c >> 12));
            tmp[n++] = (nj_u1)(0x80u | ((c >> 6) & 0x3Fu));
            tmp[n++] = (nj_u1)(0x80u | (c & 0x3Fu));
        }
    }
    {
        nj_u1 hdr[2];
        hdr[0] = (nj_u1)((n >> 8) & 0xFF);
        hdr[1] = (nj_u1)(n & 0xFF);
        st = dos_raw(vm, dos_out(vm, NJ_THIS(args)), hdr, 2);
        if (st == NJ_OK) {
            st = dos_raw(vm, dos_out(vm, NJ_THIS(args)), tmp, (nj_s4)n);
        }
    }
    nj_host_free(vm, tmp);
    return st;
}

NJFN(dos_flush)
{
    nj_ref out = dos_out(vm, NJ_THIS(args));
    return out ? nj_call(vm, out, NJ_C_OSTREAM, "flush", "()V", NULL, NULL)
               : NJ_OK;
}

NJFN(dos_size)
{
    ret->i = 0;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.io.DataInputStream                                             */
/* ------------------------------------------------------------------ */
#define DIS "java/io/DataInputStream"

static nj_ref dis_in(nj_vm *vm, nj_ref self)
{
    return nj_field_read_ref(vm, self, fld(vm, DIS, "in",
                                           "Ljava/io/InputStream;"));
}

static nj_s4 dis_one(nj_vm *vm, nj_ref in)
{
    nj_s4 c = nj_call_int(vm, in, NJ_C_ISTREAM, "read", "()I", NULL);
    if (vm->exception) {
        return -2;
    }
    if (c < 0) {
        nj_throw(vm, "java/io/EOFException", NULL);
        return -2;
    }
    return c;
}

NJFN(dis_read)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    return nj_call(vm, NJ_THIS(args), NJ_C_ISTREAM, "read", "()I", NULL, ret);
}

NJFN(dis_read_range)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_value a3[3];
    a3[0] = args[0];
    a3[1] = args[1];
    a3[2] = args[2];
    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    return nj_call(vm, NJ_THIS(args), NJ_C_ISTREAM, "read", "([BII)I", a3, ret);
}

NJFN(dis_readFully)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 i;

    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    if (!buf) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > buf->length) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    for (i = 0; i < len; i++) {
        nj_s4 c = dis_one(vm, in);
        if (c < 0) {
            return NJ_ERR;
        }
        buf->u.bytes[off + i] = (nj_u1)c;
    }
    return NJ_OK;
}

NJFN(dis_read_short)
{
    nj_s4 a = dis_one(vm, dis_in(vm, NJ_THIS(args)));
    nj_s4 b;
    if (a < 0) {
        return NJ_ERR;
    }
    b = dis_one(vm, dis_in(vm, NJ_THIS(args)));
    if (b < 0) {
        return NJ_ERR;
    }
    ret->i = (nj_s4)(nj_s2)((a << 8) | b);
    return NJ_OK;
}

NJFN(dis_read_unsigned_short)
{
    nj_s4 a = dis_one(vm, dis_in(vm, NJ_THIS(args)));
    nj_s4 b;
    if (a < 0) {
        return NJ_ERR;
    }
    b = dis_one(vm, dis_in(vm, NJ_THIS(args)));
    if (b < 0) {
        return NJ_ERR;
    }
    ret->i = (a << 8) | b;
    return NJ_OK;
}

NJFN(dis_read_int)
{
    nj_u4 v = 0u;
    int i;
    for (i = 0; i < 4; i++) {
        nj_s4 c = dis_one(vm, dis_in(vm, NJ_THIS(args)));
        if (c < 0) {
            return NJ_ERR;
        }
        v = (v << 8) | (nj_u4)c;
    }
    ret->i = (nj_s4)v;
    return NJ_OK;
}

NJFN(dis_read_long)
{
    nj_u8 v = 0u;
    int i;
    for (i = 0; i < 8; i++) {
        nj_s4 c = dis_one(vm, dis_in(vm, NJ_THIS(args)));
        if (c < 0) {
            return NJ_ERR;
        }
        v = (v << 8) | (nj_u8)c;
    }
    ret->j = (nj_s8)v;
    return NJ_OK;
}

NJFN(dis_read_boolean)
{
    nj_s4 c = dis_one(vm, dis_in(vm, NJ_THIS(args)));
    if (c < 0) {
        return NJ_ERR;
    }
    ret->i = c ? 1 : 0;
    return NJ_OK;
}

NJFN(dis_read_char)
{
    return NJ_CALL(dis_read_unsigned_short, vm, th, args, ret);
}

NJFN(dis_readUTF)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_s4 len = NJ_CALL(dis_read_unsigned_short, vm, th, args, ret);
    nj_u2 *tmp;
    nj_s4 i, n = 0;
    nj_ref out;

    if (len < 0 || vm->exception) {
        return NJ_ERR;
    }
    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)len + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0; i < len; i++) {
        nj_s4 c = dis_one(vm, in);
        if (c < 0) {
            nj_host_free(vm, tmp);
            return NJ_ERR;
        }
        if (c < 0x80) {
            tmp[n++] = (nj_u2)c;
        } else if ((c & 0xE0) == 0xC0) {
            nj_s4 c2 = dis_one(vm, in);
            if (c2 < 0) {
                nj_host_free(vm, tmp);
                return NJ_ERR;
            }
            tmp[n++] = (nj_u2)(((c & 0x1F) << 6) | (c2 & 0x3F));
            i++;
        } else {
            nj_s4 c2 = dis_one(vm, in);
            nj_s4 c3;
            if (c2 < 0) {
                nj_host_free(vm, tmp);
                return NJ_ERR;
            }
            c3 = dis_one(vm, in);
            if (c3 < 0) {
                nj_host_free(vm, tmp);
                return NJ_ERR;
            }
            tmp[n++] = (nj_u2)(((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) |
                               (c3 & 0x3F));
            i += 2;
        }
    }
    out = nj_new_string_utf16(vm, tmp, n);
    nj_host_free(vm, tmp);
    if (!out) {
        return nj_throw_oom(vm);
    }
    ret->r = out;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* javax.microedition.io                                               */
/* ------------------------------------------------------------------ */
#define NJ_C_FCONN "javax/microedition/io/FileConnection"

typedef struct j2me_file {
    nj_vm  *vm;
    nj_file f;
    int     use_port;
#if NJ_USE_LIBC
    FILE   *fp;         /* hosted fallback; the port's hooks win when present */
#endif
    int     readable;
    int     writable;
} j2me_file;

/*
 * javax.microedition.io.Connector.
 *
 * Only file:// is meaningful here, and even that needs a FileConnection
 * implementation that a bare-metal target has no use for.  Everything raises
 * ConnectionNotFoundException naming the URL, which is what the specification
 * requires for an unsupported scheme and what a developer needs to see: a game
 * that gets a silently empty stream from an http:// URL is far harder to
 * diagnose than one that throws at the point of failure.
 *
 * A port that does have a socket stack installs its own implementation the
 * same way the codec registry works - by declaring these methods itself.
 */
NJFN(connector_open)
{
    nj_ref url = NJ_ARG_REF(args, 0);
    char u[256];

    if (!url) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    u[0] = '\0';
    (void)nj_string_to_utf8(url, u, (int)sizeof(u));
    return nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException", u);
}

NJFN(connector_open_stream)
{
    /* Same refusal; the (String,int) and openInputStream forms all land
     * here so the message is identical whichever overload was called. */
    return NJ_CALL(connector_open, vm, th, args, ret);
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_method inputstream_methods[] = {
    M("<init>",       "()V", 0),
    M("read",         "()I", NJ_ACC_ABSTRACT),
    M("read",         "([B)I", 0),
    M("read",         "([BII)I", 0),
    M("skip",         "(J)J", 0),
    M("available",    "()I", 0),
    M("close",        "()V", 0),
    M("mark",         "(I)V", 0),
    M("reset",        "()V", 0),
    M("markSupported","()Z", 0)
};

static const nj_builtin_method outputstream_methods[] = {
    M("<init>", "()V", 0),
    M("write",  "(I)V", NJ_ACC_ABSTRACT),
    M("write",  "([B)V", 0),
    M("write",  "([BII)V", 0),
    M("flush",  "()V", 0),
    M("close",  "()V", 0)
};

static const nj_builtin_field bais_fields[] = {
    FLD("buf",   "[B", 0), FLD("pos",  "I", 0),
    FLD("mark",  "I", 0),  FLD("count","I", 0)
};
static const nj_builtin_method bais_methods[] = {
    M("<init>",         "([B)V", 0),
    M("<init>",         "([BII)V", 0),
    M("read",           "()I", 0),
    M("available",      "()I", 0),
    M("skip",           "(J)J", 0),
    M("mark",           "(I)V", 0),
    M("reset",          "()V", 0),
    M("markSupported",  "()Z", 0),
    M("close",          "()V", 0)
};

static const nj_builtin_field baos_fields[] = {
    FLD("buf", "[B", 0), FLD("count", "I", 0)
};
static const nj_builtin_method baos_methods[] = {
    M("<init>",       "()V", 0),
    M("<init>",       "(I)V", 0),
    M("write",        "(I)V", 0),
    M("write",        "([BII)V", 0),
    M("toByteArray",  "()[B", 0),
    M("size",         "()I", 0),
    M("reset",        "()V", 0),
    M("toString",     "()Ljava/lang/String;", 0),
    M("close",        "()V", 0)
};

static const nj_builtin_field dis_fields[] = {
    FLD("in", "Ljava/io/InputStream;", 0)
};
static const nj_builtin_method dis_methods[] = {
    M("<init>",             "(Ljava/io/InputStream;)V", 0),
    M("read",               "()I", 0),
    M("read",               "([BII)I", 0),
    M("readFully",          "([BII)V", 0),
    M("skipBytes",          "(I)I", 0),
    M("readBoolean",        "()Z", 0),
    M("readByte",           "()B", 0),
    M("readUnsignedByte",   "()I", 0),
    M("readShort",          "()S", 0),
    M("readUnsignedShort",  "()I", 0),
    M("readChar",           "()C", 0),
    M("readInt",            "()I", 0),
    M("readLong",           "()J", 0),
    M("readUTF",            "()Ljava/lang/String;", 0),
    M("available",          "()I", 0),
    M("close",              "()V", 0)
};

static const nj_builtin_field dos_fields[] = {
    FLD("out", "Ljava/io/OutputStream;", 0),
    FLD("written", "I", 0)
};
static const nj_builtin_method dos_methods[] = {
    M("<init>",          "(Ljava/io/OutputStream;)V", 0),
    M("write",           "(I)V", 0),
    M("write",           "([BII)V", 0),
    M("writeBoolean",    "(Z)V", 0),
    M("writeByte",       "(I)V", 0),
    M("writeShort",      "(I)V", 0),
    M("writeChar",       "(I)V", 0),
    M("writeInt",        "(I)V", 0),
    M("writeLong",       "(J)V", 0),
    M("writeUTF",        "(Ljava/lang/String;)V", 0),
    M("flush",           "()V", 0),
    M("close",           "()V", 0),
    M("size",            "()I", 0)
};

static const nj_builtin_method connector_methods[] = {
    M("open", "(Ljava/lang/String;)Ljavax/microedition/io/Connection;",
      NJ_ACC_STATIC),
    M("open", "(Ljava/lang/String;I)Ljavax/microedition/io/Connection;",
      NJ_ACC_STATIC),
    M("openInputStream", "(Ljava/lang/String;)Ljava/io/InputStream;",
      NJ_ACC_STATIC),
    M("openOutputStream", "(Ljava/lang/String;)Ljava/io/OutputStream;",
      NJ_ACC_STATIC),
    M("openDataInputStream", "(Ljava/lang/String;)Ljava/io/DataInputStream;",
      NJ_ACC_STATIC),
    M("openDataOutputStream", "(Ljava/lang/String;)Ljava/io/DataOutputStream;",
      NJ_ACC_STATIC),
    M("READ",  "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC),
    M("WRITE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC),
    M("READ_WRITE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL | NJ_ACC_PUBLIC)
};

static const nj_builtin_method connection_methods[] = {
    M("close", "()V", NJ_ACC_ABSTRACT)
};

/* Exception hierarchy: no members of their own. */
#define NJ_IO_EXC(name, super)                                                \
    { name, super, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },

const nj_builtin_class nj_lib_io_classes[] = {
    { "java/io/InputStream", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      NULL, 0u, inputstream_methods,
      (nj_u2)(sizeof(inputstream_methods) / sizeof(inputstream_methods[0])) },
    { "java/io/OutputStream", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      NULL, 0u, outputstream_methods,
      (nj_u2)(sizeof(outputstream_methods) / sizeof(outputstream_methods[0])) },
    { "java/io/ByteArrayInputStream", "java/io/InputStream", NJ_ACC_PUBLIC,
      bais_fields, (nj_u2)(sizeof(bais_fields) / sizeof(bais_fields[0])),
      bais_methods, (nj_u2)(sizeof(bais_methods) / sizeof(bais_methods[0])) },
    { "java/io/ByteArrayOutputStream", "java/io/OutputStream", NJ_ACC_PUBLIC,
      baos_fields, (nj_u2)(sizeof(baos_fields) / sizeof(baos_fields[0])),
      baos_methods, (nj_u2)(sizeof(baos_methods) / sizeof(baos_methods[0])) },
    { "java/io/DataInputStream", "java/io/InputStream", NJ_ACC_PUBLIC,
      dis_fields, (nj_u2)(sizeof(dis_fields) / sizeof(dis_fields[0])),
      dis_methods, (nj_u2)(sizeof(dis_methods) / sizeof(dis_methods[0])) },
    { "java/io/DataOutputStream", "java/io/OutputStream", NJ_ACC_PUBLIC,
      dos_fields, (nj_u2)(sizeof(dos_fields) / sizeof(dos_fields[0])),
      dos_methods, (nj_u2)(sizeof(dos_methods) / sizeof(dos_methods[0])) },
    NJ_IO_EXC("java/io/IOException", "java/lang/Exception")
    NJ_IO_EXC("java/io/EOFException", "java/io/IOException")
    NJ_IO_EXC("java/io/InterruptedIOException", "java/io/IOException")
    NJ_IO_EXC("java/io/UnsupportedEncodingException", "java/io/IOException")
    NJ_IO_EXC("javax/microedition/io/ConnectionNotFoundException",
              "java/io/IOException")
    { "javax/microedition/io/Connector", "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, connector_methods,
      (nj_u2)(sizeof(connector_methods) / sizeof(connector_methods[0])) },
    { "javax/microedition/io/Connection",
      NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, connection_methods,
      (nj_u2)(sizeof(connection_methods) / sizeof(connection_methods[0])) }
};
const int nj_lib_io_class_count =
    (int)(sizeof(nj_lib_io_classes) / sizeof(nj_lib_io_classes[0]));

const nj_native_entry nj_lib_io_table[] = {
    { "java/io/InputStream", "<init>",        "()V", is_close },
    { "java/io/InputStream", "read",          "()I", is_read_abstract },
    { "java/io/InputStream", "read",          "([B)I", is_read_bytes },
    { "java/io/InputStream", "read",          "([BII)I", is_read_bytes_range },
    { "java/io/InputStream", "skip",          "(J)J", is_skip },
    { "java/io/InputStream", "available",     "()I", is_available },
    { "java/io/InputStream", "close",         "()V", is_close },
    { "java/io/InputStream", "mark",          "(I)V", is_close },
    { "java/io/InputStream", "reset",         "()V", is_close },
    { "java/io/InputStream", "markSupported", "()Z", is_mark_supported },

    { "java/io/OutputStream", "<init>", "()V", os_flush },
    { "java/io/OutputStream", "write",  "(I)V", os_write_abstract },
    { "java/io/OutputStream", "write",  "([B)V", os_write_bytes },
    { "java/io/OutputStream", "write",  "([BII)V", os_write_bytes_range },
    { "java/io/OutputStream", "flush",  "()V", os_flush },
    { "java/io/OutputStream", "close",  "()V", os_close },

    { "java/io/ByteArrayInputStream", "<init>",        "([B)V", bais_init },
    { "java/io/ByteArrayInputStream", "<init>",        "([BII)V", bais_init_range },
    { "java/io/ByteArrayInputStream", "read",          "()I", bais_read },
    { "java/io/ByteArrayInputStream", "available",     "()I", bais_available },
    { "java/io/ByteArrayInputStream", "skip",          "(J)J", bais_skip },
    { "java/io/ByteArrayInputStream", "mark",          "(I)V", bais_mark },
    { "java/io/ByteArrayInputStream", "reset",         "()V", bais_reset },
    { "java/io/ByteArrayInputStream", "markSupported", "()Z", bais_mark_supported },
    { "java/io/ByteArrayInputStream", "close",         "()V", bais_close },

    { "java/io/ByteArrayOutputStream", "<init>",      "()V", baos_init },
    { "java/io/ByteArrayOutputStream", "<init>",      "(I)V", baos_init },
    { "java/io/ByteArrayOutputStream", "write",       "(I)V", baos_write },
    { "java/io/ByteArrayOutputStream", "write",       "([BII)V", baos_write_range },
    { "java/io/ByteArrayOutputStream", "toByteArray", "()[B", baos_toByteArray },
    { "java/io/ByteArrayOutputStream", "size",        "()I", baos_size },
    { "java/io/ByteArrayOutputStream", "reset",       "()V", baos_reset },
    { "java/io/ByteArrayOutputStream", "toString",    "()Ljava/lang/String;", baos_toString },
    { "java/io/ByteArrayOutputStream", "close",       "()V", baos_reset },

    { "java/io/DataInputStream", "<init>",           "(Ljava/io/InputStream;)V", baos_init },
    { "java/io/DataInputStream", "read",             "()I", dis_read },
    { "java/io/DataInputStream", "read",             "([BII)I", dis_read_range },
    { "java/io/DataInputStream", "readFully",        "([BII)V", dis_readFully },
    { "java/io/DataInputStream", "skipBytes",        "(I)I", is_available },
    { "java/io/DataInputStream", "readBoolean",      "()Z", dis_read_boolean },
    { "java/io/DataInputStream", "readByte",         "()B", dis_read },
    { "java/io/DataInputStream", "readUnsignedByte", "()I", dis_read },
    { "java/io/DataInputStream", "readShort",        "()S", dis_read_short },
    { "java/io/DataInputStream", "readUnsignedShort","()I", dis_read_unsigned_short },
    { "java/io/DataInputStream", "readChar",         "()C", dis_read_char },
    { "java/io/DataInputStream", "readInt",          "()I", dis_read_int },
    { "java/io/DataInputStream", "readLong",         "()J", dis_read_long },
    { "java/io/DataInputStream", "readUTF",          "()Ljava/lang/String;", dis_readUTF },
    { "java/io/DataInputStream", "available",        "()I", is_available },
    { "java/io/DataInputStream", "close",            "()V", is_close },

    { "java/io/DataOutputStream", "<init>",       "(Ljava/io/OutputStream;)V", baos_init },
    { "java/io/DataOutputStream", "write",        "(I)V", dos_write },
    { "java/io/DataOutputStream", "write",        "([BII)V", dos_write_range },
    { "java/io/DataOutputStream", "writeBoolean", "(Z)V", dos_write_boolean },
    { "java/io/DataOutputStream", "writeByte",    "(I)V", dos_write },
    { "java/io/DataOutputStream", "writeShort",   "(I)V", dos_write_short },
    { "java/io/DataOutputStream", "writeChar",    "(I)V", dos_write_char },
    { "java/io/DataOutputStream", "writeInt",     "(I)V", dos_write_int },
    { "java/io/DataOutputStream", "writeLong",    "(J)V", dos_write_long },
    { "java/io/DataOutputStream", "writeUTF",     "(Ljava/lang/String;)V", dos_writeUTF },
    { "java/io/DataOutputStream", "flush",        "()V", dos_flush },
    { "java/io/DataOutputStream", "close",        "()V", dos_flush },
    { "java/io/DataOutputStream", "size",         "()I", dos_size },

    { "javax/microedition/io/Connector", "open", "(Ljava/lang/String;)Ljavax/microedition/io/Connection;", connector_open },
    { "javax/microedition/io/Connector", "open", "(Ljava/lang/String;I)Ljavax/microedition/io/Connection;", connector_open_stream },
    { "javax/microedition/io/Connector", "openInputStream", "(Ljava/lang/String;)Ljava/io/InputStream;", connector_open },
    { "javax/microedition/io/Connector", "openOutputStream", "(Ljava/lang/String;)Ljava/io/OutputStream;", connector_open },
    { "javax/microedition/io/Connector", "openDataInputStream", "(Ljava/lang/String;)Ljava/io/DataInputStream;", connector_open },
    { "javax/microedition/io/Connector", "openDataOutputStream", "(Ljava/lang/String;)Ljava/io/DataOutputStream;", connector_open }
};
const int nj_lib_io_count =
    (int)(sizeof(nj_lib_io_table) / sizeof(nj_lib_io_table[0]));
