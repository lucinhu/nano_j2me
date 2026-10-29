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
#define NJ_C_READER   "java/io/Reader"
#define NJ_C_WRITER   "java/io/Writer"
#define NJ_C_ISR      "java/io/InputStreamReader"
#define NJ_C_OSW      "java/io/OutputStreamWriter"

/* Defined further down, next to the other stream helpers; the tables are
 * emitted before them.
 *
 * The isr_ and osw_ bodies are the Reader/Writer ones: the wrapped stream lives
 * in the base class's lock field, so the same body serves a Reader and an
 * InputStreamReader (see the note above the class rows), and rd_mark,
 * rd_mark_supported and rd_reset are shared the same way.  rd_ready is the
 * generic Reader default (false); InputStreamReader keeps isr_ready and its
 * real available() probe.  wr_write_str_range is Writer.write(String,int,int),
 * which OutputStreamWriter inherits. */
NJFN(isr_init); NJFN(isr_range); NJFN(isr_ready);
NJFN(isr_close);
NJFN(osw_init); NJFN(osw_range); NJFN(osw_flush);
NJFN(rd_read); NJFN(rd_init_lock); NJFN(rd_ready);
NJFN(rd_mark); NJFN(rd_mark_supported); NJFN(rd_reset); NJFN(wr_write_str_range);
NJFN(wr_close);
NJFN(ps_init); NJFN(ps_check_error); NJFN(ps_set_error);
NJFN(ps_write_bytes_range);
NJFN(ps_print_bool); NJFN(ps_print_long); NJFN(ps_print_float);
NJFN(ps_print_double); NJFN(ps_print_chars); NJFN(ps_print_obj);
NJFN(ps_println_char); NJFN(ps_println_long); NJFN(ps_println_float);
NJFN(ps_println_double); NJFN(ps_println_chars);

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
    if (off < 0 || len < 0 || (nj_u4)off > buf->length ||
        (nj_u4)len > buf->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (len == 0) {
        ret->i = 0;
        return NJ_OK;
    }
    /* CLDC InputStream.read(byte[],int,int) propagates a first-byte failure,
     * but treats an IOException after some bytes as end of input.  Other
     * Throwable types must still propagate, including RuntimeException. */
    for (i = 0; i < len; i++) {
        nj_s4 c = nj_call_int(vm, self, NJ_C_ISTREAM, "read", "()I", NULL);
        if (vm->exception) {
            nj_class *exc_cls;
            for (exc_cls = vm->exception->cls; i > 0 && exc_cls;
                 exc_cls = exc_cls->super) {
                if (strcmp(exc_cls->name, "java/io/IOException") == 0) {
                    nj_clear_exception(vm);
                    ret->i = i;
                    return NJ_OK;
                }
            }
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
    nj_value a3[4];
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

/* reset() on a stream that does not implement marking: the CLDC javadoc says
 * "throws IOException - if this stream has not been marked or if the mark has
 * been invalidated", and the reference implementation's InputStream answers
 * "mark/reset not supported".  A stream that does support marking overrides
 * this - ByteArrayInputStream does - so this is only reached when there really
 * is no mark to return to, and the failure must be visible rather than an
 * empty operation that silently leaves the stream where it is. */
NJFN(is_reset)
{
    return nj_throw(vm, "java/io/IOException", "mark/reset not supported");
}

/* How much of a skip is read per call.  Big enough that skipping a few
 * kilobytes is a couple of reads, small enough to stay off the heap budget of
 * a phone. */
#define NJ_SKIP_CHUNK 512

NJFN(is_skip)
{
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s8 done = 0;
    nj_u4 mark;
    nj_ref scratch;

    if (want <= 0) {
        ret->j = 0;
        return NJ_OK;
    }

    /* Skip through read(byte[], int, int), not read().  A stream is free to
     * implement only the range form - several titles do - and the single-byte
     * form would then land on the abstract read() and throw IOException
     * instead of skipping.  Going the other way is always safe: our
     * InputStream.read(byte[],int,int) chains down to read(). */
    mark = nj_root_mark(vm);
    scratch = nj_root(vm, nj_byte_array_new(vm, NJ_SKIP_CHUNK));
    if (!scratch) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }

    while (done < want) {
        nj_value a3[4];
        nj_s4 want_here = (nj_s4)((want - done) > NJ_SKIP_CHUNK
                                  ? NJ_SKIP_CHUNK : (want - done));
        nj_s4 n;

        a3[0].r = scratch;
        a3[1].i = 0;
        a3[2].i = want_here;
        n = nj_call_int(vm, NJ_THIS(args), NJ_C_ISTREAM, "read", "([BII)I", a3);
        if (vm->exception) {
            nj_root_release(vm, mark);
            return NJ_ERR;
        }
        if (n <= 0) {
            break;
        }
        done += n;
    }

    nj_root_release(vm, mark);
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
        nj_value a1[2];
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
    nj_value a3[4];
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
    /* ByteArrayInputStream(byte[] buf) - ONE argument slot.  The null check
     * used to be written as `!buf && NJ_ARG_INT(args, 1) != 0`, but there is
     * no slot 1 in this form: it read the operand-stack word above the frame,
     * which is whatever the calling expression left there. */
    nj_ref self = NJ_THIS(args);
    nj_ref buf = NJ_ARG_REF(args, 0);

    if (!buf) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    nj_field_write_ref(vm, self, nj_hot_field(vm, NJ_HF_BAIS_BUF, BAIS, "buf", "[B"), buf);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"), 0);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I"),
                       buf ? (nj_s4)buf->length : 0);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_MARK, BAIS, "mark", "I"), 0);
    return NJ_OK;
}

NJFN(bais_init_range)
{
    nj_status st = NJ_CALL(bais_init, vm, th, args, ret);
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);

    if (st != NJ_OK) {
        return st;
    }
    /* Preserve nano_j2me's existing bounded-window policy here.  This is
     * stricter than Java SE (which truncates an overlong window); the CLDC
     * 1.1 constructor/reset text also differs from SE.  This check is a local
     * safety policy, not evidence of JDK behaviour.  Resolving that separate
     * compatibility question must not obscure the read/skip contracts. */
    if (!buf || off < 0 || len < 0 || off > (nj_s4)buf->length - len) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"),
                       off);
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I"),
                       off + len);
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAIS_MARK, BAIS, "mark", "I"),
                       off);
    return NJ_OK;
}

NJFN(bais_read)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAIS_BUF, BAIS, "buf", "[B"));
    nj_s4 pos = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"));
    nj_s4 count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I"));

    if (!buf || pos >= count || pos >= (nj_s4)buf->length) {
        ret->i = -1;
        return NJ_OK;
    }
    ret->i = (nj_s4)buf->u.bytes[pos];
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"), pos + 1);
    return NJ_OK;
}

NJFN(bais_read_bytes_range)
{
    nj_ref self = NJ_THIS(args);
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_ref buf;
    nj_s4 pos, count;

    if (!dst) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (off < 0 || len < 0 || (nj_u4)off > dst->length ||
        (nj_u4)len > dst->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAIS_BUF, BAIS, "buf", "[B"));
    pos = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"));
    count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I"));
    if (pos < 0) return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    if (!buf || pos >= count || (nj_u4)pos >= buf->length) {
        /* BAIS checks EOF before the zero-length case (unlike InputStream). */
        ret->i = -1;
        return NJ_OK;
    }
    if (count > (nj_s4)buf->length) count = (nj_s4)buf->length;
    if (len > count - pos) len = count - pos;
    /* The destination may be the stream's own backing array. */
    if (len > 0) memmove(dst->u.bytes + off, buf->u.bytes + pos, (nj_size)len);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"), pos + len);
    ret->i = len;
    return NJ_OK;
}

NJFN(bais_available)
{
    nj_ref self = NJ_THIS(args);
    ret->i = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I")) -
             nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"));
    return NJ_OK;
}

NJFN(bais_skip)
{
    nj_ref self = NJ_THIS(args);
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s4 pos = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"));
    nj_s4 count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_COUNT, BAIS, "count", "I"));
    nj_s4 avail = count - pos;
    /* Reject a negative long before narrowing: e.g. -4294967295L would
     * otherwise become +1 on a 32-bit conversion. */
    nj_s4 step = (want <= 0 || avail <= 0) ? 0 :
                 (want > avail ? avail : (nj_s4)want);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"), pos + step);
    ret->j = step;
    return NJ_OK;
}

NJFN(bais_mark)
{
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAIS_MARK, BAIS, "mark", "I"),
                       nj_field_read_int(vm, NJ_THIS(args),
                                         nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I")));
    return NJ_OK;
}

NJFN(bais_reset)
{
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAIS_POS, BAIS, "pos", "I"),
                       nj_field_read_int(vm, NJ_THIS(args),
                                         nj_hot_field(vm, NJ_HF_BAIS_MARK, BAIS, "mark", "I")));
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
    nj_field *fb = nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B");
    nj_field *fc = nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I");
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

static nj_status baos_init_capacity(nj_vm *vm, nj_ref self, nj_u4 cap)
{
    nj_ref buf = nj_byte_array_new(vm, cap);

    if (!buf) {
        return nj_throw_oom(vm);
    }
    nj_field_write_ref(vm, self, nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B"), buf);
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"), 0);
    return NJ_OK;
}

NJFN(baos_init)
{
    /* ByteArrayOutputStream() - the default 32 bytes.  This method takes NO
     * arguments, so there is no argument window here: `args` points at the
     * operand-stack word above the frame and nothing may read args[0]. */
    return baos_init_capacity(vm, NJ_THIS(args), 32u);
}

NJFN(baos_init_cap)
{
    /* ByteArrayOutputStream(int size), bound separately on purpose.  While
     * the two constructors shared one entry, the no-argument form still read
     * args[0] - the operand-stack word above its own frame, an old reference
     * or loop counter.  Whenever that word happened to be large and positive
     * the runtime allocated it and raised OutOfMemoryError from inside a
     * <clinit>.  Which titles died, and whether they died at all, moved with
     * the heap layout, so it read like a memory shortage and was not one. */
    nj_s4 size = NJ_ARG_INT(args, 0);

    if (size < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", NULL);
    }
    return baos_init_capacity(vm, NJ_THIS(args), (nj_u4)size);
}

NJFN(baos_write)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 b = NJ_ARG_INT(args, 0);
    nj_ref buf;

    if (baos_ensure(vm, self, 1) != NJ_OK) {
        return NJ_ERR;
    }
    buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B"));
    {
        nj_s4 count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"));
        buf->u.bytes[count] = (nj_u1)b;
        nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"), count + 1);
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
    buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B"));
    count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"));
    if (len > 0) {
        memcpy(buf->u.bytes + count, src->u.bytes + off, (nj_size)len);
    }
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"), count + len);
    return NJ_OK;
}

NJFN(baos_toByteArray)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B"));
    nj_s4 count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"));
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
                               nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"));
    return NJ_OK;
}

NJFN(baos_reset)
{
    nj_field_write_int(vm, NJ_THIS(args), nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"), 0);
    return NJ_OK;
}

NJFN(baos_toString)
{
    nj_ref self = NJ_THIS(args);
    nj_ref buf = nj_field_read_ref(vm, self, nj_hot_field(vm, NJ_HF_BAOS_BUF, BAOS, "buf", "[B"));
    nj_s4 count = nj_field_read_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"));
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
    return nj_field_read_ref(vm, self,
                             nj_hot_field(vm, NJ_HF_DOS_OUT, DOS, "out",
                                          "Ljava/io/OutputStream;"));
}

/* Write raw bytes through the wrapped stream. */
static nj_status dos_raw(nj_vm *vm, nj_ref out, const nj_u1 *p, nj_s4 n)
{
    nj_value a3[4];
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

/*
 * writeFloat and writeDouble.
 *
 * CLDC 1.1's DataOutputStream has both, and they write the IEEE 754 bit
 * pattern big-endian, exactly as writeInt and writeLong write their integers.
 * They were missing here, which is not a theoretical gap: 3D跳台滑雪2011_4507
 * calls writeFloat from its save routine and died with
 *
 *     NoSuchMethodError at AlpineSki/h.h()V pc=61:
 *     java/io/DataOutputStream.writeFloat(F)V
 *
 * on the first frame it tried to save, so the title never presented anything.
 * The read side (dis_read_float, dis_read_double) has existed all along; only
 * the writer was absent.
 */
NJFN(dos_write_float)
{
    nj_u1 b[4];
    float f = NJ_ARG_FLOAT(args, 0);
    nj_u4 v;
    memcpy(&v, &f, sizeof(v));
    b[0] = (nj_u1)(v >> 24); b[1] = (nj_u1)(v >> 16);
    b[2] = (nj_u1)(v >> 8);  b[3] = (nj_u1)v;
    return dos_raw(vm, dos_out(vm, NJ_THIS(args)), b, 4);
}

NJFN(dos_write_double)
{
    nj_u1 b[8];
    double d = NJ_ARG_DOUBLE(args, 0);
    nj_u8 v;
    int i;
    memcpy(&v, &d, sizeof(v));
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
 * writeChars(String) writes each UTF-16 unit as two bytes, high byte first:
 * no modified UTF-8 and no length prefix, which is what CLDC 1.1 specifies and
 * what DataInputStream.readChar reads back.  灵异村庄_11169 builds its save
 * record with it - the member was missing, the call resolved up to the
 * DataOutput interface and threw NoSuchMethodError inside the title's own
 * helper, which its catch turned into a failed destroyApp.  One stream write
 * for the whole string, because a save record is written once per high score
 * and a per-character write would be one call into the OutputStream each.
 */
NJFN(dos_writeChars)
{
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_ref out;
    nj_u1 *tmp;
    nj_u4 i;
    nj_status st;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    out = dos_out(vm, NJ_THIS(args));
    if (s->length == 0u) {
        return NJ_OK;
    }
    tmp = (nj_u1 *)nj_host_alloc(vm, (nj_size)s->length * 2u);
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0u; i < s->length; i++) {
        nj_u4 c = nj_string_chars(s)[i];
        tmp[i * 2u] = (nj_u1)((c >> 8) & 0xFFu);
        tmp[i * 2u + 1u] = (nj_u1)(c & 0xFFu);
    }
    st = dos_raw(vm, out, tmp, (nj_s4)(s->length * 2u));
    nj_host_free(vm, tmp);
    return st;
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
    if (n > 0xFFFFu) {
        nj_host_free(vm, tmp);
        return nj_throw(vm, "java/io/UTFDataFormatException",
                        "encoded string too long");
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
    return nj_field_read_ref(vm, self,
                             nj_hot_field(vm, NJ_HF_DIS_IN, DIS, "in",
                                          "Ljava/io/InputStream;"));
}

/* DataInputStream(InputStream) and DataOutputStream(OutputStream).
 *
 * Both constructors used to be bound to baos_init, which reads its argument
 * slot as a buffer capacity.  For a stream constructor that slot holds a
 * REFERENCE, so the capacity became the low half of a heap pointer: the
 * constructor asked the heap for a buffer tens of megabytes wide and died
 * with OutOfMemoryError, or - when the pointer happened to land low - stored
 * a ByteArrayOutputStream's fields into a DataInputStream object and left
 * `in` uninitialised.  A corpus title showed exactly that pair of symptoms,
 * alternating from run to run, which is what gave the game away. */
NJFN(dis_init)
{
    nj_field_write_ref(vm, NJ_THIS(args),
                       nj_hot_field(vm, NJ_HF_DIS_IN, DIS, "in",
                                    "Ljava/io/InputStream;"),
                       NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(dos_init)
{
    nj_ref self = NJ_THIS(args);

    nj_field_write_ref(vm, self,
                       nj_hot_field(vm, NJ_HF_DOS_OUT, DOS, "out",
                                    "Ljava/io/OutputStream;"),
                       NJ_ARG_REF(args, 0));
    nj_field_write_int(vm, self,
                       nj_hot_field(vm, NJ_HF_DOS_WRITTEN, DOS, "written", "I"),
                       0);
    return NJ_OK;
}

static nj_s4 dis_one(nj_vm *vm, nj_ref in)
{
    nj_s4 c;
    /* nj_call treats a NULL receiver as a static call.  A missing resource
     * wrapped in DataInputStream must fail as a null dereference instead. */
    if (!in) {
        nj_throw(vm, "java/lang/NullPointerException", "null input stream");
        return -2;
    }
    c = nj_call_int(vm, in, NJ_C_ISTREAM, "read", "()I", NULL);
    if (vm->exception) {
        return -2;
    }
    if (c < 0) {
        nj_throw(vm, "java/io/EOFException", NULL);
        return -2;
    }
    return c;
}

NJFN(dis_read_byte)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    int c = dis_one(vm, in);
    if (c < 0) return NJ_ERR;
    ret->i = c >= 128 ? c - 256 : c;
    return NJ_OK;
}

NJFN(dis_read_unsigned_byte)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    int c = dis_one(vm, in);
    if (c < 0) return NJ_ERR;
    ret->i = c;
    return NJ_OK;
}

NJFN(dis_read)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    /* `in`, not NJ_THIS(args): reading from the wrapper dispatches straight
     * back into this function. */
    return nj_call(vm, in, NJ_C_ISTREAM, "read", "()I", NULL, ret);
}

/*
 * DataInputStream.available() is the underlying stream's, not InputStream's
 * default of zero.  A MIDlet that sizes a buffer from available() - and the
 * corpus is full of them - otherwise reads a zero-length image:
 * 寻秦皇陵的诅咒_12450 called Image.createImage(data, 0, 0) and was told
 * "no image data" once per frame.
 */
NJFN(dis_available)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    return nj_call(vm, in, NJ_C_ISTREAM, "available", "()I", NULL, ret);
}

NJFN(dis_read_range)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_value a3[4];
    a3[0] = args[0];
    a3[1] = args[1];
    a3[2] = args[2];
    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    return nj_call(vm, in, NJ_C_ISTREAM, "read", "([BII)I", a3, ret);
}

/* The body of both readFully overloads.  Takes the receiver explicitly rather
 * than going through an NJFN, whose arguments are addressed as args[-1] for
 * `this` - a synthetic argument block would make that read a garbage slot. */
static nj_status dis_readFully_run(nj_vm *vm, nj_ref self, nj_ref buf,
                                   nj_s4 off, nj_s4 len)
{
    nj_ref in = dis_in(vm, self);
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

NJFN(dis_readFully)
{
    return dis_readFully_run(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                             NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
}

/* readFully(byte[]) - the other overload CLDC declares on DataInput.  Reading
 * into the whole array is the same work as the range form at off = 0, and a
 * title compiled against the real CLDC calls exactly this one. */
NJFN(dis_readFully_all)
{
    nj_ref buf = NJ_ARG_REF(args, 0);

    return dis_readFully_run(vm, NJ_THIS(args), buf, 0,
                             buf ? (nj_s4)buf->length : 0);
}

/*
 * mark()/reset()/markSupported() on a DataInputStream: FilterInputStream
 * forwards all three to the stream it wraps, and a title that rewinds the
 * stream depends on that.  实况足球经理2009_10609's resource unpacker (d.c)
 * reads a packed container, fills its offset table, reset()s back to the
 * start and skipBytes()s to the requested entry; with reset() falling through
 * to InputStream's no-op (close()) the entry began at the wrong offset and
 * d.E parsed sizes that did not fit its buffer - the loader aborted, its
 * string table stayed null and the title stayed on a flat frame.
 */
NJFN(dis_mark)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_value a1[3];

    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    memset(a1, 0, sizeof(a1));
    a1[0].i = NJ_ARG_INT(args, 0);
    return nj_call(vm, in, NJ_C_ISTREAM, "mark", "(I)V", a1, NULL);
}

NJFN(dis_reset)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));

    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    return nj_call(vm, in, NJ_C_ISTREAM, "reset", "()V", NULL, NULL);
}

NJFN(dis_mark_supported)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_value r;

    if (!in) {
        ret->i = 0;
        return NJ_OK;
    }
    memset(&r, 0, sizeof(r));
    if (nj_call(vm, in, NJ_C_ISTREAM, "markSupported", "()Z", NULL, &r) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = r.i ? 1 : 0;
    return NJ_OK;
}

/*
 * skipBytes: an ATTEMPT, and a short count is the documented answer.  The
 * CLDC 1.1 DataInput javadoc is explicit - "Makes an attempt to skip over n
 * bytes ... However, it may skip over some smaller number of bytes, possibly
 * zero.  This method never throws an EOFException.  The actual number of
 * bytes skipped is returned." - so this must not turn an end of file into an
 * EOFException.
 *
 * It used to, and 实况足球经理2009_10609 is the title that paid for it: its
 * loader (d.E) reads a length, skipBytes()s past it, reads the next length
 * and builds its string table from what follows.  The first skip ran past
 * the end of a short resource, the EOFException aborted the loader before the
 * table was built, and every later lookup - the game's own string cache -
 * dereferenced a null array and drew nothing.  The title never reached its
 * first frame because of one short skip.
 *
 * The skip goes through the underlying stream's skip(long), which reads
 * through read(byte[],int,int) and answers the number of bytes it really
 * moved.
 */
NJFN(dis_skip_bytes)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_s4 want = NJ_ARG_INT(args, 0);
    nj_value sret;

    if (!in) {
        return nj_throw(vm, "java/io/IOException", "no underlying stream");
    }
    if (want <= 0) {
        ret->i = 0;
        return NJ_OK;
    }
    memset(&sret, 0, sizeof(sret));
    sret.j = 0;
    {
        nj_value a1[3];

        a1[0].j = (nj_s8)want;
        if (nj_call(vm, in, NJ_C_ISTREAM, "skip", "(J)J", a1, &sret) != NJ_OK) {
            return NJ_ERR;
        }
    }
    ret->i = (nj_s4)sret.j;
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

/*
 * readFloat()/readDouble() were missing from the class table entirely, so any
 * MIDlet that called them died with NoSuchMethodError before it drew anything
 * (愤怒的小鸟 did).  CLDC's DataInputStream reads them big-endian, as IEEE 754
 * bits - the same byte order readInt()/readLong() already use, so the value is
 * assembled as an integer and reinterpreted.
 */
NJFN(dis_read_float)
{
    nj_u4 v = 0u;
    float f;
    int i;
    for (i = 0; i < 4; i++) {
        nj_s4 c = dis_one(vm, dis_in(vm, NJ_THIS(args)));
        if (c < 0) {
            return NJ_ERR;
        }
        v = (v << 8) | (nj_u4)c;
    }
    memcpy(&f, &v, sizeof(f));
    ret->f = f;
    return NJ_OK;
}

NJFN(dis_read_double)
{
    nj_u8 v = 0u;
    double d;
    int i;
    for (i = 0; i < 8; i++) {
        nj_s4 c = dis_one(vm, dis_in(vm, NJ_THIS(args)));
        if (c < 0) {
            return NJ_ERR;
        }
        v = (v << 8) | (nj_u8)c;
    }
    memcpy(&d, &v, sizeof(d));
    ret->d = d;
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

/*
 * One byte of a modified-UTF-8 string, from either kind of source.
 *
 * The instance form reads through dis_one(); the static form is handed any
 * DataInput at all and has to go through that object's own readByte().  That is
 * the ONLY difference between the two, so it lives here and the length
 * handling, the continuation counting and the four allocation-failure paths
 * are shared below instead of copied.  A negative return means the byte could
 * not be read and an exception is pending: readByte() throws EOFException at
 * the end, dis_one() returns -1.
 */
static nj_s4 utf_next(nj_vm *vm, nj_ref src, int via_interface)
{
    nj_value r;

    if (!via_interface) {
        return dis_one(vm, src);
    }
    if (nj_call(vm, src, "java/io/DataInput", "readByte", "()B", NULL, &r) != NJ_OK) {
        return -1;
    }
    return r.i & 0xff;
}

/* Decode `len` modified-UTF-8 bytes from `src` into a new String. */
static nj_status read_utf_body(nj_vm *vm, nj_ref src, int via_interface,
                               nj_s4 len, nj_value *ret)
{
    nj_u2 *tmp;
    nj_s4 i, n = 0;
    nj_ref out;

    tmp = (nj_u2 *)nj_host_alloc(vm, ((nj_size)len + 1u) * sizeof(nj_u2));
    if (!tmp) {
        return nj_throw_oom(vm);
    }
    for (i = 0; i < len; i++) {
        nj_s4 c = utf_next(vm, src, via_interface);
        if (c < 0) {
            nj_host_free(vm, tmp);
            return NJ_ERR;
        }
        if (c < 0x80) {
            tmp[n++] = (nj_u2)c;
        } else if ((c & 0xE0) == 0xC0) {
            nj_s4 c2 = utf_next(vm, src, via_interface);
            if (c2 < 0) {
                nj_host_free(vm, tmp);
                return NJ_ERR;
            }
            tmp[n++] = (nj_u2)(((c & 0x1F) << 6) | (c2 & 0x3F));
            i++;
        } else {
            nj_s4 c2 = utf_next(vm, src, via_interface);
            nj_s4 c3;
            if (c2 < 0) {
                nj_host_free(vm, tmp);
                return NJ_ERR;
            }
            c3 = utf_next(vm, src, via_interface);
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

NJFN(dis_readUTF)
{
    nj_ref in = dis_in(vm, NJ_THIS(args));
    nj_s4 len;

    /* The length lives in `ret`, not in this call's return value: NJ_CALL
     * returns a status, so assigning it to `len` made every readUTF() return
     * the empty string and leave the string's own bytes in the stream for the
     * next read to walk into. */
    if (NJ_CALL(dis_read_unsigned_short, vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    len = ret->i;
    if (len < 0) {
        return NJ_ERR;
    }
    return read_utf_body(vm, in, 0, len, ret);
}

/*
 * public static String DataInputStream.readUTF(DataInput in)
 *
 * Reads the length from `in` itself and then the same body.  It cannot call the
 * instance form above: that one was called on a DataInputStream and reads
 * through it, while this one is handed any DataInput - which is the whole
 * reason the static overload exists.  世界冠军拉力赛3D_8360 uses it.
 */
NJFN(dis_readUTF_static)
{
    nj_ref in = NJ_ARG_REF(args, 0);
    nj_value r;
    nj_s4 len;

    if (nj_call(vm, in, "java/io/DataInput", "readUnsignedShort", "()I",
                NULL, &r) != NJ_OK) {
        return NJ_ERR;
    }
    len = r.i;
    if (len < 0) {
        return NJ_ERR;
    }
    return read_utf_body(vm, in, 1, len, ret);
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

    /*
     * file:// is the one scheme this runtime can honour on its own, and it is
     * a real one: JSR 75 is how a MIDlet reads a ROM, a level pack or its own
     * save data.  Everything else is refused BY NAME - a game handed a
     * silently empty stream from an http:// URL is far harder to diagnose
     * than one that throws where it asked.
     */
    if (strncmp(u, "file://", 7u) == 0) {
        ret->r = nj_fc_new(vm, u);
        return ret->r ? NJ_OK : NJ_ERR;
    }
    /*
     * http:// and https:// are real when the port has an HTTP stack.  The
     * request itself goes out on the first call that needs a response (see
     * nj_lib_http.c), so a MIDlet that only builds a URL and never reads it
     * never touches the network.  A port without the hook refuses the URL
     * here, which is what the specification asks for an unsupported scheme.
     */
    if (strncmp(u, "http://", 7u) == 0 || strncmp(u, "https://", 8u) == 0) {
        ret->r = nj_http_new(vm, u);
        return ret->r ? NJ_OK : NJ_ERR;
    }
    /*
     * socket:// is a TCP client connection, and it is real only when the port
     * has a socket stack: without the hooks the connection is refused here,
     * with the URL in the message.  No server and no datagram scheme - 0 of
     * the 436 titles in the corpus use either.
     */
    if (strncmp(u, "socket://", 9u) == 0) {
        ret->r = nj_socket_new(vm, u);
        return ret->r ? NJ_OK : NJ_ERR;
    }
    /*
     * datagram:// is a UDP socket: "datagram://:19132" binds a wildcard port
     * for a server, "datagram://host:port" also names the default peer.  It is
     * how Minecraft PE's LAN mode talks RakNet.
     */
    if (strncmp(u, "datagram://", 11u) == 0) {
        ret->r = nj_datagram_new(vm, u);
        return ret->r ? NJ_OK : NJ_ERR;
    }
    /*
     * Bluetooth.  A SERVER URL names localhost, and that is the notifier the
     * javax.bluetooth module hands back: acceptAndOpen() waits for a peer and
     * returns the connection.  A CLIENT URL ("btspp://001122334455:3") is a
     * byte stream, but not a TCP one: it goes to the Bluetooth module, which
     * asks the port's bt_connect() hook.  Handing it to socket_connect() would
     * have made the port guess which of its two stacks the URL meant.
     */
    if (strncmp(u, "btspp://", 8u) == 0 || strncmp(u, "btl2cap://", 10u) == 0) {
        /* "btl2cap://" is two characters longer than "btspp://". */
        const char *host = (u[2] == 'l') ? u + 10 : u + 8;
        if (strncmp(host, "localhost", 9u) == 0) {
            ret->r = nj_bt_listen(vm, u);
            if (!ret->r)
                return nj_throw(vm,
                    "javax/microedition/io/ConnectionNotFoundException",
                    nj_bt_listen_error(vm, u));
            return NJ_OK;
        }
        ret->r = nj_bt_connect(vm, u);
        if (!ret->r)
            return nj_throw(vm,
                "javax/microedition/io/ConnectionNotFoundException",
                nj_bt_connect_error(vm, u));
        return NJ_OK;
    }
    return nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException", u);
}

NJFN(connector_open_stream)
{
    nj_s4 mode = NJ_ARG_INT(args, 1);
    nj_status status = NJ_CALL(connector_open, vm, th, args, ret);
    nj_field *field;

    if (status != NJ_OK || !ret->r) {
        return status;
    }
    /* FileConnection needs the requested mode for write operations such as
     * truncate(). Other connection types keep their existing behaviour. */
    if (strcmp(nj_ref_class_name(ret->r),
               "javax/microedition/io/file/FileConnection") == 0) {
        if (mode < 1 || mode > 3) {
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "invalid file connection mode");
        }
        field = nj_field_lookup(vm, "javax/microedition/io/file/FileConnection",
                                "fcMode", "I", 0);
        if (field) {
            nj_field_write_int(vm, ret->r, field, mode);
        }
    }
    return NJ_OK;
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
    M("read",           "([BII)I", 0),
    M("available",      "()I", 0),
    M("skip",           "(J)J", 0),
    M("mark",           "(I)V", 0),
    M("reset",          "()V", 0),
    M("markSupported",  "()Z", 0),
    M("close",          "()V", 0)
};

/* ------------------------------------------------------------------ */
/* java.io.FileInputStream                                             */
/* ------------------------------------------------------------------ */
/* Internal FileConnection stream. Only the sandbox-resolved path and byte
 * position survive a call, so large files cost no Java buffer and snapshots
 * never retain a host handle. read(byte[]) inherits InputStream's virtual
 * dispatch to our range method. */
#define FIS "java/io/FileInputStream"
#define FIS_PATHMAX 1024

static const nj_builtin_field fis_fields[] = {
    FLD("path", "Ljava/lang/String;", 0), FLD("position", "J", 0)
};
static const nj_builtin_method fis_methods[] = {
    M("<init>", "(Ljava/lang/String;)V", NJ_ACC_PUBLIC),
    M("read", "()I", NJ_ACC_PUBLIC),
    M("read", "([BII)I", NJ_ACC_PUBLIC),
    M("skip", "(J)J", NJ_ACC_PUBLIC),
    M("available", "()I", NJ_ACC_PUBLIC),
    M("close", "()V", NJ_ACC_PUBLIC)
};

/* A zero-length request probes readability/size without allocating or reading
 * the file. Return a byte count (zero at EOF), or -1 on a host error. Ports
 * own their paths: failure through installed hooks must not retry via libc. */
static nj_s4 fis_read_host(nj_vm *vm, const char *path, nj_s8 pos,
                           void *data, nj_u4 n, nj_s8 *size)
{
    if (vm->port && vm->port->file_open) {
        nj_file f;
        nj_s4 got = 0;
        if (!vm->port->file_read || !vm->port->file_close ||
            vm->port->file_open(vm->port->ud, path, "rb", &f) != NJ_OK) {
            return -1;
        }
        if (size) *size = (nj_s8)f.size;
        if (n && pos < (nj_s8)f.size) {
            if (pos < 0 || (pos != 0 && (!vm->port->file_seek ||
                vm->port->file_seek(vm->port->ud, &f, (nj_u4)pos) != NJ_OK))) {
                got = -1;
            } else {
                if ((nj_s8)n > (nj_s8)f.size - pos) n = (nj_u4)((nj_s8)f.size - pos);
                got = vm->port->file_read(vm->port->ud, &f, data, n);
                if (got < 0 || (nj_u4)got > n) got = -1;
            }
        }
        vm->port->file_close(vm->port->ud, &f);
        return got;
    }
#if NJ_USE_LIBC
    {
        FILE *fp = fopen(path, "rb");
        long end;
        nj_s4 got = 0;
        if (!fp) return -1;
        if (fseek(fp, 0L, SEEK_END) != 0 || (end = ftell(fp)) < 0L) {
            fclose(fp);
            return -1;
        }
        if (size) *size = (nj_s8)end;
        if (n && pos < (nj_s8)end) {
            if (pos < 0 || (nj_s8)(long)pos != pos ||
                fseek(fp, (long)pos, SEEK_SET) != 0) {
                got = -1;
            } else {
                if ((nj_s8)n > (nj_s8)end - pos) n = (nj_u4)((nj_s8)end - pos);
                got = (nj_s4)fread(data, 1u, (size_t)n, fp);
                if (ferror(fp)) got = -1;
            }
        }
        if (fclose(fp) != 0) got = -1;
        return got;
    }
#else
    NJ_UNUSED(path); NJ_UNUSED(pos); NJ_UNUSED(data); NJ_UNUSED(n); NJ_UNUSED(size);
    return -1;
#endif
}

static nj_status fis_path(nj_vm *vm, nj_ref self, char *host)
{
    nj_ref path = nj_field_read_ref(vm, self,
                                   fld(vm, FIS, "path", "Ljava/lang/String;"));
    if (!path) return nj_throw(vm, "java/io/IOException", "stream is closed");
    if (nj_string_to_utf8(path, host, FIS_PATHMAX) <= 0) {
        return nj_throw(vm, "java/io/IOException", "invalid file path");
    }
    return NJ_OK;
}

NJFN(fis_init)
{
    nj_ref path = NJ_ARG_REF(args, 0);
    char host[FIS_PATHMAX];
    if (!path) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (nj_string_to_utf8(path, host, (int)sizeof(host)) <= 0 ||
        fis_read_host(vm, host, 0, NULL, 0u, NULL) < 0) {
        return nj_throw(vm, "java/io/IOException", "cannot open for reading");
    }
    nj_field_write_ref(vm, NJ_THIS(args), fld(vm, FIS, "path", "Ljava/lang/String;"), path);
    nj_field_write_long(vm, NJ_THIS(args), fld(vm, FIS, "position", "J"), 0);
    return NJ_OK;
}

static nj_status fis_read_data(nj_vm *vm, nj_ref self, void *data, nj_u4 n,
                               nj_value *ret)
{
    char host[FIS_PATHMAX];
    nj_field *position = fld(vm, FIS, "position", "J");
    nj_s8 pos = nj_field_read_long(vm, self, position);
    nj_s4 got;
    if (fis_path(vm, self, host) != NJ_OK) return NJ_ERR;
    if (!n) { ret->i = 0; return NJ_OK; }
    got = fis_read_host(vm, host, pos, data, n, NULL);
    if (got < 0) return nj_throw(vm, "java/io/IOException", "cannot read the file");
    nj_field_write_long(vm, self, position, pos + got);
    ret->i = got ? got : -1;
    return NJ_OK;
}

NJFN(fis_read)
{
    nj_u1 byte;
    nj_status status = fis_read_data(vm, NJ_THIS(args), &byte, 1u, ret);
    if (status == NJ_OK && ret->i > 0) ret->i = (nj_s4)byte;
    return status;
}

NJFN(fis_read_range)
{
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1), len = NJ_ARG_INT(args, 2);
    if (!buf) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (off < 0 || len < 0 || (nj_u4)off > buf->length ||
        (nj_u4)len > buf->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    return fis_read_data(vm, NJ_THIS(args), buf->u.bytes + off, (nj_u4)len, ret);
}

NJFN(fis_skip)
{
    char host[FIS_PATHMAX];
    nj_ref self = NJ_THIS(args);
    nj_field *position = fld(vm, FIS, "position", "J");
    nj_s8 pos = nj_field_read_long(vm, self, position);
    nj_s8 want = NJ_ARG_LONG(args, 0), size, left;
    if (fis_path(vm, self, host) != NJ_OK) return NJ_ERR;
    ret->j = 0;
    if (want <= 0) return NJ_OK;
    if (fis_read_host(vm, host, 0, NULL, 0u, &size) < 0) {
        return nj_throw(vm, "java/io/IOException", "cannot size the file");
    }
    left = size > pos ? size - pos : 0;
    ret->j = want < left ? want : left;
    nj_field_write_long(vm, self, position, pos + ret->j);
    return NJ_OK;
}

NJFN(fis_available)
{
    char host[FIS_PATHMAX];
    nj_ref self = NJ_THIS(args);
    nj_s8 pos = nj_field_read_long(vm, self, fld(vm, FIS, "position", "J"));
    nj_s8 size, left;
    if (fis_path(vm, self, host) != NJ_OK) return NJ_ERR;
    if (fis_read_host(vm, host, 0, NULL, 0u, &size) < 0) {
        return nj_throw(vm, "java/io/IOException", "cannot size the file");
    }
    left = size > pos ? size - pos : 0;
    ret->i = left > 0x7fffffff ? 0x7fffffff : (nj_s4)left;
    return NJ_OK;
}

NJFN(fis_close)
{
    nj_field_write_ref(vm, NJ_THIS(args), fld(vm, FIS, "path", "Ljava/lang/String;"), NULL);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* java.io.FileOutputStream                                            */
/* ------------------------------------------------------------------ */
/*
 * Not a CLDC class, and not pretending to be one: FileConnection
 * .openOutputStream() has to hand back an OutputStream that is backed by a
 * file. Keep the path and current position in Java fields, reopening the host
 * file for each write so snapshots never contain a FILE * or a port handle.
 * Buffering an entire file in a ByteArrayOutputStream made Minecraft's 21 MB
 * chunks.dat exhaust even a 64 MB heap while growing its backing array.
 *
 * The existing ByteArrayOutputStream layout is retained; count records the
 * file position and buf stays empty. The path arrives already resolved against
 * the sandbox root, because FileConnection owns that decision.
 */
#define FOS "java/io/FileOutputStream"
#define FOS_PATHMAX 1024

static const nj_builtin_field fos_fields[] = {
    FLD("path", "Ljava/lang/String;", 0)
};
static const nj_builtin_method fos_methods[] = {
    M("<init>", "(Ljava/lang/String;)V", NJ_ACC_PUBLIC),
    M("flush",  "()V", NJ_ACC_PUBLIC),
    M("close",  "()V", NJ_ACC_PUBLIC),
    M("write", "(I)V", NJ_ACC_PUBLIC),
    M("write", "([BII)V", NJ_ACC_PUBLIC)
};

/*
 * Write at the stream's current position without retaining the file. A port
 * that can open files is asked first,
 * because that is the only route that exists when the runtime is built without
 * a C library; libc is the fallback, not the other way round.
 */
static int fos_write_host(nj_vm *vm, const char *path, const void *data,
                          nj_u4 n, nj_u4 pos)
{
    if (vm->port && vm->port->file_open) {
        nj_file f;
        int ok;
        /* FileConnection.create() owns creation. Falling back to "wb" after
         * a read-permission failure would destroy an existing file. */
        if (vm->port->file_open(vm->port->ud, path, "r+b", &f) != NJ_OK) {
            return 0;
        }
        ok = pos == 0u || (vm->port->file_seek &&
             vm->port->file_seek(vm->port->ud, &f, pos) == NJ_OK);
        if (ok && n != 0u) {
            ok = vm->port->file_write &&
                 vm->port->file_write(vm->port->ud, &f, data, n) == (nj_s4)n;
        }
        vm->port->file_close(vm->port->ud, &f);
        return ok;
    }
#if NJ_USE_LIBC
    {
        FILE *fp = fopen(path, "r+b");
        int ok;
        if (!fp) return 0;
        ok = pos == 0u || fseek(fp, (long)pos, SEEK_SET) == 0;
        if (ok && n != 0u) ok = fwrite(data, 1u, (size_t)n, fp) == (size_t)n;
        if (fclose(fp) != 0) ok = 0;
        return ok;
    }
#else
    (void)path;
    (void)data;
    (void)n;
    (void)pos;
    return 0;
#endif
}

NJFN(fos_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref path = NJ_ARG_REF(args, 0);
    char host[FOS_PATHMAX];

    if (!path) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (nj_string_to_utf8(path, host, (int)sizeof(host)) <= 0 ||
        !fos_write_host(vm, host, NULL, 0u, 0u)) {
        return nj_throw(vm, "java/io/IOException", "cannot open the file");
    }
    nj_field_write_int(vm, self, nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I"), 0);
    nj_field_write_ref(vm, self, fld(vm, FOS, "path", "Ljava/lang/String;"), path);
    return NJ_OK;
}

static nj_status fos_write_data(nj_vm *vm, nj_ref self, const void *data, nj_u4 n)
{
    nj_ref path = nj_field_read_ref(vm, self,
                                   fld(vm, FOS, "path", "Ljava/lang/String;"));
    nj_field *fc = nj_hot_field(vm, NJ_HF_BAOS_COUNT, BAOS, "count", "I");
    nj_s4 pos = nj_field_read_int(vm, self, fc);
    char host[FOS_PATHMAX];
    if (!path) return nj_throw(vm, "java/io/IOException", "stream is closed");
    if (pos < 0 || n > 0x7fffffffu - (nj_u4)pos ||
        nj_string_to_utf8(path, host, (int)sizeof(host)) <= 0 ||
        !fos_write_host(vm, host, data, n, (nj_u4)pos)) {
        return nj_throw(vm, "java/io/IOException", "cannot write the file");
    }
    nj_field_write_int(vm, self, fc, pos + (nj_s4)n);
    return NJ_OK;
}

NJFN(fos_write)
{
    nj_u1 b = (nj_u1)NJ_ARG_INT(args, 0);
    return fos_write_data(vm, NJ_THIS(args), &b, 1u);
}

NJFN(fos_write_range)
{
    nj_ref buf = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1), len = NJ_ARG_INT(args, 2);
    if (!buf) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (off < 0 || len < 0 || (nj_u4)off > buf->length ||
        (nj_u4)len > buf->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    return fos_write_data(vm, NJ_THIS(args), buf->u.bytes + off, (nj_u4)len);
}

NJFN(fos_flush)
{
    /* Each write closes its host handle and flushes the host buffer. */
    return NJ_OK;
}

NJFN(fos_close)
{
    nj_field_write_ref(vm, NJ_THIS(args), fld(vm, FOS, "path", "Ljava/lang/String;"), NULL);
    return NJ_OK;
}

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
    M("readFully",          "([B)V", 0),
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
    M("readFloat",          "()F", 0),
    M("readDouble",         "()D", 0),
    M("readUTF",            "()Ljava/lang/String;", 0),
    M("readUTF",            "(Ljava/io/DataInput;)Ljava/lang/String;",
                            NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("available",          "()I", 0),
    M("mark",               "(I)V", 0),
    M("reset",              "()V", 0),
    M("markSupported",      "()Z", 0),
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
    M("writeChars",      "(Ljava/lang/String;)V", 0),
    M("writeInt",        "(I)V", 0),
    M("writeLong",       "(J)V", 0),
    M("writeFloat",      "(F)V", 0),
    M("writeDouble",     "(D)V", 0),
    M("writeUTF",        "(Ljava/lang/String;)V", 0),
    M("flush",           "()V", 0),
    M("close",           "()V", 0),
    M("size",            "()I", 0)
};

static const nj_builtin_method connector_methods[] = {
    M("open", "(Ljava/lang/String;)Ljavax/microedition/io/Connection;",
      NJ_ACC_STATIC),
    M("open", "(Ljava/lang/String;IZ)Ljavax/microedition/io/Connection;",
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

/* ------------------------------------------------------------------ */
/* Reader / Writer                                                     */
/* ------------------------------------------------------------------ */
/*
 * The character streams CLDC 1.1 has and this runtime did not: a real MIDlet
 * (vNes, loading a palette file) died with NoClassDefFoundError on
 * java/io/InputStreamReader.  They cannot be stubbed - javac refuses to compile
 * into the java.io package, which belongs to the java.base module - so they
 * have to exist HERE, and tools/check_cldc_api.py cannot see the gap either,
 * because the JDK supplies the class happily at compile time.
 *
 * Default constructors use the advertised microedition.encoding (ISO-8859-1).
 * Explicit encoding constructors and streaming UTF-8/ASCII live in
 * nj_io_charset.inc; an unknown name must fail instead of falling back.
 */
static nj_ref strm_field(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                         const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

/*
 * Reader(Object lock): CLDC's protected constructor that stores the caller's
 * lock object in the field.  For a Reader built over a stream this runtime
 * hands it the InputStream, which is why the same slot is also what the
 * concrete read bodies read bytes from.
 */
NJFN(isr_init)
{
    nj_ref self = NJ_THIS(args);
    nj_field *f = nj_field_lookup(vm, NJ_C_READER, "lock",
                                  "Ljava/lang/Object;", 0);
    nj_ref in = NJ_ARG_REF(args, 0);
    if (!in) {
        return nj_throw(vm, "java/lang/NullPointerException", "lock is null");
    }
    if (f) {
        nj_field_write_ref(vm, self, f, in);
    }
    return NJ_OK;
}

/*
 * Reader(): CLDC's protected no-argument constructor.  The lock object is the
 * reader itself, which is exactly what a subclass synchronises on (the
 * bundled de/enough/polish/io/StringReader does `getfield lock; monitorenter`
 * in every method).  There is no wrapped stream; a subclass that only
 * overrides read(char[],int,int) gets working inherited read()/skip() from
 * rd_read/rd_skip, which dispatch back into the override.
 */
NJFN(rd_init_lock)
{
    nj_ref self = NJ_THIS(args);
    nj_field *f = nj_field_lookup(vm, NJ_C_READER, "lock",
                                  "Ljava/lang/Object;", 0);
    if (f) {
        nj_field_write_ref(vm, self, f, self);
    }
    return NJ_OK;
}

NJFN(isr_ready)
{
    nj_ref in = strm_field(vm, NJ_THIS(args), NJ_C_READER, "lock",
                           "Ljava/lang/Object;");
    nj_value r;
    if (!in) {
        ret->i = 0;
        return NJ_OK;
    }
    r.i = 0;
    if (nj_call(vm, in, NJ_C_ISTREAM, "available", "()I", NULL, &r) != NJ_OK) {
        nj_clear_exception(vm);
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = (r.i > 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(isr_close)
{
    nj_ref in = strm_field(vm, NJ_THIS(args), NJ_C_READER, "lock",
                           "Ljava/lang/Object;");
    if (in) {
        nj_value a[4];
        a[0].bits = 0u;
        (void)nj_call(vm, in, NJ_C_ISTREAM, "close", "()V", a, NULL);
    }
    return NJ_OK;
}

NJFN(osw_init)
{
    nj_ref self = NJ_THIS(args);
    nj_field *f = nj_field_lookup(vm, NJ_C_WRITER, "wrOut",
                                  "Ljava/io/OutputStream;", 0);
    nj_ref out = NJ_ARG_REF(args, 0);
    if (!out) {
        return nj_throw(vm, "java/lang/NullPointerException", "out is null");
    }
    if (f) {
        nj_field_write_ref(vm, self, f, out);
    }
    return NJ_OK;
}

/*
 * The range forms, which are where CLDC's concrete methods actually arrive:
 * write(char[]), write(String) and read(char[]) all funnel into
 * write(char[],int,int) and read(char[],int,int).  An OutputStreamWriter that
 * only implements write(int) therefore turns every character-stream write
 * into a method error - which is exactly what the reader/writer end-to-end
 * fixture found the first time it ran.
 *
 * ISO-8859-1 again: the low byte each way, the same identity isr_read relies
 * on, so a round trip through these two is byte-exact rather than approximately
 * right.
 */
NJFN(osw_range)
{
    nj_ref out = strm_field(vm, NJ_THIS(args), NJ_C_WRITER, "wrOut",
                            "Ljava/io/OutputStream;");
    nj_ref chars = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 i;

    if (!out) {
        return nj_throw(vm, "java/io/IOException", "stream is closed");
    }
    if (!chars) {
        return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > chars->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    for (i = 0; i < len; i++) {
        nj_value a[4];
        a[0].i = (nj_s4)(((nj_u2 *)chars->u.bytes)[off + i] & 0xFFu);
        if (nj_call(vm, out, NJ_C_OSTREAM, "write", "(I)V", a, NULL) != NJ_OK) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

NJFN(isr_range)
{
    nj_ref in = strm_field(vm, NJ_THIS(args), NJ_C_READER, "lock",
                           "Ljava/lang/Object;");
    nj_ref chars = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_s4 i = 0;
    nj_value r;

    if (!in) {
        return nj_throw(vm, "java/io/IOException", "stream is closed");
    }
    if (!chars) {
        return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > chars->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    /*
     * Read one byte at a time and stop at the first end of stream.  Returning
     * the short count is the contract: -1 means nothing was read at all, and a
     * zero-length request must not consume a byte.
     */
    while (i < len) {
        r.i = 0;
        if (nj_call(vm, in, NJ_C_ISTREAM, "read", "()I", NULL, &r) != NJ_OK) {
            return NJ_ERR;
        }
        if (r.i < 0) {
            break;
        }
        ((nj_u2 *)chars->u.bytes)[off + i] = (nj_u2)(r.i & 0xFF);
        i++;
    }
    ret->i = (i == 0 && len > 0) ? -1 : i;
    return NJ_OK;
}

NJFN(osw_flush)
{
    nj_ref out = strm_field(vm, NJ_THIS(args), NJ_C_WRITER, "wrOut",
                            "Ljava/io/OutputStream;");
    if (out) {
        nj_value a[4];
        a[0].bits = 0u;
        (void)nj_call(vm, out, NJ_C_OSTREAM, "flush", "()V", a, NULL);
    }
    return NJ_OK;
}

#include "nj_io_charset.inc"

/*
 * The concrete half of java.io.Reader and java.io.Writer.
 *
 * CLDC defines five of their methods on top of the abstract ones, and all five
 * were declared with no implementation behind them: read(char[]) is
 * read(b, 0, b.length), write(int) and write(String) end up in
 * write(char[], int, int), and skip(long) reads and discards.  Until these
 * existed a MIDlet calling any of them got UnsatisfiedLinkError, and
 * tests/test_natives listed them by name as the only known gap.
 *
 * Every one of them goes through the abstract method rather than doing the
 * work here, which is what makes a subclass's implementation take effect.
 * (The tables that declare these methods use M(...) directly; see the note
 * above reader_methods for why there is no private row macro any more.)
 */
static nj_s4 ch_len(nj_ref a)
{
    return a ? (nj_s4)a->length : 0;
}

NJFN(rd_read_chars)
{
    nj_value a[4];
    nj_value r;
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    }
    a[0].r = b;
    a[1].i = 0;
    a[2].i = ch_len(b);
    r.i = -1;
    if (nj_call(vm, NJ_THIS(args), NJ_C_READER, "read", "([CII)I", a, &r)
                != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = r.i;
    return NJ_OK;
}

/*
 * Reader.read(): CLDC gives it the default body that reads one character by
 * delegating to read(char[],int,int).  Going through the abstract method is
 * what makes a subclass override take effect; it is also why a plain Reader
 * built over an InputStream keeps reading, because the receiver's
 * read(char[],int,int) row is isr_range.
 */
NJFN(rd_read)
{
    nj_ref one = nj_char_array_new(vm, 1u);
    nj_value a[3];
    nj_value r;
    if (!one) {
        return NJ_ERR;
    }
    a[0].r = one;
    a[1].i = 0;
    a[2].i = 1;
    r.i = -1;
    if (nj_call(vm, NJ_THIS(args), NJ_C_READER, "read", "([CII)I", a, &r)
                != NJ_OK) {
        return NJ_ERR;
    }
    if (r.i < 0) {
        ret->i = -1;
    } else {
        ret->i = (nj_s4)((nj_u2 *)nj_array_data(one))[0];
    }
    return NJ_OK;
}

/*
 * Reader.ready(): CLDC's default body, and it is "never ready" - a Reader that
 * owns no stream cannot know that a character is available.  A subclass may
 * override it; InputStreamReader does, through isr_ready's InputStream
 * available() probe, and keeps its own binding in nj_lib_io_table.  This
 * generic row must not be isr_ready: a plain Reader's lock is the reader
 * itself, not an InputStream, so probing it would only raise and swallow a
 * lookup error (de/enough/polish/io/StringReader and any javac-compiled
 * Reader subclass inherit this default).
 */
NJFN(rd_ready)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(rd_skip)
{
    nj_s8 want = NJ_ARG_LONG(args, 0);
    nj_s8 done = 0;

    while (done < want) {
        nj_value r;
        r.i = -1;
        if (nj_call(vm, NJ_THIS(args), NJ_C_READER, "read", "()I", NULL, &r)
            != NJ_OK) {
            return NJ_ERR;
        }
        if (r.i < 0) {
            break;      /* end of stream: report what was actually skipped */
        }
        done++;
    }
    ret->j = done;
    return NJ_OK;
}

NJFN(wr_write_int)
{
    nj_value a[4];
    nj_ref one = nj_char_array_new(vm, 1u);
    nj_u2 *p;

    if (!one) {
        return NJ_ERR;
    }
    p = (nj_u2 *)nj_array_data(one);
    if (p) {
        p[0] = (nj_u2)NJ_ARG_INT(args, 0);
    }
    a[0].r = one;
    a[1].i = 0;
    a[2].i = 1;
    if (nj_call(vm, NJ_THIS(args), NJ_C_WRITER, "write", "([CII)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(wr_write_chars)
{
    nj_value a[4];
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    }
    a[0].r = b;
    a[1].i = 0;
    a[2].i = ch_len(b);
    if (nj_call(vm, NJ_THIS(args), NJ_C_WRITER, "write", "([CII)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(wr_write_str)
{
    nj_value a[4];
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_ref c;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "string is null");
    }
    c = nj_call_ref(vm, s, "java/lang/String", "toCharArray", "()[C", NULL);
    if (!c) {
        return NJ_ERR;
    }
    a[0].r = c;
    a[1].i = 0;
    a[2].i = ch_len(c);
    if (nj_call(vm, NJ_THIS(args), NJ_C_WRITER, "write", "([CII)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

/*
 * Writer.close(): the mirror of isr_close, over the Writer slot, so a bare
 * Writer, an OutputStreamWriter and (through the inherited field) any
 * subclass all close what they were handed.
 */
NJFN(wr_close)
{
    nj_ref out = strm_field(vm, NJ_THIS(args), NJ_C_WRITER, "wrOut",
                            "Ljava/io/OutputStream;");
    if (out) {
        nj_value a[4];
        a[0].bits = 0u;
        (void)nj_call(vm, out, NJ_C_OSTREAM, "close", "()V", a, NULL);
    }
    return NJ_OK;
}

/*
 * mark()/markSupported()/reset(), and the range form of write(String).  The
 * three Reader methods are concrete in CLDC, and this runtime's character
 * streams wrap a byte stream and remember nothing: markSupported() is false,
 * mark() has nothing to record, and reset() refuses with the IOException the
 * specification names.  write(String,int,int) is the slice form of
 * write(String); it is the one Writer method OutputStreamWriter does not
 * inherit a body for, so the body lives here and serves both.
 */
NJFN(rd_mark_supported)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(rd_mark)
{
    (void)NJ_ARG_INT(args, 0);   /* nothing is remembered, on purpose */
    return NJ_OK;
}

NJFN(rd_reset)
{
    return nj_throw(vm, "java/io/IOException", "mark/reset not supported");
}

NJFN(wr_write_str_range)
{
    nj_value a[3];
    nj_ref s = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_ref c;
    nj_u2 *p;
    nj_s4 i;

    if (!s) {
        return nj_throw(vm, "java/lang/NullPointerException", "string is null");
    }
    if (off < 0 || len < 0 || (nj_u4)off > nj_string_len(s) ||
        (nj_u4)len > nj_string_len(s) - (nj_u4)off) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException",
                        "string index out of range");
    }
    /* Empty slices also dispatch to the subclass, which checks for close(). */
    /* Copy the slice straight into the char[] the range form takes; going
     * through String.substring would allocate twice for the same characters. */
    c = nj_char_array_new(vm, (nj_u4)len);
    if (!c) {
        return NJ_ERR;
    }
    p = (nj_u2 *)nj_array_data(c);
    if (!p && len > 0) {
        return NJ_ERR;
    }
    for (i = 0; i < len; i++) {
        p[i] = nj_string_chars(s)[off + i];
    }
    a[0].r = c;
    a[1].i = 0;
    a[2].i = len;
    if (nj_call(vm, NJ_THIS(args), NJ_C_WRITER, "write", "([CII)V", a, NULL)
        != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

/*
 * Reader's protected lock object, which CLDC names lock.  The same slot does
 * double duty in this runtime: Reader(Object lock) stores whatever it is
 * handed, and an InputStreamReader is handed its stream, so the concrete read
 * bodies can still read bytes out of it.  Reader() stores the reader itself,
 * and a subclass synchronises on that (de/enough/polish/io/StringReader does
 * `getfield lock; monitorenter`).  Writer's stream slot is unchanged.
 */
static const nj_builtin_field rd_fields[] = {
    /* CLDC declares Reader.lock protected; StringReader subclasses read it
     * with getfield, so the access flags have to match the specification. */
    { "lock", "Ljava/lang/Object;", NJ_ACC_PROTECTED }
};
static const nj_builtin_field wr_fields[] = {
    { "wrOut", "Ljava/io/OutputStream;", 0 }
};

/*
 * Reader and Writer are abstract classes in CLDC, and this runtime registers
 * them as concrete rows because a builtin table has one shape.  Their methods
 * are concrete here wherever the specification gives them a body: read(),
 * read(char[]), read(char[],int,int), ready(), skip(long), close(), mark(),
 * markSupported() and reset() for Reader, and the write family, flush() and
 * close() for Writer.  The storage above makes those bodies real rather than
 * formal - a Reader built over an InputStream reads that stream, exactly as an
 * InputStreamReader does - and it is also the protected lock object CLDC
 * declares, which is why Reader() and Reader(Object) both exist.
 *
 * The rows are written with M(...) rather than a private brace macro on
 * purpose: tools/m3g_api_audit.py reads method tables by looking for M(
 * entries, so a wrapper macro hides the whole table from the audit and every
 * member of it is reported as a member this runtime cannot resolve.  The table
 * is the interface - it has to be legible to the tool that checks it.
 */
static const nj_builtin_method reader_methods[] = {
    M("<init>", "()V",     NJ_ACC_PROTECTED),
    M("<init>", "(Ljava/lang/Object;)V", NJ_ACC_PROTECTED),
    M("close",  "()V",     NJ_ACC_PUBLIC),
    M("mark",   "(I)V",    NJ_ACC_PUBLIC),
    M("markSupported", "()Z", NJ_ACC_PUBLIC),
    M("read",   "()I",     NJ_ACC_PUBLIC),
    M("read",   "([C)I",   NJ_ACC_PUBLIC),
    M("read",   "([CII)I", NJ_ACC_PUBLIC),
    M("ready",  "()Z",     NJ_ACC_PUBLIC),
    M("reset",  "()V",     NJ_ACC_PUBLIC),
    M("skip",   "(J)J",    NJ_ACC_PUBLIC)
};
static const nj_builtin_method writer_methods[] = {
    M("<init>", "(Ljava/lang/Object;)V", NJ_ACC_PROTECTED),
    M("close",  "()V",     NJ_ACC_PUBLIC),
    M("flush",  "()V",     NJ_ACC_PUBLIC),
    M("write",  "(I)V",    NJ_ACC_PUBLIC),
    M("write",  "([C)V",   NJ_ACC_PUBLIC),
    M("write",  "([CII)V", NJ_ACC_PUBLIC),
    M("write",  "(Ljava/lang/String;)V", NJ_ACC_PUBLIC),
    M("write",  "(Ljava/lang/String;II)V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method isr_methods[] = {
    M("<init>", "(L" NJ_C_ISTREAM ";)V", NJ_ACC_PUBLIC),
    M("<init>", "(L" NJ_C_ISTREAM ";Ljava/lang/String;)V",
       NJ_ACC_PUBLIC),
    M("read",   "()I", NJ_ACC_PUBLIC),
    M("read",   "([CII)I", NJ_ACC_PUBLIC),
    M("skip",   "(J)J", NJ_ACC_PUBLIC),
    M("ready",  "()Z", NJ_ACC_PUBLIC),
    M("markSupported", "()Z", NJ_ACC_PUBLIC),
    M("mark",   "(I)V", NJ_ACC_PUBLIC),
    M("reset",  "()V", NJ_ACC_PUBLIC),
    M("close",  "()V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method osw_methods[] = {
    M("<init>", "(L" NJ_C_OSTREAM ";)V", NJ_ACC_PUBLIC),
    M("<init>", "(L" NJ_C_OSTREAM ";Ljava/lang/String;)V",
       NJ_ACC_PUBLIC),
    M("write",  "(I)V", NJ_ACC_PUBLIC),
    M("write",  "([CII)V", NJ_ACC_PUBLIC),
    M("write",  "(Ljava/lang/String;II)V", NJ_ACC_PUBLIC),
    M("flush",  "()V", NJ_ACC_PUBLIC),
    M("close",  "()V", NJ_ACC_PUBLIC)
};

/* ------------------------------------------------------------------ */
/* java.io.PrintStream                                                 */
/* ------------------------------------------------------------------ */
/*
 * PrintStream's class row belongs to the core module - System.out and
 * System.err are created there with it - but the native registry is keyed by
 * class name, method name and descriptor across every module
 * (src/core/nj_native.c), so these bodies bind to that class without touching
 * the row.  A method the core table did not declare simply has no native to
 * bind to; nothing here depends on one being there.
 *
 * EVERY OVERLOAD FORMATS THE WAY THE CLASS ALREADY DID.  The value goes
 * through the runtime's own String.valueOf, which is Integer/Long/Float/
 * Double's own toString, and the resulting String is handed to print(String)
 * or println(String) - the two methods whose output path already existed.  So
 * there is one formatter and one way to the port log, not a second one.
 *
 * checkError(), setError() and the stream given to the constructor need
 * somewhere to live: they use the instance fields "psError" and "psOut" on the
 * class.  Every lookup below is NULL-safe, so a build whose PrintStream has no
 * such field still runs - checkError() then reports false, setError() has
 * nothing to record and write(byte[],int,int) goes to the log, which is what
 * System.out can honestly do.
 */
static nj_field *ps_field(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, "java/io/PrintStream", name, desc, 0);
}

/* Hand an already formatted string to the method that existed before. */
static nj_status ps_hand_over(nj_vm *vm, nj_ref self, nj_ref s, int newline)
{
    nj_value a[1];

    if (!s) {
        return NJ_ERR;      /* formatting threw; the exception is pending */
    }
    a[0].r = s;
    return nj_call(vm, self, "java/io/PrintStream",
                   newline ? "println" : "print", "(Ljava/lang/String;)V",
                   a, NULL);
}

/* Format one argument with String.valueOf(...) and hand the result over. */
static nj_status ps_fmt(nj_vm *vm, nj_ref self, const char *value_of_desc,
                        nj_value *a, int newline)
{
    nj_value r;

    r.r = NULL;
    if (nj_call_static(vm, "java/lang/String", "valueOf", value_of_desc,
                       a, &r) != NJ_OK) {
        return NJ_ERR;
    }
    return ps_hand_over(vm, self, r.r, newline);
}

NJFN(ps_init)
{
    nj_ref out = NJ_ARG_REF(args, 0);
    nj_field *f = ps_field(vm, "psOut", "Ljava/io/OutputStream;");

    if (!out) {
        return nj_throw(vm, "java/lang/NullPointerException", "out is null");
    }
    if (f) {
        nj_field_write_ref(vm, NJ_THIS(args), f, out);
    }
    return NJ_OK;
}

NJFN(ps_check_error)
{
    nj_field *f = ps_field(vm, "psError", "Z");

    /* checkError() flushes first, so a stream that only failed on the last
     * buffered byte still reports the error. */
    if (nj_call(vm, NJ_THIS(args), "java/io/PrintStream", "flush", "()V",
                NULL, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = (f && nj_field_read_int(vm, NJ_THIS(args), f) != 0) ? 1 : 0;
    return NJ_OK;
}

NJFN(ps_set_error)
{
    nj_field *f = ps_field(vm, "psError", "Z");

    if (f) {
        nj_field_write_int(vm, NJ_THIS(args), f, 1);
    }
    return NJ_OK;
}

/*
 * write(byte[],int,int).  A PrintStream built over a stream writes to that
 * stream - it is the only way the object the title handed over can ever be
 * observed - while System.out, built with none, falls back to write(int),
 * the path its println has always used.
 */
NJFN(ps_write_bytes_range)
{
    nj_ref b = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    nj_s4 len = NJ_ARG_INT(args, 2);
    nj_ref out = strm_field(vm, NJ_THIS(args), "java/io/PrintStream", "psOut",
                            "Ljava/io/OutputStream;");
    nj_s4 i;

    if (!b) {
        return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    }
    if (off < 0 || len < 0 || (nj_u4)(off + len) > b->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    }
    if (out) {
        nj_value a[4];
        a[0].r = b;
        a[1].i = off;
        a[2].i = len;
        return nj_call(vm, out, NJ_C_OSTREAM, "write", "([BII)V", a, NULL);
    }
    for (i = 0; i < len; i++) {
        nj_value a[1];
        a[0].i = (nj_s4)(((nj_u1 *)b->u.bytes)[off + i] & 0xFFu);
        if (nj_call(vm, NJ_THIS(args), "java/io/PrintStream", "write", "(I)V",
                    a, NULL) != NJ_OK) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

NJFN(ps_print_bool)
{
    nj_value a[1];

    a[0].i = NJ_ARG_INT(args, 0) ? 1 : 0;
    return ps_fmt(vm, NJ_THIS(args), "(Z)Ljava/lang/String;", a, 0);
}

NJFN(ps_print_long)
{
    nj_value a[2];

    a[0].j = NJ_ARG_LONG(args, 0);
    a[1].bits = 0u;      /* the upper half of the two-slot value */
    return ps_fmt(vm, NJ_THIS(args), "(J)Ljava/lang/String;", a, 0);
}

NJFN(ps_print_float)
{
    nj_value a[1];

    a[0].f = NJ_ARG_FLOAT(args, 0);
    return ps_fmt(vm, NJ_THIS(args), "(F)Ljava/lang/String;", a, 0);
}

NJFN(ps_print_double)
{
    nj_value a[2];

    a[0].d = NJ_ARG_DOUBLE(args, 0);
    a[1].bits = 0u;      /* the upper half of the two-slot value */
    return ps_fmt(vm, NJ_THIS(args), "(D)Ljava/lang/String;", a, 0);
}

NJFN(ps_print_chars)
{
    nj_value a[1];

    a[0].r = NJ_ARG_REF(args, 0);
    return ps_fmt(vm, NJ_THIS(args), "([C)Ljava/lang/String;", a, 0);
}

NJFN(ps_print_obj)
{
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_ref s;

    if (!o) {
        s = nj_string_new(vm, "null");
    } else {
        s = nj_call_ref(vm, o, "java/lang/Object", "toString",
                        "()Ljava/lang/String;", NULL);
    }
    return ps_hand_over(vm, NJ_THIS(args), s, 0);
}

NJFN(ps_println_char)
{
    nj_value a[1];

    a[0].i = NJ_ARG_INT(args, 0);
    return ps_fmt(vm, NJ_THIS(args), "(C)Ljava/lang/String;", a, 1);
}

NJFN(ps_println_long)
{
    nj_value a[2];

    a[0].j = NJ_ARG_LONG(args, 0);
    a[1].bits = 0u;      /* the upper half of the two-slot value */
    return ps_fmt(vm, NJ_THIS(args), "(J)Ljava/lang/String;", a, 1);
}

NJFN(ps_println_float)
{
    nj_value a[1];

    a[0].f = NJ_ARG_FLOAT(args, 0);
    return ps_fmt(vm, NJ_THIS(args), "(F)Ljava/lang/String;", a, 1);
}

NJFN(ps_println_double)
{
    nj_value a[2];

    a[0].d = NJ_ARG_DOUBLE(args, 0);
    a[1].bits = 0u;      /* the upper half of the two-slot value */
    return ps_fmt(vm, NJ_THIS(args), "(D)Ljava/lang/String;", a, 1);
}

NJFN(ps_println_chars)
{
    nj_value a[1];

    a[0].r = NJ_ARG_REF(args, 0);
    return ps_fmt(vm, NJ_THIS(args), "([C)Ljava/lang/String;", a, 1);
}

/*
 * Connector.READ / WRITE / READ_WRITE.  A MIDlet writes Connector.READ when it
 * opens a connection, so these are not decoration - and javac inlines the
 * stub's value, which makes a missing runtime field a NoSuchFieldError at the
 * moment the game opens a file.
 */
static const nj_builtin_field connector_fields[] = {
    { "READ",       "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "WRITE",      "I", NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "READ_WRITE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL }
};

nj_status nj_io_late_init(nj_vm *vm);

nj_status nj_io_late_init(nj_vm *vm)
{
    static const struct { const char *n; nj_s4 v; } k[] = {
        { "READ", 1 }, { "WRITE", 2 }, { "READ_WRITE", 3 }
    };
    unsigned i;
    if (!vm) {
        return NJ_ERR;
    }
    for (i = 0u; i < sizeof(k) / sizeof(k[0]); i++) {
        nj_field *f = nj_field_lookup(vm, "javax/microedition/io/Connector",
                                      k[i].n, "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, k[i].v);
        }
    }
    return NJ_OK;
}

/*
 * CLDC 1.1 declares DataInput and DataOutput as interfaces, and MIDlets call
 * their methods through an interface reference: 拳皇98终极之战_7534 does
 * `invokeinterface java/io/DataInput.readByte:()B` on a DataInputStream.
 * The call is resolved through the interface, so the method set has to be
 * declared here; dispatch itself still happens on the receiver's class, where
 * DataInputStream and DataOutputStream already answer every one of them.
 *
 * readFloat/readDouble and writeFloat/writeDouble/writeChars were the rows
 * missing until now: 急速公路狂飙－新年版_84 loads its track models by calling
 * DataInput.readFloat through an interface reference, which died with
 * NoSuchMethodError during track loading after the menus had already drawn.
 */
static const nj_builtin_method datainput_methods[] = {
    M("readFully", "([B)V", NJ_ACC_ABSTRACT),
    M("readFully", "([BII)V", NJ_ACC_ABSTRACT),
    M("skipBytes", "(I)I", NJ_ACC_ABSTRACT),
    M("readBoolean", "()Z", NJ_ACC_ABSTRACT),
    M("readByte", "()B", NJ_ACC_ABSTRACT),
    M("readUnsignedByte", "()I", NJ_ACC_ABSTRACT),
    M("readShort", "()S", NJ_ACC_ABSTRACT),
    M("readUnsignedShort", "()I", NJ_ACC_ABSTRACT),
    M("readChar", "()C", NJ_ACC_ABSTRACT),
    M("readInt", "()I", NJ_ACC_ABSTRACT),
    M("readLong", "()J", NJ_ACC_ABSTRACT),
    M("readFloat", "()F", NJ_ACC_ABSTRACT),
    M("readDouble", "()D", NJ_ACC_ABSTRACT),
    M("readUTF", "()Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method dataoutput_methods[] = {
    M("write", "(I)V", NJ_ACC_ABSTRACT),
    M("write", "([B)V", NJ_ACC_ABSTRACT),
    M("write", "([BII)V", NJ_ACC_ABSTRACT),
    M("writeBoolean", "(Z)V", NJ_ACC_ABSTRACT),
    M("writeByte", "(I)V", NJ_ACC_ABSTRACT),
    M("writeShort", "(I)V", NJ_ACC_ABSTRACT),
    M("writeChar", "(I)V", NJ_ACC_ABSTRACT),
    M("writeInt", "(I)V", NJ_ACC_ABSTRACT),
    M("writeLong", "(J)V", NJ_ACC_ABSTRACT),
    M("writeFloat", "(F)V", NJ_ACC_ABSTRACT),
    M("writeDouble", "(D)V", NJ_ACC_ABSTRACT),
    M("writeChars", "(Ljava/lang/String;)V", NJ_ACC_ABSTRACT),
    M("writeUTF", "(Ljava/lang/String;)V", NJ_ACC_ABSTRACT)
};

/*
 * StreamConnectionNotifier is the server half of javax.microedition.io: a
 * MIDlet opens a local URL, calls acceptAndOpen() and gets the connection a
 * peer made.  It is declared here because it is an io type, even though the
 * only thing in this runtime that can answer it is javax.bluetooth - a port
 * with a Bluetooth hook family gets it, and a port without one never sees the
 * class used, because opening the URL already failed.
 */
static const nj_builtin_method streamnotifier_methods[] = {
    { "acceptAndOpen", "()Ljavax/microedition/io/StreamConnection;",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT }
};

const nj_builtin_class nj_lib_io_classes[] = {
    { "javax/microedition/io/StreamConnectionNotifier",
      "javax/microedition/io/Connection",
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, streamnotifier_methods,
      (nj_u2)(sizeof(streamnotifier_methods) / sizeof(streamnotifier_methods[0])) },
    { NJ_C_READER, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      rd_fields, (nj_u2)(sizeof(rd_fields) / sizeof(rd_fields[0])),
      reader_methods,
      (nj_u2)(sizeof(reader_methods) / sizeof(reader_methods[0])) },
    { NJ_C_WRITER, "java/lang/Object",
      NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      wr_fields, (nj_u2)(sizeof(wr_fields) / sizeof(wr_fields[0])),
      writer_methods,
      (nj_u2)(sizeof(writer_methods) / sizeof(writer_methods[0])) },
    { NJ_C_ISR, NJ_C_READER, NJ_ACC_PUBLIC,
      CS_FIELDS, CS_NFIELDS, /* Reader.lock holds the stream. */
      isr_methods,
      (nj_u2)(sizeof(isr_methods) / sizeof(isr_methods[0])) },
    { NJ_C_OSW, NJ_C_WRITER, NJ_ACC_PUBLIC,
      CS_FIELDS, CS_NFIELDS, /* Writer.wrOut holds the stream. */
      osw_methods,
      (nj_u2)(sizeof(osw_methods) / sizeof(osw_methods[0])) },
    { "java/io/InputStream", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      NULL, 0u, inputstream_methods,
      (nj_u2)(sizeof(inputstream_methods) / sizeof(inputstream_methods[0])) },
    { "java/io/OutputStream", "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      NULL, 0u, outputstream_methods,
      (nj_u2)(sizeof(outputstream_methods) / sizeof(outputstream_methods[0])) },
    { FIS, NJ_C_ISTREAM, NJ_ACC_PUBLIC,
      fis_fields, (nj_u2)(sizeof(fis_fields) / sizeof(fis_fields[0])),
      fis_methods, (nj_u2)(sizeof(fis_methods) / sizeof(fis_methods[0])) },
    { "java/io/ByteArrayInputStream", "java/io/InputStream", NJ_ACC_PUBLIC,
      bais_fields, (nj_u2)(sizeof(bais_fields) / sizeof(bais_fields[0])),
      bais_methods, (nj_u2)(sizeof(bais_methods) / sizeof(bais_methods[0])) },
    { "java/io/ByteArrayOutputStream", "java/io/OutputStream", NJ_ACC_PUBLIC,
      baos_fields, (nj_u2)(sizeof(baos_fields) / sizeof(baos_fields[0])),
      baos_methods, (nj_u2)(sizeof(baos_methods) / sizeof(baos_methods[0])) },
    { FOS, BAOS, NJ_ACC_PUBLIC,
      fos_fields, (nj_u2)(sizeof(fos_fields) / sizeof(fos_fields[0])),
      fos_methods, (nj_u2)(sizeof(fos_methods) / sizeof(fos_methods[0])) },
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
    NJ_IO_EXC("java/io/UTFDataFormatException", "java/io/IOException")
    NJ_IO_EXC("javax/microedition/io/ConnectionNotFoundException",
              "java/io/IOException")
    { "javax/microedition/io/Connector", "java/lang/Object", NJ_ACC_PUBLIC,
      connector_fields,
      (nj_u2)(sizeof(connector_fields) / sizeof(connector_fields[0])),
      connector_methods,
      (nj_u2)(sizeof(connector_methods) / sizeof(connector_methods[0])) },
    { "javax/microedition/io/Connection",
      NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, connection_methods,
      (nj_u2)(sizeof(connection_methods) / sizeof(connection_methods[0])) },
    { "java/io/DataInput", NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, datainput_methods,
      (nj_u2)(sizeof(datainput_methods) / sizeof(datainput_methods[0])) },
    { "java/io/DataOutput", NULL,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, dataoutput_methods,
      (nj_u2)(sizeof(dataoutput_methods) / sizeof(dataoutput_methods[0])) }
};
const int nj_lib_io_class_count =
    (int)(sizeof(nj_lib_io_classes) / sizeof(nj_lib_io_classes[0]));

const nj_native_entry nj_lib_io_table[] = {
    /*
     * Reader and Writer carry the concrete bodies the specification gives
     * them (see reader_methods); the lock slot Reader owns doubles as the
     * stream an InputStreamReader is built over, and the subclasses below
     * inherit both.  ready() is the one method whose body differs between the
     * two: a plain Reader answers false (rd_ready), an InputStreamReader
     * probes the stream it wraps (isr_ready, bound by its own row below).
     */
    { NJ_C_READER, "<init>", "()V",                 rd_init_lock },
    { NJ_C_READER, "<init>", "(Ljava/lang/Object;)V", isr_init },
    { NJ_C_READER, "close",  "()V",                 isr_close },
    { NJ_C_READER, "mark",   "(I)V",                rd_mark },
    { NJ_C_READER, "markSupported", "()Z",          rd_mark_supported },
    { NJ_C_READER, "read",   "()I",                 rd_read },
    { NJ_C_READER, "read",  "([C)I",                rd_read_chars },
    { NJ_C_READER, "read",   "([CII)I",             isr_range },
    { NJ_C_READER, "ready",  "()Z",                 rd_ready },
    { NJ_C_READER, "reset",  "()V",                 rd_reset },
    { NJ_C_READER, "skip",   "(J)J",                rd_skip },
    { NJ_C_WRITER, "<init>", "(Ljava/lang/Object;)V", osw_init },
    { NJ_C_WRITER, "close",  "()V",                 wr_close },
    { NJ_C_WRITER, "flush",  "()V",                 osw_flush },
    { NJ_C_WRITER, "write", "(I)V",                 wr_write_int },
    { NJ_C_WRITER, "write", "([C)V",                wr_write_chars },
    { NJ_C_WRITER, "write", "([CII)V",              osw_range },
    { NJ_C_WRITER, "write", "(Ljava/lang/String;)V", wr_write_str },
    { NJ_C_WRITER, "write", "(Ljava/lang/String;II)V", wr_write_str_range },
    { "java/io/InputStream", "<init>",        "()V", is_close },
    { "java/io/InputStream", "read",          "()I", is_read_abstract },
    { "java/io/InputStream", "read",          "([B)I", is_read_bytes },
    { "java/io/InputStream", "read",          "([BII)I", is_read_bytes_range },
    { "java/io/InputStream", "skip",          "(J)J", is_skip },
    { "java/io/InputStream", "available",     "()I", is_available },
    { "java/io/InputStream", "close",         "()V", is_close },
    { "java/io/InputStream", "mark",          "(I)V", is_close },
    { "java/io/InputStream", "reset",         "()V", is_reset },
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
    { "java/io/ByteArrayInputStream", "read",          "([BII)I", bais_read_bytes_range },
    { "java/io/ByteArrayInputStream", "available",     "()I", bais_available },
    { "java/io/ByteArrayInputStream", "skip",          "(J)J", bais_skip },
    { "java/io/ByteArrayInputStream", "mark",          "(I)V", bais_mark },
    { "java/io/ByteArrayInputStream", "reset",         "()V", bais_reset },
    { "java/io/ByteArrayInputStream", "markSupported", "()Z", bais_mark_supported },
    { "java/io/ByteArrayInputStream", "close",         "()V", bais_close },

    { "java/io/ByteArrayOutputStream", "<init>",      "()V", baos_init },
    { "java/io/ByteArrayOutputStream", "<init>",      "(I)V", baos_init_cap },
    { "java/io/ByteArrayOutputStream", "write",       "(I)V", baos_write },
    { "java/io/ByteArrayOutputStream", "write",       "([BII)V", baos_write_range },
    { "java/io/ByteArrayOutputStream", "toByteArray", "()[B", baos_toByteArray },
    { "java/io/ByteArrayOutputStream", "size",        "()I", baos_size },
    { "java/io/ByteArrayOutputStream", "reset",       "()V", baos_reset },
    { "java/io/ByteArrayOutputStream", "toString",    "()Ljava/lang/String;", baos_toString },
    /*
     * close() must NOT reset: ByteArrayOutputStream.close() is documented as
     * having no effect, and 寻秦皇陵的诅咒_12450 does write(buf,0,n), close(),
     * toByteArray() - bound to baos_reset it got an empty image back and
     * reported IllegalArgumentException "no image data" once per frame.
     */
    { "java/io/ByteArrayOutputStream", "close",       "()V", is_close },
    { FIS, "<init>", "(Ljava/lang/String;)V", fis_init },
    { FIS, "read", "()I", fis_read },
    { FIS, "read", "([BII)I", fis_read_range },
    { FIS, "skip", "(J)J", fis_skip },
    { FIS, "available", "()I", fis_available },
    { FIS, "close", "()V", fis_close },
    { FOS, "<init>", "(Ljava/lang/String;)V", fos_init },
    { FOS, "flush",  "()V", fos_flush },
    { FOS, "close",  "()V", fos_close },
    { FOS, "write", "(I)V", fos_write },
    { FOS, "write", "([BII)V", fos_write_range },

    { "java/io/DataInputStream", "<init>",           "(Ljava/io/InputStream;)V", dis_init },
    { "java/io/DataInputStream", "read",             "()I", dis_read },
    { "java/io/DataInputStream", "read",             "([BII)I", dis_read_range },
    { "java/io/DataInputStream", "readFully",        "([B)V", dis_readFully_all },
    { "java/io/DataInputStream", "readFully",        "([BII)V", dis_readFully },
    { "java/io/DataInputStream", "skipBytes",        "(I)I", dis_skip_bytes },
    { "java/io/DataInputStream", "readBoolean",      "()Z", dis_read_boolean },
    { "java/io/DataInputStream", "readByte",         "()B", dis_read_byte },
    { "java/io/DataInputStream", "readUnsignedByte", "()I", dis_read_unsigned_byte },
    { "java/io/DataInputStream", "readShort",        "()S", dis_read_short },
    { "java/io/DataInputStream", "readUnsignedShort","()I", dis_read_unsigned_short },
    { "java/io/DataInputStream", "readChar",         "()C", dis_read_char },
    { "java/io/DataInputStream", "readInt",          "()I", dis_read_int },
    { "java/io/DataInputStream", "readLong",         "()J", dis_read_long },
    { "java/io/DataInputStream", "readFloat",        "()F", dis_read_float },
    { "java/io/DataInputStream", "readDouble",       "()D", dis_read_double },
    { "java/io/DataInputStream", "readUTF",          "()Ljava/lang/String;", dis_readUTF },
    { "java/io/DataInputStream", "readUTF",          "(Ljava/io/DataInput;)Ljava/lang/String;", dis_readUTF_static },
    { "java/io/DataInputStream", "available",        "()I", dis_available },
    { "java/io/DataInputStream", "mark",             "(I)V", dis_mark },
    { "java/io/DataInputStream", "reset",            "()V", dis_reset },
    { "java/io/DataInputStream", "markSupported",    "()Z", dis_mark_supported },
    { "java/io/DataInputStream", "close",            "()V", is_close },

    { "java/io/DataOutputStream", "<init>",       "(Ljava/io/OutputStream;)V", dos_init },
    { "java/io/DataOutputStream", "write",        "(I)V", dos_write },
    { "java/io/DataOutputStream", "write",        "([BII)V", dos_write_range },
    { "java/io/DataOutputStream", "writeBoolean", "(Z)V", dos_write_boolean },
    { "java/io/DataOutputStream", "writeByte",    "(I)V", dos_write },
    { "java/io/DataOutputStream", "writeShort",   "(I)V", dos_write_short },
    { "java/io/DataOutputStream", "writeChar",    "(I)V", dos_write_char },
    { "java/io/DataOutputStream", "writeChars",   "(Ljava/lang/String;)V", dos_writeChars },
    { "java/io/DataOutputStream", "writeInt",     "(I)V", dos_write_int },
    { "java/io/DataOutputStream", "writeLong",    "(J)V", dos_write_long },
    { "java/io/DataOutputStream", "writeFloat",   "(F)V", dos_write_float },
    { "java/io/DataOutputStream", "writeDouble",  "(D)V", dos_write_double },
    { "java/io/DataOutputStream", "writeUTF",     "(Ljava/lang/String;)V", dos_writeUTF },
    { "java/io/DataOutputStream", "flush",        "()V", dos_flush },
    { "java/io/DataOutputStream", "close",        "()V", dos_flush },
    { "java/io/DataOutputStream", "size",         "()I", dos_size },

    { NJ_C_ISR, "<init>", "(L" NJ_C_ISTREAM ";)V", isr_init },
    { NJ_C_ISR, "<init>", "(L" NJ_C_ISTREAM ";Ljava/lang/String;)V", isr_init_encoding },
    { NJ_C_ISR, "read",  "()I", isr_encoded_read },
    { NJ_C_ISR, "read",  "([CII)I", isr_encoded_range },
    { NJ_C_ISR, "skip",  "(J)J", rd_skip },
    { NJ_C_ISR, "ready", "()Z", isr_encoded_ready },
    { NJ_C_ISR, "mark",  "(I)V", rd_mark },
    { NJ_C_ISR, "markSupported", "()Z", rd_mark_supported },
    { NJ_C_ISR, "reset", "()V", rd_reset },
    { NJ_C_ISR, "close", "()V", isr_stream_close },
    { NJ_C_OSW, "<init>", "(L" NJ_C_OSTREAM ";)V", osw_init },
    { NJ_C_OSW, "<init>", "(L" NJ_C_OSTREAM ";Ljava/lang/String;)V", osw_init_encoding },
    { NJ_C_OSW, "write", "(I)V", osw_encoded_write },
    { NJ_C_OSW, "write", "([CII)V", osw_encoded_range },
    { NJ_C_OSW, "write", "(Ljava/lang/String;II)V", wr_write_str_range },
    { NJ_C_OSW, "flush", "()V", osw_stream_flush },
    { NJ_C_OSW, "close", "()V", osw_stream_close },
    { "javax/microedition/io/Connector", "open", "(Ljava/lang/String;)Ljavax/microedition/io/Connection;", connector_open },
    { NJ_C_ISR, "<init>", "(L" NJ_C_ISTREAM ";)V", isr_init },
    { NJ_C_ISR, "<init>", "(L" NJ_C_ISTREAM ";Ljava/lang/String;)V", isr_init_encoding },

    { "javax/microedition/io/Connector", "open", "(Ljava/lang/String;I)Ljavax/microedition/io/Connection;", connector_open_stream },
    { "javax/microedition/io/Connector", "open", "(Ljava/lang/String;IZ)Ljavax/microedition/io/Connection;", connector_open_stream },
    { "javax/microedition/io/Connector", "openInputStream", "(Ljava/lang/String;)Ljava/io/InputStream;", connector_open },
    { "javax/microedition/io/Connector", "openOutputStream", "(Ljava/lang/String;)Ljava/io/OutputStream;", connector_open },
    { "javax/microedition/io/Connector", "openDataInputStream", "(Ljava/lang/String;)Ljava/io/DataInputStream;", connector_open },
    { "javax/microedition/io/Connector", "openDataOutputStream", "(Ljava/lang/String;)Ljava/io/DataOutputStream;", connector_open },

    /*
     * PrintStream.  The class row and the eleven natives that write to the
     * port log live in the core module; these are the members the reference
     * declares and the runtime did not resolve.  Registering a native for a
     * method a table does not declare is legal - the registry is a lookup
     * table, not a second declaration - so these bind the moment the row
     * exists, and are simply unused until then.
     */
    { "java/io/PrintStream", "<init>",     "(Ljava/io/OutputStream;)V", ps_init },
    { "java/io/PrintStream", "checkError", "()Z", ps_check_error },
    { "java/io/PrintStream", "setError",   "()V", ps_set_error },
    { "java/io/PrintStream", "write",      "([BII)V", ps_write_bytes_range },
    { "java/io/PrintStream", "print",      "(Z)V", ps_print_bool },
    { "java/io/PrintStream", "print",      "(J)V", ps_print_long },
    { "java/io/PrintStream", "print",      "(F)V", ps_print_float },
    { "java/io/PrintStream", "print",      "(D)V", ps_print_double },
    { "java/io/PrintStream", "print",      "([C)V", ps_print_chars },
    { "java/io/PrintStream", "print",      "(Ljava/lang/Object;)V", ps_print_obj },
    { "java/io/PrintStream", "println",    "(C)V", ps_println_char },
    { "java/io/PrintStream", "println",    "(J)V", ps_println_long },
    { "java/io/PrintStream", "println",    "(F)V", ps_println_float },
    { "java/io/PrintStream", "println",    "(D)V", ps_println_double },
    { "java/io/PrintStream", "println",    "([C)V", ps_println_chars }
};
const int nj_lib_io_count =
    (int)(sizeof(nj_lib_io_table) / sizeof(nj_lib_io_table[0]));
