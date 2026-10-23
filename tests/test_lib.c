/*
 * nano_j2me - tests/test_lib.c
 *
 * The class library's own test, driven through the C API the rest of the
 * runtime uses (nj_object_new / nj_call / nj_call_special / nj_array_*).  It
 * needs no fixture jar: every case is a java.io stream, and every case is one
 * that a real title from the corpus found broken.  That is the point of the
 * file - it is a regression net for a family of bug, not a coverage sweep.
 *
 *   1. Constructors - DataInputStream(InputStream) and
 *      DataOutputStream(OutputStream) ran ByteArrayOutputStream's constructor.
 *      That one reads its argument slot as a buffer capacity, and for a stream
 *      constructor the slot holds a REFERENCE, so the capacity became the low
 *      half of a heap pointer.  The title that found it alternated between
 *      OutOfMemoryError and "read() is not implemented by this InputStream"
 *      from run to run, depending on where the pointer landed.
 *   2. readFully(byte[]) - declared by DataInput but absent from the native
 *      table, so the call raised NoSuchMethodError.  There is no other form to
 *      fall back on: the interface requires both.
 *   3. skipBytes(int) - was bound to InputStream.available(), so it returned 0
 *      and skipped nothing.
 *   4. skip(long) - InputStream.skip has to go through read(byte[],int,int).
 *      A stream is free to implement only that form, and the single-byte form
 *      would then land on the abstract read() and throw.
 *   5. Round trip - DataOutputStream -> ByteArrayOutputStream ->
 *      ByteArrayInputStream -> DataInputStream returns what went in.
 *   6. Static field access - reading a static field is an active use of the
 *      class that declares it, so `getstatic` must run its <clinit> first.
 *      A title read an interface's static array before that initialiser had
 *      run and died in `iaload` with a NullPointerException on a null array.
 *   8. The argument window - a native that reads an argument its method does
 *      not have reads the caller's operand stack.  ByteArrayOutputStream()'s
 *      constructor did that, and the stale word was a heap address, so the
 *      buffer it allocated was as big as the address and the title died with
 *      OutOfMemoryError in <clinit> on whichever builds landed there.
 *   7. Record store overloads - the corpus opens its save file with MIDP
 *      2.0's (name, create, authmode, writable) and with MIDP 1.0's (name,
 *      vendor, suite); both were missing, and eight titles died in startApp
 *      before they had read a record.
 *
 * Usage:  test_lib
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

#include "nj_internal.h"
#include "nj_midi.h" /* nj_class_load: the fixtures below are raw classes */
#include "nj_midp.h"     /* nj_midp_image_pixels: assert on an Image's store */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Assertion machinery                                                 */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;

#define OK(cond)                                                              \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define OK_MSG(cond, ...)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);   \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
#define BAOS    "java/io/ByteArrayOutputStream"
#define BAIS    "java/io/ByteArrayInputStream"
#define DIS     "java/io/DataInputStream"
#define DOS     "java/io/DataOutputStream"
#define ISTREAM "java/io/InputStream"

static nj_value vref(nj_ref r)
{
    nj_value v;
    v.bits = 0u;
    v.r = r;
    return v;
}

static nj_value vint(nj_s4 i)
{
    nj_value v;
    v.bits = 0u;
    v.i = i;
    return v;
}

static nj_value vlong(nj_s8 j)
{
    nj_value v;
    v.bits = 0u;
    v.j = j;
    return v;
}

/* Assert that the VM came out of a step with no exception pending.  A pending
 * exception is not cleared by the call itself, so this both checks and tidies.
 */
static void clean(nj_vm *vm, const char *where)
{
    if (nj_has_exception(vm)) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: unexpected exception: %s\n", where,
                d ? d : "(no description)");
        nj_clear_exception(vm);
    } else {
        g_pass++;
    }
}

/* Allocate `cls` and run its constructor.  Returns NULL and reports on
 * failure, so a caller can hand the result straight to a later assertion. */
static nj_ref construct(nj_vm *vm, const char *cls, const char *desc,
                        nj_value *args, const char *where)
{
    nj_ref o = nj_object_new(vm, cls);

    if (!o) {
        g_fail++;
        fprintf(stderr, "FAIL %s: cannot allocate %s\n", where, cls);
        return NULL;
    }
    if (nj_call_special(vm, o, cls, "<init>", desc, args, NULL) != NJ_OK) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: %s%s threw: %s\n", where, cls, desc,
                d ? d : "(no description)");
        nj_clear_exception(vm);
        return NULL;
    }
    g_pass++;
    return o;
}

/* A ByteArrayInputStream over a fresh byte[] holding `data`. */
static nj_ref bais_over(nj_vm *vm, const unsigned char *data, nj_u4 len)
{
    nj_ref arr = nj_byte_array_new(vm, len);
    nj_value a[2];
    nj_u4 i;

    if (!arr) {
        g_fail++;
        return NULL;
    }
    for (i = 0u; i < len; i++) {
        nj_array_set_int(arr, i, (nj_s4)data[i]);
    }
    a[0] = vref(arr);
    return construct(vm, BAIS, "([B)V", a, "ByteArrayInputStream(byte[])");
}

/* ByteArrayInputStream(byte[], int, int) is a window: reads stop at its end,
 * reset() returns to the window start, and a window that does not fit is
 * rejected the way the JDK rejects it.  The old code computed count as
 * offset + length without checking anything and left mark at 0, so a bad
 * window could read past the array and reset() could rewind outside it. */
static void test_bais_range(nj_vm *vm)
{
    static const unsigned char data[8] = { 10, 11, 12, 13, 14, 15, 16, 17 };
    nj_ref arr = nj_byte_array_new(vm, 8u);
    nj_ref stream;
    nj_value a[3];
    nj_value ret;
    unsigned i;

    OK(arr != NULL);
    if (!arr) { return; }
    for (i = 0u; i < 8u; i++) {
        nj_array_set_int(arr, i, (nj_s4)data[i]);
    }

    a[0] = vref(arr);
    a[1] = vint(2);
    a[2] = vint(3);
    stream = construct(vm, BAIS, "([BII)V", a, "ByteArrayInputStream(byte[],int,int)");
    OK(stream != NULL);
    if (!stream) { return; }
    {
        nj_value mark_arg[1];

        /* available() reports the room left inside the window. */
        OK(nj_call(vm, stream, BAIS, "available", "()I", NULL, &ret) == NJ_OK && ret.i == 3);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 12);
        OK(nj_call(vm, stream, BAIS, "available", "()I", NULL, &ret) == NJ_OK && ret.i == 2);
        mark_arg[0] = vint(0);
        OK(nj_call(vm, stream, BAIS, "mark", "(I)V", mark_arg, NULL) == NJ_OK);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 13);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 14);
        OK(nj_call(vm, stream, BAIS, "reset", "()V", NULL, NULL) == NJ_OK);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 13);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 14);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == -1);
    }

    /* reset() without a mark returns to the window's own start, not to the
     * start of the underlying array: the mark position of a ranged stream is
     * its offset. */
    a[0] = vref(arr);
    a[1] = vint(2);
    a[2] = vint(3);
    stream = construct(vm, BAIS, "([BII)V", a, "ByteArrayInputStream(byte[],int,int)");
    OK(stream != NULL);
    if (stream) {
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 12);
        OK(nj_call(vm, stream, BAIS, "reset", "()V", NULL, NULL) == NJ_OK);
        OK(nj_call(vm, stream, BAIS, "read", "()I", NULL, &ret) == NJ_OK && ret.i == 12);
    }

    /* The window must fit: the JDK throws IndexOutOfBoundsException. */
    {
        static const struct { nj_s4 off; nj_s4 len; } bad[] = {
            { -1, 1 }, { 0, -1 }, { 8, 1 }, { 7, 2 }
        };
        unsigned k;

        for (k = 0u; k < sizeof(bad) / sizeof(bad[0]); ++k) {
            nj_ref o = nj_object_new(vm, BAIS);
            nj_status st;

            a[0] = vref(arr);
            a[1] = vint(bad[k].off);
            a[2] = vint(bad[k].len);
            OK(o != NULL);
            if (!o) { continue; }
            st = nj_call_special(vm, o, BAIS, "<init>", "([BII)V", a, NULL);
            OK_MSG(st != NJ_OK && nj_take_exception(vm) != 0,
                   "window (%d,%d) must be rejected",
                   (int)bad[k].off, (int)bad[k].len);
            nj_clear_exception(vm);
        }
    }
    /* The arrays above are garbage now; collect them so a later heap-pressure
     * test in this file does not see them as live bytes. */
    nj_gc_collect(vm);
}

/* A DataInputStream over an existing stream. */
static nj_ref dis_over(nj_vm *vm, nj_ref in)
{
    nj_value a[2];

    a[0] = vref(in);
    return construct(vm, DIS, "(Ljava/io/InputStream;)V", a,
                     "DataInputStream(InputStream)");
}

/* A missing JAR resource can be wrapped in DataInputStream.  Primitive
 * reads must report the null stream, not try a static InputStream.read(). */
static void test_null_data_input(nj_vm *vm)
{
    static const struct { const char *name; const char *desc; } methods[] = {
        { "readUTF", "()Ljava/lang/String;" },
        { "readInt", "()I" },
        { "readUnsignedShort", "()I" }
    };
    nj_ref dis = dis_over(vm, NULL);
    nj_value ret;
    unsigned i;

    OK(dis != NULL);
    if (!dis) { return; }
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        const char *message;
        OK(nj_call(vm, dis, DIS, methods[i].name, methods[i].desc,
                   NULL, &ret) != NJ_OK);
        message = nj_exception_describe(vm);
        OK_MSG(message && strstr(message, "NullPointerException"),
               "%s on a null stream raises NullPointerException (got %s)",
               methods[i].name, message ? message : "no exception");
        nj_clear_exception(vm);
    }
}

/* ------------------------------------------------------------------ */
/* 1 + 5: round trip through the whole stack                           */
/* ------------------------------------------------------------------ */
static void test_round_trip(nj_vm *vm)
{
    static const unsigned char expect[11] = {
        0x01, 0x02, 0x03, 0x04,   /* writeInt(0x01020304)   */
        0x7F,                     /* writeByte(0x7F)        */
        0x00, 0x02, 'h', 'i',     /* writeUTF("hi")         */
        0x01, 0x02                /* writeShort(0x0102)     */
    };
    const unsigned char *raw;
    nj_value a[3];
    nj_value ret;
    nj_ref baos, dos, bytes, bais, dis;
    nj_s4 n;
    nj_u4 i;

    baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    OK(baos != NULL);

    a[0] = vref(baos);
    dos = construct(vm, DOS, "(Ljava/io/OutputStream;)V", a,
                    "DataOutputStream(OutputStream)");
    OK(dos != NULL);
    clean(vm, "new DataOutputStream(baos)");

    a[0] = vint(0x01020304);
    OK(nj_call(vm, dos, DOS, "writeInt", "(I)V", a, NULL) == NJ_OK);
    a[0] = vint(0x7F);
    OK(nj_call(vm, dos, DOS, "writeByte", "(I)V", a, NULL) == NJ_OK);
    a[0] = vref(nj_string_new(vm, "hi"));
    OK(nj_call(vm, dos, DOS, "writeUTF", "(Ljava/lang/String;)V", a, NULL) == NJ_OK);
    a[0] = vint(0x0102);
    OK(nj_call(vm, dos, DOS, "writeShort", "(I)V", a, NULL) == NJ_OK);
    OK(nj_call(vm, dos, DOS, "flush", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "DataOutputStream writes");

    n = nj_call_int(vm, baos, BAOS, "size", "()I", NULL);
    OK_MSG(n == 11, "size() = %d, expected 11", (int)n);
    clean(vm, "ByteArrayOutputStream.size()");

    bytes = nj_call_ref(vm, baos, BAOS, "toByteArray", "()[B", NULL);
    OK(bytes != NULL);
    if (bytes) {
        OK(nj_array_length(bytes) == 11u);
        raw = (const unsigned char *)nj_array_data(bytes);
        for (i = 0u; i < 11u && raw; i++) {
            OK_MSG(raw[i] == expect[i], "byte %u = 0x%02X, expected 0x%02X",
                   i, raw[i], expect[i]);
        }
    }
    clean(vm, "toByteArray()");

    a[0] = vref(bytes);
    bais = construct(vm, BAIS, "([B)V", a, "ByteArrayInputStream(byte[])");
    OK(bais != NULL);
    dis = dis_over(vm, bais);
    OK(dis != NULL);

    /* The constructor above is the regression: with the wrong binding this
     * either raised OutOfMemoryError or left `in` unset, so every read below
     * failed instead of returning a value. */
    OK(nj_call_int(vm, dis, DIS, "readInt", "()I", NULL) == 0x01020304);
    OK(nj_call_int(vm, dis, DIS, "readByte", "()B", NULL) == 0x7F);

    ret.bits = 0u;
    OK(nj_call(vm, dis, DIS, "readUTF", "()Ljava/lang/String;", NULL,
               &ret) == NJ_OK);
    OK(ret.r != NULL && nj_string_equals_utf8(ret.r, "hi"));

    OK(nj_call_int(vm, dis, DIS, "readShort", "()S", NULL) == 0x0102);
    OK_MSG(nj_call_int(vm, dis, DIS, "read", "()I", NULL) == -1,
           "read() past the end must return -1");

    /*
     * writeFloat and writeDouble.  Both were missing, and 3D跳台滑雪2011_4507
     * died with NoSuchMethodError in its save routine because of it.  The
     * encoding is IEEE 754 big-endian, which the round trip through the reader
     * pins down: a wrong byte order still writes the right number of bytes,
     * but reading them back does not give the same number.
     */
    {
        nj_ref b2 = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
        nj_ref d2, i2, r2;
        nj_value q[2];
        OK(b2 != NULL);
        q[0] = vref(b2);
        d2 = construct(vm, DOS, "(Ljava/io/OutputStream;)V", q,
                       "DataOutputStream(baos)");
        OK(d2 != NULL);
        clean(vm, "new DataOutputStream for floats");

        {   /* vfloat()/vdouble() are defined further down this file */
            nj_value t;
            t.bits = 0u;
            t.f = 1.5f;
            q[0] = t;
        }
        q[1].bits = 0u;
        OK(nj_call(vm, d2, DOS, "writeFloat", "(F)V", q, NULL) == NJ_OK);
        {
            nj_value t;
            t.bits = 0u;
            t.d = -2.25;
            q[0] = t;
        }
        q[1].bits = 0u;            /* (D)V occupies two slots */
        OK(nj_call(vm, d2, DOS, "writeDouble", "(D)V", q, NULL) == NJ_OK);
        clean(vm, "writeFloat/writeDouble");

        n = nj_call_int(vm, b2, BAOS, "size", "()I", NULL);
        OK_MSG(n == 12, "writeFloat+writeDouble wrote %d bytes, expected 12",
               (int)n);

        q[0] = vref(nj_call_ref(vm, b2, BAOS, "toByteArray", "()[B", NULL));
        i2 = construct(vm, BAIS, "([B)V", q, "ByteArrayInputStream(byte[])");
        OK(i2 != NULL);
        r2 = dis_over(vm, i2);
        OK(r2 != NULL);
        if (r2) {
            OK(nj_call(vm, r2, DIS, "readFloat", "()F", NULL, &ret) == NJ_OK);
            OK_MSG(ret.f == 1.5f, "readFloat gave %g, expected 1.5", (double)ret.f);
            OK(nj_call(vm, r2, DIS, "readDouble", "()D", NULL, &ret) == NJ_OK);
            OK_MSG(ret.d == -2.25, "readDouble gave %g, expected -2.25", ret.d);
        }
        clean(vm, "float/double round trip");
    }
    /* Sprite links use 0xff as -1.  Returning 255 makes Metal Slug 4
     * recurse into a nonexistent sprite; unsigned reads still return 255. */
    {
        unsigned char values[] = { 0x7f, 0x80, 0xff, 0x7f, 0x80, 0xff };
        nj_ref stream = dis_over(vm, bais_over(vm, values, sizeof(values)));
        OK(nj_call_int(vm, stream, DIS, "readByte", "()B", NULL) == 127);
        OK(nj_call_int(vm, stream, DIS, "readByte", "()B", NULL) == -128);
        OK(nj_call_int(vm, stream, DIS, "readByte", "()B", NULL) == -1);
        OK(nj_call_int(vm, stream, DIS, "readUnsignedByte", "()I", NULL) == 127);
        OK(nj_call_int(vm, stream, DIS, "readUnsignedByte", "()I", NULL) == 128);
        OK(nj_call_int(vm, stream, DIS, "readUnsignedByte", "()I", NULL) == 255);
        OK(nj_call(vm, stream, DIS, "readByte", "()B", NULL, &ret) != NJ_OK);
        OK(nj_has_exception(vm));
        nj_clear_exception(vm);
        OK(nj_call(vm, stream, DIS, "readUnsignedByte", "()I", NULL, &ret) != NJ_OK);
        OK(nj_has_exception(vm));
        nj_clear_exception(vm);
    }
    clean(vm, "DataInputStream reads");
}

/* ------------------------------------------------------------------ */
/* 2: the static DataInputStream.readUTF(DataInput)                    */
/* ------------------------------------------------------------------ */
static void test_read_utf_static(nj_vm *vm)
{
    /* writeUTF's encoding: a two-byte big-endian length, then the bytes.
     * "hi" is two bytes, and U+4E2D is three (E4 B8 AD). */
    static const unsigned char data[9] = {
        0x00, 0x02, 'h', 'i',
        0x00, 0x03, 0xE4, 0xB8, 0xAD
    };
    nj_value a[3];
    nj_value ret;
    nj_ref dis;

    /* A game calls this form to decode a length-prefixed string out of a
     * stream it already holds. Two things have to be right: the linker must
     * find the static row, and the native behind it must read the ARGUMENT
     * rather than the receiver. */
    dis = dis_over(vm, bais_over(vm, data, (nj_u4)sizeof(data)));
    OK(dis != NULL);

    a[0] = vref(dis);
    a[1].bits = 0u;
    ret.bits = 0u;
    OK(nj_call_static(vm, DIS, "readUTF",
                      "(Ljava/io/DataInput;)Ljava/lang/String;", a,
                      &ret) == NJ_OK);
    OK_MSG(ret.r != NULL && nj_string_equals_utf8(ret.r, "hi"),
           "the static readUTF returned the first string");

    a[0] = vref(dis);
    ret.bits = 0u;
    OK(nj_call_static(vm, DIS, "readUTF",
                      "(Ljava/io/DataInput;)Ljava/lang/String;", a,
                      &ret) == NJ_OK);
    {
        const nj_u2 *ch = ret.r ? nj_string_chars(ret.r) : NULL;

        OK_MSG(ch != NULL && nj_string_length(ret.r) == 1u &&
               ch[0] == 0x4E2Du,
               "and three UTF-8 bytes decoded to U+4E2D, got %u chars",
               ret.r ? (unsigned)nj_string_length(ret.r) : 0u);
    }
    clean(vm, "static readUTF");
}

/* ------------------------------------------------------------------ */
/* 2: readFully(byte[])                                                */
/* ------------------------------------------------------------------ */
static void test_read_fully(nj_vm *vm)
{
    static const unsigned char data[4] = { 11, 22, 33, 44 };
    nj_value a[3];
    nj_ref bais, dis, dst;
    nj_u4 i;

    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    dis = dis_over(vm, bais);
    OK(dis != NULL);

    dst = nj_byte_array_new(vm, 4u);
    OK(dst != NULL);
    a[0] = vref(dst);
    OK(nj_call(vm, dis, DIS, "readFully", "([B)V", a, NULL) == NJ_OK);
    clean(vm, "readFully([B)V");
    for (i = 0u; i < 4u; i++) {
        OK_MSG(nj_array_get_int(dst, i) == (nj_s4)data[i],
               "dst[%u] = %d, expected %d", i, (int)nj_array_get_int(dst, i),
               (int)data[i]);
    }

    /* The one-argument form shares the range form's validation, so a bad range
     * must still be rejected rather than silently read. */
    a[0] = vref(dst);
    a[1] = vint(0);
    a[2] = vint(-1);
    OK(nj_call(vm, dis, DIS, "readFully", "([BII)V", a, NULL) != NJ_OK);
    OK(nj_has_exception(vm));
    nj_clear_exception(vm);

    /* Running out of stream is the checked EOFException, not a short read:
     * the four bytes are gone, so asking for six must raise EOFException and
     * leave the partial bytes in place. */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    dis = dis_over(vm, bais);
    OK(dis != NULL);
    dst = nj_byte_array_new(vm, 6u);
    OK(dst != NULL);
    a[0] = vref(dst);
    OK(nj_call(vm, dis, DIS, "readFully", "([B)V", a, NULL) != NJ_OK);
    {
        const char *d = nj_exception_describe(vm);

        OK_MSG(d != NULL && strstr(d, "EOFException") != NULL,
               "readFully past the end raises EOFException: %s",
               d ? d : "(no exception)");
    }
    nj_clear_exception(vm);
    OK(nj_array_get_int(dst, 0u) == 11 && nj_array_get_int(dst, 3u) == 44);
    clean(vm, "readFully past the end");
}

/* ------------------------------------------------------------------ */
/* 3 + 4: skipBytes(int) and InputStream.skip(long)                    */
/* ------------------------------------------------------------------ */
static void test_skips(nj_vm *vm)
{
    static const unsigned char data[4] = { 10, 20, 30, 40 };
    nj_value a[3];
    nj_value ret;
    nj_ref bais, dis;

    /* skipBytes must consume the bytes, not just report how many are
     * available without consuming them. */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    dis = dis_over(vm, bais);
    OK(dis != NULL);

    a[0] = vint(2);
    OK_MSG(nj_call_int(vm, dis, DIS, "skipBytes", "(I)I", a) == 2,
           "skipBytes(2) must return 2");
    OK_MSG(nj_call_int(vm, dis, DIS, "read", "()I", NULL) == 30,
           "the byte after a 2-byte skip must be the third one");
    clean(vm, "skipBytes");

    /*
     * ... and a skip that runs past the end is a SHORT COUNT, not an
     * exception: "This method never throws an EOFException.  The actual
     * number of bytes skipped is returned." (CLDC 1.1 DataInput).  It threw
     * one, and 实况足球经理2009_10609's loader died in the middle of building
     * its string table because of it.
     */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    dis = dis_over(vm, bais);
    OK(dis != NULL);
    a[0] = vint(100);
    OK_MSG(nj_call_int(vm, dis, DIS, "skipBytes", "(I)I", a) == 4,
           "skipBytes past the end must report the 4 bytes it could skip");
    OK_MSG(!vm->exception, "and must not raise anything");
    OK_MSG(nj_call_int(vm, dis, DIS, "skipBytes", "(I)I", a) == 0,
           "and a skip at the end of the stream is 0");
    clean(vm, "skipBytes past the end");

    /* InputStream.skip, on the inherited implementation (nj_call_special
     * bypasses ByteArrayInputStream's own override, which is what a subclass
     * providing only read(byte[],int,int) would inherit). */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    a[0] = vlong(2);
    a[1].bits = 0u;
    ret.bits = 0u;
    OK(nj_call_special(vm, bais, ISTREAM, "skip", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 2, "InputStream.skip(2) = %lld, expected 2",
           (long long)ret.j);
    OK_MSG(nj_call_int(vm, bais, ISTREAM, "read", "()I", NULL) == 30,
           "the byte after a 2-byte skip must be the third one");
    clean(vm, "InputStream.skip");

    /* Skipping more than the stream holds reports what it could skip. */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    a[0] = vlong(100);
    a[1].bits = 0u;
    ret.bits = 0u;
    OK(nj_call_special(vm, bais, ISTREAM, "skip", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 4, "skip(100) on 4 bytes = %lld, expected 4",
           (long long)ret.j);
    clean(vm, "InputStream.skip past the end");
}

/* ------------------------------------------------------------------ */
/* 5: DataOutputStream.writeChars(String)                               */
/* ------------------------------------------------------------------ */
static void test_write_chars(nj_vm *vm)
{
    static const nj_u2 units[3] = { 0x0048u, 0x0069u, 0x00E9u };  /* H i é */
    nj_ref baos, dos, bais, dis, bytes;
    nj_value a[2];
    nj_s4 n;

    baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    OK(baos != NULL);
    a[0] = vref(baos);
    dos = construct(vm, DOS, "(Ljava/io/OutputStream;)V", a,
                    "DataOutputStream(baos)");
    OK(dos != NULL);
    clean(vm, "new DataOutputStream for writeChars");

    /*
     * CLDC 1.1: each character is one 16-bit unit, high byte first - no length
     * prefix and no modified UTF-8, which is what writeUTF does.  灵异村庄
     * _11169 builds its save record with this method; it used to be missing
     * entirely, so the call resolved up to the DataOutput interface and threw
     * NoSuchMethodError inside the title's own helper.
     */
    a[0] = vref(nj_string_new_chars(vm, units, 3));
    OK(a[0].r != NULL);
    OK(nj_call(vm, dos, DOS, "writeChars", "(Ljava/lang/String;)V", a, NULL)
       == NJ_OK);
    clean(vm, "writeChars");

    n = nj_call_int(vm, baos, BAOS, "size", "()I", NULL);
    OK_MSG(n == 6, "three characters wrote %d bytes, expected 6", (int)n);

    bytes = nj_call_ref(vm, baos, BAOS, "toByteArray", "()[B", NULL);
    OK(bytes != NULL);
    a[0] = vref(bytes);
    bais = construct(vm, BAIS, "([B)V", a, "ByteArrayInputStream(bytes)");
    OK(bais != NULL);
    a[0] = vref(bais);
    dis = construct(vm, DIS, "(Ljava/io/InputStream;)V", a,
                    "DataInputStream(bais)");
    OK(dis != NULL);
    clean(vm, "new DataInputStream for writeChars");
    /* readChar is declared "()C", so the lookup has to name that descriptor:
     * asking for "()I" is a NoSuchMethodError, which is what the first version
     * of this test proved by accident. */
    OK_MSG(nj_call_int(vm, dis, DIS, "readChar", "()C", NULL) == 0x0048,
           "the first unit must read back as H");
    OK_MSG(nj_call_int(vm, dis, DIS, "readChar", "()C", NULL) == 0x0069,
           "the second unit must read back as i");
    OK_MSG(nj_call_int(vm, dis, DIS, "readChar", "()C", NULL) == 0x00E9,
           "the third unit must read back as e-acute, not as modified UTF-8");
    OK_MSG(nj_call_int(vm, dis, DIS, "read", "()I", NULL) == -1,
           "and the stream must be at its end after three units");
    clean(vm, "writeChars round trip");
}

/* ------------------------------------------------------------------ */
/* 6: media - VolumeControl.setLevel and Player.setMediaTime            */
/* ------------------------------------------------------------------ */
#define MEDIA_MGR "javax/microedition/media/Manager"
#define PLAYER    "javax/microedition/media/Player"
#define VOLUME    "javax/microedition/media/control/VolumeControl"

/*
 * Both of these were advertised with the wrong shape: setLevel was declared
 * void where the spec returns the level it settled on, and setMediaTime did
 * not exist at all.  The corpus found them the honest way - 28 titles call
 * setLevel(I)I and 11 call setMediaTime(J)J - as a NoSuchMethodError on the
 * first media call, which a game usually catches and then plays on in silence.
 */
static void test_media(nj_vm *vm)
{
    nj_value a[2];
    nj_value ret;
    nj_ref player, ctl, ctl2, uri;

    uri = nj_string_new(vm, "tone://");
    OK_MSG(uri != NULL, "a tone locator can be built");
    a[0] = vref(uri);
    ret.bits = 0u;
    if (nj_call_static(vm, MEDIA_MGR, "createPlayer",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Player;",
                       a, &ret) != NJ_OK) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL Manager.createPlayer(tone://) threw: %s\n",
                d ? d : "(no description)");
        nj_clear_exception(vm);
        return;
    }
    g_pass++;
    player = ret.r;
    OK(player != NULL);

    a[0] = vlong(5000);
    a[1].bits = 0u;
    ret.bits = 0u;
    OK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 5000, "Player.setMediaTime returns the time it set, got %lld",
           (long long)ret.j);

    a[0] = vlong(-7);
    a[1].bits = 0u;
    ret.bits = 0u;
    OK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 0, "and clamps a negative seek to 0, got %lld",
           (long long)ret.j);

    a[0] = vref(nj_string_new(vm, "VolumeControl"));
    ret.bits = 0u;
    OK(nj_call(vm, player, PLAYER, "getControl",
               "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
               a, &ret) == NJ_OK);
    ctl = ret.r;
    OK_MSG(ctl != NULL, "the tone player offers a VolumeControl");
    if (!ctl) {
        return;
    }

    a[0] = vint(60);
    ret.bits = 0u;
    OK(nj_call(vm, ctl, VOLUME, "setLevel", "(I)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 60, "VolumeControl.setLevel returns the level it set, "
           "got %d", ret.i);
    OK_MSG(nj_call_int(vm, ctl, VOLUME, "getLevel", "()I", NULL) == 60,
           "and the level stuck");

    a[0] = vint(101);
    ret.bits = 0u;
    if (nj_call(vm, ctl, VOLUME, "setLevel", "(I)I", a, &ret) == NJ_OK) {
        g_fail++;
        fprintf(stderr, "FAIL setLevel(101) was accepted\n");
    } else {
        g_pass++;
        nj_clear_exception(vm);
    }

    /*
     * Controllable and Control have to exist even though the Player is what
     * answers: a title writes `((Controllable) player).getControl(...)`, and
     * some call the interface method straight on the Player.  Resolving that
     * reference is what used to raise NoClassDefFoundError, before any cast.
     */
    OK_MSG(nj_class_lookup(vm, "javax/microedition/media/Controllable") != NULL,
           "javax/microedition/media/Controllable exists");
    OK_MSG(nj_class_lookup(vm, "javax/microedition/media/Control") != NULL,
           "javax/microedition/media/Control exists");
    a[0] = vref(nj_string_new(vm, "VolumeControl"));
    ret.bits = 0u;
    OK_MSG(nj_call(vm, player, "javax/microedition/media/Controllable",
                   "getControl",
                   "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                   a, &ret) == NJ_OK && ret.r != NULL,
           "the Player answers Controllable.getControl");
    /*
     * The other spelling.  getControl takes the fully qualified class name or
     * the bare one, and 开心农场_240 asks with the long form: the match against
     * the bare name returned NULL, the game's checkcast passed on that null
     * (the JVM spec says it must) and its setLevel threw NullPointerException
     * at g.b(I)Z pc=65.  A unit test that only ever wrote "VolumeControl"
     * could not have found this; the corpus did.
     */
    a[0] = vref(nj_string_new(vm,
               "javax.microedition.media.control.VolumeControl"));
    ret.bits = 0u;
    OK_MSG(nj_call(vm, player, PLAYER, "getControl",
                   "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                   a, &ret) == NJ_OK && ret.r != NULL,
           "the fully qualified control name finds the control");
    ctl2 = ret.r;
    if (ctl2 != NULL) {
        a[0] = vint(40);
        ret.bits = 0u;
        OK(nj_call(vm, ctl2, VOLUME, "setLevel", "(I)I", a, &ret) == NJ_OK);
        OK_MSG(ret.i == 40, "and it drives the same player, got %d", ret.i);
    }
    a[0] = vref(nj_string_new(vm,
               "javax.microedition.media.control.ToneControl"));
    ret.bits = 0u;
    OK_MSG(nj_call(vm, player, PLAYER, "getControl",
                   "(Ljava/lang/String;)Ljavax/microedition/media/Control;",
                   a, &ret) == NJ_OK && ret.r != NULL,
           "and so does the fully qualified tone control name");
    clean(vm, "Controllable");
    clean(vm, "media controls");
}

/* ------------------------------------------------------------------ */
/* 7: String - the ranged, encoded and StringBuffer constructors        */
/* ------------------------------------------------------------------ */
#define STRING "java/lang/String"
#define SBUF   "java/lang/StringBuffer"

/* A byte[] from a C array of bytes. */
static nj_ref bytes_of(nj_vm *vm, const unsigned char *data, nj_u4 len)
{
    nj_ref arr = nj_byte_array_new(vm, len);
    nj_u4 i;

    if (!arr) {
        g_fail++;
        return NULL;
    }
    for (i = 0u; i < len; i++) {
        nj_array_set_int(arr, i, (nj_s4)data[i]);
    }
    return arr;
}

/*
 * String(byte[], int, int) and String(byte[], int, int, String) were missing
 * outright - 11 and 6 corpus titles call them, and a missing constructor is a
 * NoSuchMethodError on the first decode, i.e. before the game draws anything.
 * The decode itself is shared with String(byte[]) and String(byte[], String),
 * so this also pins the UTF-8 decoder: it must produce the character, not
 * refuse the bytes above ASCII, and a truncated sequence must cost one U+FFFD
 * rather than the rest of the array.
 */
static void test_strings(nj_vm *vm)
{
    static const unsigned char ascii[4] = { 'a', 'b', 'c', 'd' };
    static const unsigned char cjk[3] = { 0xE4, 0xB8, 0xAD };   /* U+4E2D 中 */
    static const unsigned char bad[2] = { 0xE4, 0x41 };         /* cut 3-byte + 'A' */
    nj_value a[4];
    nj_ref arr, s, sb, enc, chars, out;
    nj_u2 pair[2];

    /* new String(byte[], int, int) is a slice, not the whole array. */
    arr = bytes_of(vm, ascii, 4u);
    a[0] = vref(arr);
    a[1] = vint(1);
    a[2] = vint(2);
    s = construct(vm, STRING, "([BII)V", a, "String(byte[],1,2)");
    OK_MSG(s != NULL && nj_string_equals_utf8(s, "bc"),
           "new String({'a','b','c','d'},1,2) is \"bc\"");
    OK_MSG(s != NULL && nj_string_length(s) == 2u,
           "and is 2 chars, got %u", s ? (unsigned)nj_string_length(s) : 0u);

    /* A range that runs off the end is an index exception, not a truncation. */
    a[0] = vref(arr);
    a[1] = vint(3);
    a[2] = vint(5);
    sb = nj_object_new(vm, STRING);
    OK(sb != NULL);
    if (sb &&
        nj_call_special(vm, sb, STRING, "<init>", "([BII)V", a, NULL) == NJ_OK) {
        g_fail++;
        fprintf(stderr, "FAIL String(bytes,3,5) was accepted\n");
    } else if (sb) {
        const char *d = nj_exception_describe(vm);
        OK_MSG(d != NULL && strstr(d, "StringIndexOutOfBounds") != NULL,
               "String(bytes,3,5) throws an index exception, got %s",
               d ? d : "(nothing)");
        nj_clear_exception(vm);
    }

    /* The UTF-8 form of the ranged constructor, and the same bytes through the
     * single-argument decoder. */
    arr = bytes_of(vm, cjk, 3u);
    enc = nj_string_new(vm, "UTF-8");
    a[0] = vref(arr);
    a[1] = vint(0);
    a[2] = vint(3);
    a[3] = vref(enc);
    s = construct(vm, STRING, "([BIILjava/lang/String;)V", a,
                  "String(byte[],0,3,\"UTF-8\")");
    OK_MSG(s != NULL && nj_string_length(s) == 1u,
           "three UTF-8 bytes make one char, got %u",
           s ? (unsigned)nj_string_length(s) : 0u);
    OK_MSG(s != NULL && nj_string_data(s)[0] == 0x4E2Du,
           "and it is U+4E2D, got U+%04X",
           s ? (unsigned)nj_string_data(s)[0] : 0u);

    a[0] = vref(arr);
    a[1] = vref(enc);
    s = construct(vm, STRING, "([BLjava/lang/String;)V", a,
                  "String(byte[],\"UTF-8\")");
    /* nj_string_equals_utf8 is one byte per char, so CJK is checked by code
     * unit rather than through it. */
    OK_MSG(s != NULL && nj_string_length(s) == 1u &&
           nj_string_data(s)[0] == 0x4E2Du,
           "String(byte[],\"UTF-8\") decodes the same bytes to U+4E2D");

    /* A bad sequence costs one replacement char and keeps the next byte. */
    arr = bytes_of(vm, bad, 2u);
    a[0] = vref(arr);
    a[1] = vref(enc);
    s = construct(vm, STRING, "([BLjava/lang/String;)V", a,
                  "String({0xE4,'A'},\"UTF-8\")");
    OK_MSG(s != NULL && nj_string_length(s) == 2u,
           "a truncated sequence plus 'A' is 2 chars, got %u",
           s ? (unsigned)nj_string_length(s) : 0u);
    OK_MSG(s != NULL && nj_string_data(s)[0] == 0xFFFDu &&
           nj_string_data(s)[1] == (nj_u2)'A',
           "and reads U+FFFD then 'A'");

    /* The one-argument form is Latin-1, which is what the platform default
     * encoding means here: 0xE4 is U+00E4, not the start of a UTF-8 sequence. */
    a[0] = vref(arr);
    s = construct(vm, STRING, "([B)V", a, "String({0xE4,'A'})");
    OK_MSG(s != NULL && nj_string_length(s) == 2u &&
           nj_string_data(s)[0] == 0x00E4u && nj_string_data(s)[1] == 0x41u,
           "String(byte[]) stays one byte per char");

    /* getBytes(String) has to write what the read side reads. */
    pair[0] = 0x4E2Du;
    chars = nj_char_array_new(vm, 1u);
    OK(chars != NULL);
    if (chars) {
        nj_array_set_int(chars, 0u, 0x4E2D);
    }
    s = nj_string_new_chars(vm, pair, 1);
    OK(s != NULL);
    a[0] = vref(enc);
    out = nj_call_ref(vm, s, STRING, "getBytes", "(Ljava/lang/String;)[B", a);
    OK_MSG(out != NULL && nj_array_length(out) == 3u,
           "getBytes(\"UTF-8\") of U+4E2D is 3 bytes, got %u",
           out ? (unsigned)nj_array_length(out) : 0u);
    if (out && nj_array_length(out) == 3u) {
        const unsigned char *raw = (const unsigned char *)nj_array_data(out);
        OK_MSG(raw && raw[0] == 0xE4u && raw[1] == 0xB8u && raw[2] == 0xADu,
               "and they are E4 B8 AD");
    }
    clean(vm, "getBytes(String)");

    /* String.valueOf(char[]) and new String(StringBuffer) - the two shapes a
     * game uses to turn a buffer of characters back into a string. */
    pair[0] = (nj_u2)'h';
    pair[1] = (nj_u2)'i';
    a[0] = vref(nj_char_array_new(vm, 2u));
    OK(a[0].r != NULL);
    if (a[0].r) {
        nj_array_set_int(a[0].r, 0u, (nj_s4)'h');
        nj_array_set_int(a[0].r, 1u, (nj_s4)'i');
    }
    s = nj_call_ref(vm, NULL, STRING, "valueOf", "([C)Ljava/lang/String;", a);
    OK_MSG(s != NULL && nj_string_equals_utf8(s, "hi"),
           "String.valueOf({'h','i'}) is \"hi\"");
    clean(vm, "String.valueOf(char[])");

    a[0] = vref(nj_string_new(vm, "hi"));
    sb = construct(vm, SBUF, "(Ljava/lang/String;)V", a, "StringBuffer(\"hi\")");
    OK(sb != NULL);
    a[0] = vref(sb);
    s = construct(vm, STRING, "(Ljava/lang/StringBuffer;)V", a,
                  "String(StringBuffer)");
    OK_MSG(s != NULL && nj_string_equals_utf8(s, "hi"),
           "new String(StringBuffer(\"hi\")) is \"hi\"");

    /* lastIndexOf(int, int) walks back from fromIndex, and a negative start
     * finds nothing instead of scanning the whole string. */
    s = nj_string_new(vm, "abcabc");
    a[0] = vint((nj_s4)'a');
    a[1] = vint(5);
    OK_MSG(nj_call_int(vm, s, STRING, "lastIndexOf", "(II)I", a) == 3,
           "lastIndexOf('a',5) is 3");
    a[1] = vint(2);
    OK_MSG(nj_call_int(vm, s, STRING, "lastIndexOf", "(II)I", a) == 0,
           "lastIndexOf('a',2) is 0");
    a[1] = vint(-1);
    OK_MSG(nj_call_int(vm, s, STRING, "lastIndexOf", "(II)I", a) == -1,
           "lastIndexOf('a',-1) is -1");
    clean(vm, "String.lastIndexOf");
}

/* ------------------------------------------------------------------ */
/* Number wrappers, parsing, printing, and a double for the buffers    */
/* ------------------------------------------------------------------ */
#define INTEGER "java/lang/Integer"
#define LONGW   "java/lang/Long"
#define SHORTW  "java/lang/Short"
#define BYTEW   "java/lang/Byte"
#define CHARW   "java/lang/Character"
#define FLOATW  "java/lang/Float"
#define DOUBLEW "java/lang/Double"

static nj_value vfloat(float f)
{
    nj_value v;

    v.bits = 0u;
    v.f = f;
    return v;
}

static nj_value vdouble(double d)
{
    nj_value v;

    v.bits = 0u;
    v.d = d;
    return v;
}

/* Call a static that takes one String and returns a value, reporting a throw
 * through *threw (and clearing it, so the next case starts clean). */
static nj_value static_from_string(nj_vm *vm, const char *cls, const char *name,
                                   const char *desc, const char *text, int *threw)
{
    nj_value a[2];
    nj_value r;

    r.bits = 0u;
    a[0] = vref(nj_string_new(vm, text));
    *threw = 0;
    if (nj_call_static(vm, cls, name, desc, a, &r) != NJ_OK) {
        *threw = 1;
        nj_clear_exception(vm);
        r.bits = 0u;
    }
    return r;
}

/* Angry Birds' XMLLevel$LevelHandler.startElement parses XML attributes with
 * Float.parseFloat(String); the missing declaration stopped it at pc 592.
 * Check the returned bits so negative zero and subnormal rounding matter. */
static void test_float_parse(nj_vm *vm)
{
    static const struct { const char *text; nj_u4 bits; } values[] = {
        { "3.5", 0x40600000u }, { "-12.75", 0xC14C0000u },
        { "+.5", 0x3F000000u }, { "2.", 0x40000000u },
        { "1.25e2", 0x42FA0000u }, { "-125E-2", 0xBFA00000u },
        { "0.1", 0x3DCCCCCDu }, { "16777217", 0x4B800000u },
        { "0", 0u }, { "-0.0", 0x80000000u },
        { "-0e999999", 0x80000000u },
        { "\t\r\n +1.5f \037", 0x3FC00000u },
        { "2F", 0x40000000u }, { "3d", 0x40400000u },
        { "4D", 0x40800000u },
        { "Infinity", 0x7F800000u }, { "+Infinity", 0x7F800000u },
        { " -Infinity\n", 0xFF800000u },
        { "NaN", 0x7FC00000u }, { "+NaN", 0x7FC00000u },
        { "-NaN", 0x7FC00000u },
        { "3.4028235e38", 0x7F7FFFFFu },
        { "3.4028236e38", 0x7F800000u },
        { "-3.4028236e38", 0xFF800000u },
        { "1e999999", 0x7F800000u },
        { "-1e999999", 0xFF800000u },
        { "1.17549435e-38", 0x00800000u },
        { "1.4e-45", 1u }, { "7.1e-46", 1u },
        { "7e-46", 0u }, { "-7e-46", 0x80000000u },
        { "1e-999999", 0u }, { "-1e-999999", 0x80000000u }
    };
    static const char *invalid[] = {
        "", " \t\n", "+", "-", ".", "1e", "1e+", "1e-",
        "1.2.3", "1e2e3", "1 2", "1 f", "f", "1ff", "1dF",
        "1,5", "1x", "nan", "inf", "infinity", "NaNf", "InfinityD",
        "--1", "++1", "+-1", "1\302\240", "\302\2401"
    };
    nj_value a[2], r;
    unsigned i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        int threw;
        nj_u4 bits;
        r = static_from_string(vm, FLOATW, "parseFloat",
                               "(Ljava/lang/String;)F", values[i].text, &threw);
        a[0] = r;
        bits = 0u;
        if (!threw && nj_call_static(vm, FLOATW, "floatToIntBits", "(F)I",
                                     a, &r) == NJ_OK) {
            bits = (nj_u4)r.i;
        }
        OK_MSG(!threw && bits == values[i].bits,
               "Float.parseFloat(\"%s\") bits %08x, expected %08x",
               values[i].text, (unsigned)bits, (unsigned)values[i].bits);
        clean(vm, "Float.parseFloat valid literal");
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        const char *message;
        a[0] = vref(nj_string_new(vm, invalid[i]));
        OK(nj_call_static(vm, FLOATW, "parseFloat", "(Ljava/lang/String;)F",
                          a, &r) != NJ_OK);
        message = nj_exception_describe(vm);
        OK_MSG(message && strstr(message, "NumberFormatException"),
               "Float.parseFloat(\"%s\") raises NumberFormatException (got %s)",
               invalid[i], message ? message : "no exception");
        nj_clear_exception(vm);
    }
    /* trim() includes U+0000 but an embedded NUL must not terminate a parse. */
    {
        static const nj_u2 padded[] = { 0u, '1', '.', '5', 0x20u, 0u };
        static const nj_u2 embedded[] = { '1', 0u, '5' };
        const char *message;
        a[0] = vref(nj_string_new_chars(vm, padded, 6));
        OK(nj_call_static(vm, FLOATW, "parseFloat", "(Ljava/lang/String;)F",
                          a, &r) == NJ_OK && r.f == 1.5f);
        clean(vm, "Float.parseFloat trims NUL");
        a[0] = vref(nj_string_new_chars(vm, embedded, 3));
        OK(nj_call_static(vm, FLOATW, "parseFloat", "(Ljava/lang/String;)F",
                          a, &r) != NJ_OK);
        message = nj_exception_describe(vm);
        OK(message && strstr(message, "NumberFormatException"));
        nj_clear_exception(vm);
        a[0] = vref(NULL);
        OK(nj_call_static(vm, FLOATW, "parseFloat", "(Ljava/lang/String;)F",
                          a, &r) != NJ_OK);
        message = nj_exception_describe(vm);
        OK_MSG(message && strstr(message, "NullPointerException"),
               "Float.parseFloat(null) raises NullPointerException (got %s)",
               message ? message : "no exception");
        nj_clear_exception(vm);
    }
}

static void test_numbers(nj_vm *vm)
{
    nj_value a[2];
    nj_value r;
    nj_ref o;
    nj_ref sb;
    nj_ref s;
    int threw;

    /* parseInt used to accept "2147483648" and wrap it to INT_MIN: its
     * accumulator was compared against 2^32, not 2^31. */
    OK_MSG(static_from_string(vm, INTEGER, "parseInt",
                              "(Ljava/lang/String;)I", "2147483647",
                              &threw).i == 2147483647 && !threw,
           "parseInt(\"2147483647\") is the maximum");
    (void)static_from_string(vm, INTEGER, "parseInt",
                             "(Ljava/lang/String;)I", "2147483648", &threw);
    OK_MSG(threw, "parseInt(\"2147483648\") is out of range");
    OK_MSG(static_from_string(vm, INTEGER, "parseInt",
                              "(Ljava/lang/String;)I", "-2147483648",
                              &threw).i == (nj_s4)0x80000000 && !threw,
           "parseInt(\"-2147483648\") is the minimum");

    /* It is a parse, not a prefix scan. */
    (void)static_from_string(vm, INTEGER, "parseInt",
                             "(Ljava/lang/String;)I", " 12", &threw);
    OK_MSG(threw, "parseInt(\" 12\") is not a number");
    (void)static_from_string(vm, INTEGER, "parseInt",
                             "(Ljava/lang/String;)I", "12x", &threw);
    OK_MSG(threw, "parseInt(\"12x\") is not a number");
    (void)static_from_string(vm, INTEGER, "parseInt",
                             "(Ljava/lang/String;)I", "", &threw);
    OK_MSG(threw, "parseInt(\"\") is not a number");

    a[0] = vref(nj_string_new(vm, "-2A"));
    a[1] = vint(16);
    r.bits = 0u;
    OK_MSG(nj_call_static(vm, INTEGER, "parseInt", "(Ljava/lang/String;I)I",
                          a, &r) == NJ_OK && r.i == -42,
           "parseInt(\"-2A\",16) is -42");
    a[0] = vref(nj_string_new(vm, "z"));
    a[1] = vint(36);
    OK_MSG(nj_call_static(vm, INTEGER, "parseInt", "(Ljava/lang/String;I)I",
                          a, &r) == NJ_OK && r.i == 35,
           "parseInt(\"z\",36) is 35");
    a[0] = vref(nj_string_new(vm, "2"));
    a[1] = vint(2);
    (void)nj_call_static(vm, INTEGER, "parseInt", "(Ljava/lang/String;I)I",
                         a, &r);
    OK_MSG(nj_has_exception(vm), "parseInt(\"2\",2) is not a base-2 digit");
    nj_clear_exception(vm);

    /* byteValue is a truncation to the low 8 bits, sign-extended. */
    a[0] = vint(0x1FF);
    o = construct(vm, INTEGER, "(I)V", a, "Integer(511)");
    OK_MSG(o != NULL && nj_call_int(vm, o, INTEGER, "byteValue", "()B",
                                    NULL) == -1,
           "new Integer(511).byteValue() is -1");

    a[0] = vref(nj_string_new(vm, "42"));
    o = nj_call_ref(vm, NULL, INTEGER, "valueOf",
                    "(Ljava/lang/String;)Ljava/lang/Integer;", a);
    OK_MSG(o != NULL && nj_call_int(vm, o, INTEGER, "intValue", "()I",
                                    NULL) == 42,
           "Integer.valueOf(\"42\").intValue() is 42");
    a[0] = vref(nj_string_new(vm, "4x"));
    o = nj_call_ref(vm, NULL, INTEGER, "valueOf",
                    "(Ljava/lang/String;)Ljava/lang/Integer;", a);
    OK_MSG(o == NULL && nj_has_exception(vm),
           "Integer.valueOf(\"4x\") throws");
    nj_clear_exception(vm);

    /* The 64-bit parse has to hold the whole range, in both directions. */
    OK_MSG(static_from_string(vm, LONGW, "parseLong", "(Ljava/lang/String;)J",
                              "9223372036854775807",
                              &threw).j == 0x7FFFFFFFFFFFFFFFLL && !threw,
           "parseLong(\"9223372036854775807\") is the maximum");
    (void)static_from_string(vm, LONGW, "parseLong", "(Ljava/lang/String;)J",
                             "9223372036854775808", &threw);
    OK_MSG(threw, "parseLong(\"9223372036854775808\") is out of range");
    OK_MSG(static_from_string(vm, LONGW, "parseLong", "(Ljava/lang/String;)J",
                              "-9223372036854775808",
                              &threw).j == -0x7FFFFFFFFFFFFFFFLL - 1 && !threw,
           "parseLong(\"-9223372036854775808\") is the minimum");
    a[0] = vref(nj_string_new(vm, "ff"));
    a[1] = vint(16);
    r.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "parseLong", "(Ljava/lang/String;I)J",
                          a, &r) == NJ_OK && r.j == 255,
           "parseLong(\"ff\",16) is 255");

    OK_MSG(static_from_string(vm, SHORTW, "parseShort",
                              "(Ljava/lang/String;)S", "32767", &threw).i == 32767
               && !threw,
           "parseShort(\"32767\") is the maximum");
    (void)static_from_string(vm, SHORTW, "parseShort",
                             "(Ljava/lang/String;)S", "32768", &threw);
    OK_MSG(threw, "parseShort(\"32768\") is out of range");
    OK_MSG(static_from_string(vm, BYTEW, "parseByte",
                              "(Ljava/lang/String;)B", "-128", &threw).i == -128
               && !threw,
           "parseByte(\"-128\") is the minimum");
    (void)static_from_string(vm, BYTEW, "parseByte",
                             "(Ljava/lang/String;)B", "128", &threw);
    OK_MSG(threw, "parseByte(\"128\") is out of range");

    a[0] = vint((nj_s4)'7');
    a[1] = vint(10);
    r.bits = 0u;
    OK_MSG(nj_call_static(vm, CHARW, "digit", "(CI)I", a, &r) == NJ_OK
               && r.i == 7,
           "Character.digit('7',10) is 7");
    a[0] = vint((nj_s4)'f');
    a[1] = vint(16);
    OK_MSG(nj_call_static(vm, CHARW, "digit", "(CI)I", a, &r) == NJ_OK
               && r.i == 15,
           "Character.digit('f',16) is 15");
    a[0] = vint((nj_s4)'G');
    OK_MSG(nj_call_static(vm, CHARW, "digit", "(CI)I", a, &r) == NJ_OK
               && r.i == -1,
           "Character.digit('G',16) is -1");

    /* A wrapper is a bit box: the constructor stores the bits, so the value
     * survives, NaN stays NaN, and the bit conversions agree with it.  The
     * constructor used to write a C cast of the float into an int field. */
    a[0] = vfloat(3.5f);
    o = construct(vm, FLOATW, "(F)V", a, "Float(3.5f)");
    r.bits = 0u;
    OK_MSG(o != NULL
               && nj_call(vm, o, FLOATW, "floatValue", "()F", NULL, &r) == NJ_OK
               && r.f == 3.5f,
           "new Float(3.5f).floatValue() is 3.5f");
    a[0] = vfloat(0.0f / 0.0f);
    o = construct(vm, FLOATW, "(F)V", a, "Float(NaN)");
    OK_MSG(o != NULL && nj_call_int(vm, o, FLOATW, "isNaN", "()Z", NULL) == 1,
           "new Float(NaN).isNaN() is true");
    OK_MSG(o != NULL
               && nj_call_int(vm, o, FLOATW, "isInfinite", "()Z", NULL) == 0,
           "new Float(NaN).isInfinite() is false");

    a[0] = vint(0x3F800000);
    r.bits = 0u;
    OK_MSG(nj_call_static(vm, FLOATW, "intBitsToFloat", "(I)F", a, &r) == NJ_OK
               && r.f == 1.0f,
           "Float.intBitsToFloat(0x3F800000) is 1.0f");
    a[0] = vfloat(1.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "floatToIntBits", "(F)I", a, &r) == NJ_OK
               && (nj_u4)r.i == 0x3F800000u,
           "Float.floatToIntBits(1.0f) is 0x3F800000");
    a[0] = vfloat(0.0f / 0.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "floatToIntBits", "(F)I", a, &r) == NJ_OK
               && (nj_u4)r.i == 0x7FC00000u,
           "floatToIntBits(NaN) is the canonical quiet NaN");

    /*
     * A float field holds the bit pattern, not a rounded number, and 0.1f is
     * the value that tells the two apart: its 32 bits (0x3DCCCCCD) do not
     * survive a round trip through a C float-to-int conversion, so a wrapper
     * that stored or read `value` through the typed accessors handed back
     * 0x3DCCCC00 - a different float, by one part in ten million, in every
     * arithmetic expression the title went on to evaluate.
     */
    a[0] = vfloat(0.1f);
    o = construct(vm, FLOATW, "(F)V", a, "Float(0.1f)");
    {
        nj_value got;
        got.bits = 0u;
        OK(o != NULL && nj_call(vm, o, FLOATW, "floatValue", "()F", NULL, &got) == NJ_OK);
        a[0] = got;
        OK_MSG(nj_call_static(vm, FLOATW, "floatToIntBits", "(F)I", a, &r) == NJ_OK
                   && (nj_u4)r.i == 0x3DCCCCCDu,
               "new Float(0.1f).floatValue() keeps its four bytes");
    }
    a[0] = vdouble(0.1);
    o = construct(vm, DOUBLEW, "(D)V", a, "Double(0.1)");
    {
        nj_value got;
        got.bits = 0u;
        OK(o != NULL && nj_call(vm, o, DOUBLEW, "doubleValue", "()D", NULL, &got) == NJ_OK);
        a[0] = got;
        OK_MSG(nj_call_static(vm, DOUBLEW, "doubleToLongBits", "(D)J", a, &r) == NJ_OK
                   && (nj_u8)r.j == 0x3FB999999999999Aull,
               "new Double(0.1).doubleValue() keeps its eight bytes");
    }
    /* A NaN carrying a payload the runtime never chose still reads as NaN:
     * the typed accessors converted it, and a float-to-int conversion of a NaN
     * is not required to give anything back at all. */
    a[0] = vint(0x7FC00001);
    OK(nj_call_static(vm, FLOATW, "intBitsToFloat", "(I)F", a, &r) == NJ_OK);
    a[0] = r;
    o = construct(vm, FLOATW, "(F)V", a, "Float(payload NaN)");
    OK_MSG(o != NULL && nj_call_int(vm, o, FLOATW, "isNaN", "()Z", NULL) == 1,
           "a NaN with a payload is still NaN");
    a[0] = vlong((nj_s8)0x7FF8000000000001LL);
    OK(nj_call_static(vm, DOUBLEW, "longBitsToDouble", "(J)D", a, &r) == NJ_OK);
    a[0] = r;
    o = construct(vm, DOUBLEW, "(D)V", a, "Double(payload NaN)");
    OK_MSG(o != NULL && nj_call_int(vm, o, DOUBLEW, "isNaN", "()Z", NULL) == 1,
           "a double NaN with a payload is still NaN");
    a[0] = r;
    OK_MSG(nj_call_static(vm, DOUBLEW, "doubleToLongBits", "(D)J", a, &r) == NJ_OK
               && (nj_u8)r.j == 0x7FF8000000000000ull,
           "doubleToLongBits collapses that NaN to the canonical pattern");

    /*
     * The static isNaN/isInfinite(F) pair, which CLDC 1.1 declares alongside
     * the no-arg instance forms.  Only the instance forms existed, so a MIDlet
     * that tested a computed float failed to resolve the call - a real game
     * (急速狂飙完美破解版_4964) stopped dead on Float.isNaN(F)Z.  Both
     * descriptors are declared here, and the two must not be confused: a
     * lookup that answered the static call with the instance body would read
     * the receiver out of an argument list that has none.
     */
    a[0] = vfloat(0.0f / 0.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "isNaN", "(F)Z", a, &r) == NJ_OK
               && r.i == 1,
           "Float.isNaN(NaN) is true");
    a[0] = vfloat(1.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "isNaN", "(F)Z", a, &r) == NJ_OK
               && r.i == 0,
           "Float.isNaN(1.0f) is false");
    a[0] = vfloat(1.0f / 0.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "isInfinite", "(F)Z", a, &r) == NJ_OK
               && r.i == 1,
           "Float.isInfinite(1.0f/0.0f) is true");
    a[0] = vfloat(-1.0f / 0.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "isInfinite", "(F)Z", a, &r) == NJ_OK
               && r.i == 1,
           "Float.isInfinite(-1.0f/0.0f) is true");
    a[0] = vfloat(1.0f);
    OK_MSG(nj_call_static(vm, FLOATW, "isInfinite", "(F)Z", a, &r) == NJ_OK
               && r.i == 0,
           "Float.isInfinite(1.0f) is false");

    a[0] = vdouble(0.0 / 0.0);
    OK_MSG(nj_call_static(vm, DOUBLEW, "isNaN", "(D)Z", a, &r) == NJ_OK
               && r.i == 1,
           "Double.isNaN(NaN) is true");
    a[0] = vdouble(1.0 / 0.0);
    OK_MSG(nj_call_static(vm, DOUBLEW, "isInfinite", "(D)Z", a, &r) == NJ_OK
               && r.i == 1,
           "Double.isInfinite(1.0/0.0) is true");
    a[0] = vdouble(2.5);
    OK_MSG(nj_call_static(vm, DOUBLEW, "isNaN", "(D)Z", a, &r) == NJ_OK
               && r.i == 0,
           "Double.isNaN(2.5) is false");

    a[0] = vref(nj_string_new(vm, "2.5"));
    o = nj_call_ref(vm, NULL, DOUBLEW, "valueOf",
                    "(Ljava/lang/String;)Ljava/lang/Double;", a);
    r.bits = 0u;
    OK_MSG(o != NULL
               && nj_call(vm, o, DOUBLEW, "doubleValue", "()D", NULL,
                          &r) == NJ_OK
               && r.d == 2.5,
           "Double.valueOf(\"2.5\").doubleValue() is 2.5");
    a[0] = vref(nj_string_new(vm, "2.5x"));
    o = nj_call_ref(vm, NULL, DOUBLEW, "valueOf",
                    "(Ljava/lang/String;)Ljava/lang/Double;", a);
    OK_MSG(o == NULL && nj_has_exception(vm), "Double.valueOf(\"2.5x\") throws");
    nj_clear_exception(vm);

    /* append(double) on both buffers, so a title that prints a score as a
     * double gets a line rather than a NoSuchMethodError. */
    a[0] = vref(nj_string_new(vm, "x="));
    sb = construct(vm, SBUF, "(Ljava/lang/String;)V", a, "StringBuffer(\"x=\")");
    a[0] = vdouble(2.5);
    OK_MSG(sb != NULL
               && nj_call_ref(vm, sb, SBUF, "append",
                              "(D)Ljava/lang/StringBuffer;", a) == sb,
           "StringBuffer.append(double) returns the buffer");
    s = nj_call_ref(vm, sb, SBUF, "toString", "()Ljava/lang/String;", NULL);
    OK_MSG(s != NULL && nj_string_equals_utf8(s, "x=2.5"),
           "StringBuffer(\"x=\").append(2.5) is \"x=2.5\"");

    /* println(Object) and println(boolean): the assertion is that they return
     * cleanly.  The text itself goes to the host's stdout, like the runtime's
     * own diagnostics. */
    o = nj_object_new(vm, "java/io/PrintStream");
    a[0] = vref(NULL);
    OK_MSG(o != NULL
               && nj_call(vm, o, "java/io/PrintStream", "println",
                          "(Ljava/lang/Object;)V", a, NULL) == NJ_OK,
           "println((Object)null) writes a line");
    a[0] = vint(0);
    OK_MSG(o != NULL
               && nj_call(vm, o, "java/io/PrintStream", "println", "(Z)V", a,
                          NULL) == NJ_OK,
           "println(false) writes a line");
    clean(vm, "print");

    clean(vm, "numbers");
}

/* ------------------------------------------------------------------ */
/* 6. initialising on a static field read                              */
/* ------------------------------------------------------------------ */
/* Two classes, compiled from source and kept as bytes so the test needs no
 * jar and no JDK:
 *
 *   public class T { static int[] TAB; static { TAB = new int[2];
 *                                               TAB[0] = 77; TAB[1] = 88; } }
 *   public class R { public static int first() { return T.TAB[0]; } }
 *
 * R has no <clinit> of its own, so R.first() cannot return 77 unless the
 * `getstatic T.TAB` inside it initialised T on the way past.  Without that,
 * TAB is null, `iaload` raises NullPointerException and first() never
 * returns - which is exactly what the corpus title did.
 */
static const unsigned char T_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x47, 0x00, 0x12, 0x0a, 0x00,
    0x02, 0x00, 0x03, 0x07, 0x00, 0x04, 0x0c, 0x00, 0x05, 0x00, 0x06, 0x01,
    0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e,
    0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x09, 0x00, 0x08,
    0x00, 0x09, 0x07, 0x00, 0x0a, 0x0c, 0x00, 0x0b, 0x00, 0x0c, 0x01, 0x00,
    0x01, 0x54, 0x01, 0x00, 0x03, 0x54, 0x41, 0x42, 0x01, 0x00, 0x02, 0x5b,
    0x49, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65, 0x01, 0x00, 0x0f, 0x4c,
    0x69, 0x6e, 0x65, 0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62,
    0x6c, 0x65, 0x01, 0x00, 0x08, 0x3c, 0x63, 0x6c, 0x69, 0x6e, 0x69, 0x74,
    0x3e, 0x01, 0x00, 0x0a, 0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69,
    0x6c, 0x65, 0x01, 0x00, 0x06, 0x54, 0x2e, 0x6a, 0x61, 0x76, 0x61, 0x00,
    0x21, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
    0x0b, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x05, 0x00,
    0x06, 0x00, 0x01, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7, 0x00, 0x01, 0xb1, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x08, 0x00, 0x0f, 0x00, 0x06, 0x00, 0x01, 0x00, 0x0d,
    0x00, 0x00, 0x00, 0x39, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15,
    0x05, 0xbc, 0x0a, 0xb3, 0x00, 0x07, 0xb2, 0x00, 0x07, 0x03, 0x10, 0x4d,
    0x4f, 0xb2, 0x00, 0x07, 0x04, 0x10, 0x58, 0x4f, 0xb1, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x12, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x06, 0x00, 0x05, 0x00, 0x0d, 0x00, 0x06, 0x00, 0x14, 0x00,
    0x07, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x11
};

static const unsigned char R_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x47, 0x00, 0x15, 0x0a, 0x00,
    0x02, 0x00, 0x03, 0x07, 0x00, 0x04, 0x0c, 0x00, 0x05, 0x00, 0x06, 0x01,
    0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e,
    0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x09, 0x00, 0x08,
    0x00, 0x09, 0x07, 0x00, 0x0a, 0x0c, 0x00, 0x0b, 0x00, 0x0c, 0x01, 0x00,
    0x01, 0x54, 0x01, 0x00, 0x03, 0x54, 0x41, 0x42, 0x01, 0x00, 0x02, 0x5b,
    0x49, 0x07, 0x00, 0x0e, 0x01, 0x00, 0x01, 0x52, 0x01, 0x00, 0x04, 0x43,
    0x6f, 0x64, 0x65, 0x01, 0x00, 0x0f, 0x4c, 0x69, 0x6e, 0x65, 0x4e, 0x75,
    0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x05,
    0x66, 0x69, 0x72, 0x73, 0x74, 0x01, 0x00, 0x03, 0x28, 0x29, 0x49, 0x01,
    0x00, 0x0a, 0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x46, 0x69, 0x6c, 0x65,
    0x01, 0x00, 0x06, 0x52, 0x2e, 0x6a, 0x61, 0x76, 0x61, 0x00, 0x21, 0x00,
    0x0d, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00,
    0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x1d, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7, 0x00, 0x01, 0xb1,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00, 0x11, 0x00, 0x12, 0x00, 0x01,
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x06, 0xb2, 0x00, 0x07, 0x03, 0x2e, 0xac, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x01, 0x00, 0x13, 0x00, 0x00, 0x00, 0x02, 0x00, 0x14
};

static const unsigned char S_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x47, 0x00, 0x21, 0x0a, 0x00,
    0x02, 0x00, 0x03, 0x07, 0x00, 0x04, 0x0c, 0x00, 0x05, 0x00, 0x06, 0x01,
    0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e,
    0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x07, 0x00, 0x08,
    0x01, 0x00, 0x1d, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x69, 0x6f, 0x2f, 0x42,
    0x79, 0x74, 0x65, 0x41, 0x72, 0x72, 0x61, 0x79, 0x4f, 0x75, 0x74, 0x70,
    0x75, 0x74, 0x53, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x0a, 0x00, 0x07, 0x00,
    0x03, 0x0a, 0x00, 0x07, 0x00, 0x0b, 0x0c, 0x00, 0x0c, 0x00, 0x0d, 0x01,
    0x00, 0x04, 0x73, 0x69, 0x7a, 0x65, 0x01, 0x00, 0x03, 0x28, 0x29, 0x49,
    0x0a, 0x00, 0x07, 0x00, 0x0f, 0x0c, 0x00, 0x05, 0x00, 0x10, 0x01, 0x00,
    0x04, 0x28, 0x49, 0x29, 0x56, 0x0a, 0x00, 0x07, 0x00, 0x12, 0x0c, 0x00,
    0x13, 0x00, 0x10, 0x01, 0x00, 0x05, 0x77, 0x72, 0x69, 0x74, 0x65, 0x07,
    0x00, 0x15, 0x01, 0x00, 0x22, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61,
    0x6e, 0x67, 0x2f, 0x49, 0x6c, 0x6c, 0x65, 0x67, 0x61, 0x6c, 0x41, 0x72,
    0x67, 0x75, 0x6d, 0x65, 0x6e, 0x74, 0x45, 0x78, 0x63, 0x65, 0x70, 0x74,
    0x69, 0x6f, 0x6e, 0x07, 0x00, 0x17, 0x01, 0x00, 0x01, 0x53, 0x01, 0x00,
    0x04, 0x43, 0x6f, 0x64, 0x65, 0x01, 0x00, 0x0f, 0x4c, 0x69, 0x6e, 0x65,
    0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x01,
    0x00, 0x05, 0x70, 0x72, 0x6f, 0x62, 0x65, 0x01, 0x00, 0x04, 0x28, 0x49,
    0x29, 0x49, 0x01, 0x00, 0x08, 0x63, 0x61, 0x70, 0x61, 0x63, 0x69, 0x74,
    0x79, 0x01, 0x00, 0x0d, 0x53, 0x74, 0x61, 0x63, 0x6b, 0x4d, 0x61, 0x70,
    0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x08, 0x6e, 0x65, 0x67, 0x61,
    0x74, 0x69, 0x76, 0x65, 0x01, 0x00, 0x0a, 0x53, 0x6f, 0x75, 0x72, 0x63,
    0x65, 0x46, 0x69, 0x6c, 0x65, 0x01, 0x00, 0x06, 0x53, 0x2e, 0x6a, 0x61,
    0x76, 0x61, 0x00, 0x21, 0x00, 0x16, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x01, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x18,
    0x00, 0x00, 0x00, 0x1d, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
    0x2a, 0xb7, 0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x19, 0x00,
    0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09, 0x00,
    0x1a, 0x00, 0x1b, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x25, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0d, 0x1a, 0xbb, 0x00, 0x07, 0x59,
    0xb7, 0x00, 0x09, 0xb6, 0x00, 0x0a, 0x60, 0xac, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x19, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x09, 0x00, 0x1c, 0x00, 0x0d, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00,
    0x00, 0x54, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x22, 0xbb, 0x00,
    0x07, 0x59, 0x10, 0x07, 0xb7, 0x00, 0x0e, 0x4b, 0x03, 0x3c, 0x1b, 0x10,
    0x14, 0xa2, 0x00, 0x0e, 0x2a, 0x1b, 0xb6, 0x00, 0x11, 0x84, 0x01, 0x01,
    0xa7, 0xff, 0xf2, 0x2a, 0xb6, 0x00, 0x0a, 0xac, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x19, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x0a, 0x00, 0x07, 0x00, 0x1d, 0x00, 0x08, 0x00, 0x1d, 0x00, 0x00,
    0x00, 0x0c, 0x00, 0x02, 0xfd, 0x00, 0x0c, 0x07, 0x00, 0x07, 0x01, 0xfa,
    0x00, 0x10, 0x00, 0x09, 0x00, 0x1e, 0x00, 0x0d, 0x00, 0x01, 0x00, 0x18,
    0x00, 0x00, 0x00, 0x46, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0e,
    0xbb, 0x00, 0x07, 0x59, 0x02, 0xb7, 0x00, 0x0e, 0x57, 0x02, 0xac, 0x4b,
    0x04, 0xac, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0b, 0x00, 0x14,
    0x00, 0x02, 0x00, 0x19, 0x00, 0x00, 0x00, 0x12, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x0c, 0x00, 0x09, 0x00, 0x0d, 0x00, 0x0b, 0x00, 0x0e, 0x00, 0x0c,
    0x00, 0x0f, 0x00, 0x1d, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x4b, 0x07,
    0x00, 0x14, 0x00, 0x01, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20,
};

/*
 * java.util.Random, checked against a real JVM.
 *
 * The seed multiply, the 48-bit truncation, the next(32) splice and the
 * nextLong() shift are wrapping arithmetic that Java specifies exactly and C
 * leaves undefined.  Two of those were already found by UBSan and fixed - the
 * seed multiply, and the nextLong() shift, which reported
 *
 *   nj_lib_util.c:1623:18: runtime error: left shift of 2271413148 by 32
 *   places cannot be represented in type 'long int'
 *
 * while running 电子宠物蛋_11972.  The values below come from running the same
 * seeds through OpenJDK, so this is a reference and not a snapshot of whatever
 * this runtime happens to do.
 */
#define RND "java/util/Random"
static void test_random(nj_vm *vm)
{
    nj_value a[2];
    nj_value out;
    a[0] = vlong(0);
    a[1].bits = 0u;
    nj_ref r = construct(vm, RND, "(J)V", a, "Random(0)");
    if (!r) { return; }

    OK_MSG(nj_call_int(vm, r, RND, "nextInt", "()I", NULL) == -1155484576,
           "new Random(0).nextInt() == -1155484576 (OpenJDK)");
    OK_MSG(nj_call_int(vm, r, RND, "nextInt", "()I", NULL) == -723955400,
           "then -723955400");
    OK_MSG(nj_call_int(vm, r, RND, "nextInt", "()I", NULL) == 1033096058,
           "then 1033096058");
    OK_MSG(nj_call(vm, r, RND, "nextLong", "()J", NULL, &out) == NJ_OK &&
           out.j == (nj_s8)(-7261648964369397258LL),
           "then nextLong() == -7261648964369397258");
    OK_MSG(nj_call(vm, r, RND, "nextLong", "()J", NULL, &out) == NJ_OK &&
           out.j == (nj_s8)5700976833288827063LL,
           "then 5700976833288827063");
    OK_MSG(nj_call_int(vm, r, RND, "nextBoolean", "()Z", NULL) == 0,
           "then nextBoolean() == false");
    OK_MSG(nj_call_int(vm, r, RND, "nextBoolean", "()Z", NULL) == 1,
           "then true");
    OK_MSG(nj_call(vm, r, RND, "nextDouble", "()D", NULL, &out) == NJ_OK &&
           out.d == 0.78153463204530480,
           "then nextDouble() == 0.7815346320453048");

    /* setSeed restarts the same LCG, so the sequence must match the JVM's. */
    a[0] = vlong(1);
    a[1].bits = 0u;
    r = construct(vm, RND, "(J)V", a, "Random(1)");
    if (r) {
        a[0] = vlong(7);
    a[1].bits = 0u;
        OK_MSG(nj_call(vm, r, RND, "setSeed", "(J)V", a, NULL) == NJ_OK,
               "setSeed(7)");
        OK_MSG(nj_call_int(vm, r, RND, "nextInt", "()I", NULL) == -1156638823,
               "then nextInt() == -1156638823");
        a[0] = vint(100);
        OK_MSG(nj_call_int(vm, r, RND, "nextInt", "(I)I", a) == 64,
               "then nextInt(100) == 64");
    }
    clean(vm, "java.util.Random");
}

static void test_static_field_init(nj_vm *vm)
{
    nj_value ret;

    OK(nj_class_load(vm, "T", T_class, (nj_u4)sizeof(T_class)) != NULL);
    OK(nj_class_load(vm, "R", R_class, (nj_u4)sizeof(R_class)) != NULL);
    clean(vm, "loading the static-field fixtures");

    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, "R", "first", "()I", NULL, &ret) == NJ_OK,
           "R.first() completes");
    OK_MSG(ret.i == 77,
           "R.first() returned %ld, so the getstatic ran T.<clinit>",
           (long)ret.i);
    clean(vm, "R.first()");

    clean(vm, "static field init");
}

/*
 * Record store overloads.  The corpus asks for a store in the MIDP 2.0 form
 * (name, createIfNecessary, authmode, writable) and in MIDP 1.0's three
 * string form; only (name) and (name, create) were declared, so eight titles
 * died in startApp with NoSuchMethodError before they read a single record.
 */
static void test_rms_listing(const nj_port *base, int libc_backend)
{
    const char *RS = "javax/microedition/rms/RecordStore";
    const char *OPEN = "(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore;";
    nj_port port = *base;
    nj_vm *vm;
    nj_value a[2], ret;
    nj_ref store;
    char dir[512], ignored[560];
    FILE *fp;
    int pass;

    snprintf(dir, sizeof(dir), "%s/list-%s", base->rms_dir,
             libc_backend ? "libc" : "port");
    (void)mkdir(dir, 0777);
    port.rms_dir = dir;
    if (libc_backend) {
        port.file_open = NULL;
        port.file_remove = NULL;
        port.file_exists = NULL;
        port.dir_open = NULL;
        port.dir_next = NULL;
        port.dir_close = NULL;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    OK_MSG(vm != NULL, "create RMS listing VM");
    if (!vm) { return; }
    a[0] = vref(nj_string_new(vm, "farm.save"));
    (void)nj_call_static(vm, RS, "deleteRecordStore", "(Ljava/lang/String;)V", a, NULL);
    nj_clear_exception(vm);
    snprintf(ignored, sizeof(ignored), "%s/ignored.rms", dir);
    (void)mkdir(ignored, 0777);
    snprintf(ignored, sizeof(ignored), "%s/unrelated.txt", dir);
    fp = fopen(ignored, "wb");
    if (fp) { fclose(fp); }
    OK(nj_call_static(vm, RS, "listRecordStores", "()[Ljava/lang/String;", NULL, &ret) == NJ_OK);
    OK_MSG(ret.r == NULL, "no stores returns null; directories and unrelated files excluded");
    a[0] = vref(nj_string_new(vm, "farm.save"));
    a[1] = vint(1);
    OK(nj_call_static(vm, RS, "openRecordStore", OPEN, a, &ret) == NJ_OK);
    store = ret.r;
    OK(nj_call(vm, store, RS, "closeRecordStore", "()V", NULL, NULL) == NJ_OK);
    /* Listing must not depend on open handles or an in-memory name cache. */
    for (pass = 0; pass < 2; pass++) {
        OK(nj_call_static(vm, RS, "listRecordStores", "()[Ljava/lang/String;", NULL, &ret) == NJ_OK);
        OK_MSG(ret.r && ret.r->length == 1u &&
               nj_string_equals_utf8(ret.r->u.refs[0], "farm.save"),
               "closed store listed with its original name, including after VM restart");
        if (pass == 0) {
            nj_vm_destroy(vm);
            vm = nj_vm_create(&port, 2u * 1024u * 1024u);
            OK(vm != NULL);
            if (!vm) { return; }
        }
    }
    a[0] = vref(nj_string_new(vm, "farm.save"));
    a[1] = vint(0);
    OK(nj_call_static(vm, RS, "openRecordStore", OPEN, a, &ret) == NJ_OK);
    OK_MSG(ret.r != NULL, "listed store can be reopened without creation");
    OK(nj_call(vm, ret.r, RS, "closeRecordStore", "()V", NULL, NULL) == NJ_OK);
    a[0] = vref(nj_string_new(vm, "farm.save"));
    OK(nj_call_static(vm, RS, "deleteRecordStore", "(Ljava/lang/String;)V", a, NULL) == NJ_OK);
    OK(nj_call_static(vm, RS, "listRecordStores", "()[Ljava/lang/String;", NULL, &ret) == NJ_OK);
    OK_MSG(ret.r == NULL, "deleted store disappears from listing");
    clean(vm, "RMS listing");
    nj_vm_destroy(vm);
    (void)remove(ignored);
    snprintf(ignored, sizeof(ignored), "%s/ignored.rms", dir);
    (void)remove(ignored);
    (void)remove(dir);
}

static void test_rms_overloads(nj_vm *vm)
{
    static const char *RS = "javax/microedition/rms/RecordStore";
    static const char *OPEN4 = "(Ljava/lang/String;ZIZ)"
                               "Ljavax/microedition/rms/RecordStore;";
    static const char *OPEN3 = "(Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/lang/String;)"
                               "Ljavax/microedition/rms/RecordStore;";
    nj_value a[4];
    nj_value ret;
    nj_ref nm, rs, got;

    nm = nj_string_new(vm, "njlib_overloads");
    OK_MSG(nm != NULL, "an RMS name can be built");

    /* createIfNecessary = true, AUTHMODE_ANY = 1, writable = true */
    a[0] = vref(nm);
    a[1] = vint(1);
    a[2] = vint(1);
    a[3] = vint(1);
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, RS, "openRecordStore", OPEN4, a, &ret) == NJ_OK,
           "MIDP 2.0's openRecordStore(name, create, authmode, writable)");
    rs = ret.r;
    OK_MSG(rs != NULL, "the store opened");
    clean(vm, "opening a store the MIDP 2.0 way");
    if (!rs) {
        return;
    }

    got = nj_call_ref(vm, rs, RS, "getName", "()Ljava/lang/String;", NULL);
    OK_MSG(got != NULL && nj_string_equals_utf8(got, "njlib_overloads"),
           "the store knows its own name");
    clean(vm, "asking the store its name");

    /* HighSpeed 3D creates empty placeholder records before initializing its
     * defaults.  MIDP getRecord(int) must return null for these, not byte[0]. */
    {
        nj_s4 id;
        nj_ref data = nj_byte_array_new(vm, 1u);
        nj_ref empty = nj_byte_array_new(vm, 0u);
        OK_MSG(data != NULL && empty != NULL, "RMS empty-record fixtures");
        if (data && empty) {
            data->u.bytes[0] = 73u;
            a[0] = vref(NULL); a[1] = vint(0); a[2] = vint(0);
            OK_MSG(nj_call(vm, rs, RS, "addRecord", "([BII)I", a, &ret)
                       == NJ_OK, "add a null-data empty record");
            id = ret.i;
            a[0] = vint(id);
            ret.r = data;
            OK_MSG(nj_call(vm, rs, RS, "getRecord", "(I)[B", a, &ret)
                       == NJ_OK && ret.r == NULL,
                   "an empty record returns null");
            OK_MSG(nj_call_int(vm, rs, RS, "getRecordSize", "(I)I", a) == 0,
                   "an empty record has size zero");
            a[1] = vref(data); a[2] = vint(1);
            OK_MSG(nj_call(vm, rs, RS, "getRecord", "(I[BI)I", a, &ret)
                       == NJ_OK && ret.i == 0 && data->u.bytes[0] == 73u,
                   "buffer overload copies zero bytes for an empty record");
            a[0] = vint(id); a[1] = vref(data);
            a[2] = vint(0); a[3] = vint(1);
            OK_MSG(nj_call(vm, rs, RS, "setRecord", "(I[BII)V", a, NULL)
                       == NJ_OK, "initialize the placeholder record");
            got = nj_call_ref(vm, rs, RS, "getRecord", "(I)[B", a);
            OK_MSG(got && got != data && got->length == 1u &&
                       got->u.bytes[0] == 73u,
                   "a nonempty record still returns an independent copy");
            a[1] = vref(empty); a[3] = vint(0);
            OK_MSG(nj_call(vm, rs, RS, "setRecord", "(I[BII)V", a, NULL)
                       == NJ_OK, "clear a record using a zero-length array");
            ret.r = data;
            OK_MSG(nj_call(vm, rs, RS, "getRecord", "(I)[B", a, &ret)
                       == NJ_OK && ret.r == NULL,
                   "a cleared record returns null");
            a[0] = vref(empty); a[1] = vint(0); a[2] = vint(0);
            OK_MSG(nj_call(vm, rs, RS, "addRecord", "([BII)I", a, &ret)
                       == NJ_OK, "add an empty-array record");
            a[0] = vint(ret.i);
            ret.r = data;
            OK_MSG(nj_call(vm, rs, RS, "getRecord", "(I)[B", a, &ret)
                       == NJ_OK && ret.r == NULL,
                   "an empty-array record also returns null");
            clean(vm, "reading empty RMS records");
        }
    }

    /* The same store through MIDP 1.0's (name, vendor, suite). */
    a[0] = vref(nm);
    a[1] = vref(nj_string_new(vm, "nanoj2me"));
    a[2] = vref(nj_string_new(vm, "test_lib"));
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, RS, "openRecordStore", OPEN3, a, &ret) == NJ_OK,
           "MIDP 1.0's openRecordStore(name, vendor, suite)");
    OK_MSG(ret.r != NULL, "the store opens under a vendor and suite too");
    clean(vm, "opening a store the MIDP 1.0 way");

    /* createIfNecessary = false on a name that is not there.  The spec wants
     * RecordStoreNotFoundException; a silent NULL would surface much later as
     * a NullPointerException inside the title. */
    a[0] = vref(nj_string_new(vm, "njlib_absent_9f3a"));
    a[1] = vint(0);
    a[2] = vint(1);
    a[3] = vint(0);
    ret.bits = 0u;
    if (nj_call_static(vm, RS, "openRecordStore", OPEN4, a, &ret) == NJ_OK) {
        g_fail++;
        fprintf(stderr, "FAIL %s:%d: a missing store opened anyway\n",
                __FILE__, __LINE__);
        clean(vm, "opening a store that is not there");
    } else {
        const char *d = nj_exception_describe(vm);
        OK_MSG(d != NULL && strstr(d, "RecordStoreNotFoundException") != NULL,
               "a missing store raises RecordStoreNotFoundException: %s",
               d ? d : "(nothing)");
        nj_clear_exception(vm);
    }
}

/*
 * A native reads the caller's operand stack when it reads an argument it does
 * not have.  ByteArrayOutputStream() takes no arguments, so its `args`
 * window points at the word ABOVE the frame - and in `f(x) + new BAOS()`,
 * that word is the dup'd reference of the object being constructed.  Its low
 * half is a heap address: large, positive, and different on every build, so
 * the constructor allocated it and the title died with OutOfMemoryError
 * during <clinit> on some builds and not others.  S.probe is that shape
 * exactly: `iload_0; new; dup; invokespecial <init>()V; invokevirtual size`.
 */
/*
 * O is the OutOfMemoryError fixture.  Reproducing the bug needs FRAGMENTATION,
 * not a request bigger than the heap: array_new_internal (src/core/nj_vm.c)
 * rejects len > 0x00FFFFFF with OutOfMemoryError before the allocator is even
 * consulted, so an oversized array cannot reach the broken path.  fragment()
 * arranges the shape that did reach it - a 1.5 MB array fills a 2 MB heap, is
 * dropped, and the next request (1.6 MB) fits in the heap but in no free block
 * the collection can produce.
 *
 * nj_alloc's "collect once and retry, then give up" read its retry counter out
 * of vm->gc_requested, and nj_gc_run CLEARS that flag when it starts - so the
 * allocation collected, retried, collected, retried, forever.  A real title
 * (火影忍者-终极格斗_6604) asked for 300 KB on a heap with 7.2 MB free whose
 * largest free block was 285 KB and spun 750,000 collections in twenty seconds
 * with its frame counter frozen, at heap 8 only.  With the counter held in a
 * local, the second failure returns NULL and the caller raises
 * OutOfMemoryError exactly as a game expects.
 *
 * Both halves matter: tryFrag catches it (returns -1) and alloc leaves it
 * pending for a caller that does not.
 */
static const unsigned char O_class[] = {
    0xCAu, 0xFEu, 0xBAu, 0xBEu, 0x00u, 0x47u, 0x00u, 0x34u, 0x00u, 0x1Au, 0x0Au, 0x00u,
    0x02u, 0x00u, 0x03u, 0x07u, 0x00u, 0x04u, 0x0Cu, 0x00u, 0x05u, 0x00u, 0x06u, 0x01u,
    0x00u, 0x10u, 0x6Au, 0x61u, 0x76u, 0x61u, 0x2Fu, 0x6Cu, 0x61u, 0x6Eu, 0x67u, 0x2Fu,
    0x4Fu, 0x62u, 0x6Au, 0x65u, 0x63u, 0x74u, 0x01u, 0x00u, 0x06u, 0x3Cu, 0x69u, 0x6Eu,
    0x69u, 0x74u, 0x3Eu, 0x01u, 0x00u, 0x03u, 0x28u, 0x29u, 0x56u, 0x03u, 0x00u, 0x01u,
    0x86u, 0xA0u, 0x03u, 0x00u, 0x16u, 0xE3u, 0x60u, 0x03u, 0x00u, 0x18u, 0x6Au, 0x00u,
    0x0Au, 0x00u, 0x0Bu, 0x00u, 0x0Cu, 0x07u, 0x00u, 0x0Du, 0x0Cu, 0x00u, 0x0Eu, 0x00u,
    0x0Fu, 0x01u, 0x00u, 0x01u, 0x4Fu, 0x01u, 0x00u, 0x08u, 0x66u, 0x72u, 0x61u, 0x67u,
    0x6Du, 0x65u, 0x6Eu, 0x74u, 0x01u, 0x00u, 0x03u, 0x28u, 0x29u, 0x49u, 0x07u, 0x00u,
    0x11u, 0x01u, 0x00u, 0x1Au, 0x6Au, 0x61u, 0x76u, 0x61u, 0x2Fu, 0x6Cu, 0x61u, 0x6Eu,
    0x67u, 0x2Fu, 0x4Fu, 0x75u, 0x74u, 0x4Fu, 0x66u, 0x4Du, 0x65u, 0x6Du, 0x6Fu, 0x72u,
    0x79u, 0x45u, 0x72u, 0x72u, 0x6Fu, 0x72u, 0x01u, 0x00u, 0x04u, 0x43u, 0x6Fu, 0x64u,
    0x65u, 0x01u, 0x00u, 0x0Fu, 0x4Cu, 0x69u, 0x6Eu, 0x65u, 0x4Eu, 0x75u, 0x6Du, 0x62u,
    0x65u, 0x72u, 0x54u, 0x61u, 0x62u, 0x6Cu, 0x65u, 0x01u, 0x00u, 0x07u, 0x74u, 0x72u,
    0x79u, 0x46u, 0x72u, 0x61u, 0x67u, 0x01u, 0x00u, 0x0Du, 0x53u, 0x74u, 0x61u, 0x63u,
    0x6Bu, 0x4Du, 0x61u, 0x70u, 0x54u, 0x61u, 0x62u, 0x6Cu, 0x65u, 0x01u, 0x00u, 0x05u,
    0x61u, 0x6Cu, 0x6Cu, 0x6Fu, 0x63u, 0x01u, 0x00u, 0x04u, 0x28u, 0x49u, 0x29u, 0x49u,
    0x01u, 0x00u, 0x0Au, 0x53u, 0x6Fu, 0x75u, 0x72u, 0x63u, 0x65u, 0x46u, 0x69u, 0x6Cu,
    0x65u, 0x01u, 0x00u, 0x06u, 0x4Fu, 0x2Eu, 0x6Au, 0x61u, 0x76u, 0x61u, 0x00u, 0x21u,
    0x00u, 0x0Bu, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x01u,
    0x00u, 0x05u, 0x00u, 0x06u, 0x00u, 0x01u, 0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x1Du,
    0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x05u, 0x2Au, 0xB7u, 0x00u, 0x01u,
    0xB1u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x13u, 0x00u, 0x00u, 0x00u, 0x06u, 0x00u,
    0x01u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x09u, 0x00u, 0x0Eu, 0x00u, 0x0Fu, 0x00u,
    0x01u, 0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x45u, 0x00u, 0x01u, 0x00u, 0x04u, 0x00u,
    0x00u, 0x00u, 0x19u, 0x12u, 0x07u, 0xBCu, 0x08u, 0x4Bu, 0x12u, 0x08u, 0xBCu, 0x08u,
    0x4Cu, 0x12u, 0x07u, 0xBCu, 0x08u, 0x4Du, 0x01u, 0x4Cu, 0x12u, 0x09u, 0xBCu, 0x08u,
    0x4Eu, 0x2Du, 0xBEu, 0xACu, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x13u, 0x00u, 0x00u,
    0x00u, 0x1Au, 0x00u, 0x06u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x05u, 0x00u, 0x04u,
    0x00u, 0x0Au, 0x00u, 0x05u, 0x00u, 0x0Fu, 0x00u, 0x06u, 0x00u, 0x11u, 0x00u, 0x07u,
    0x00u, 0x16u, 0x00u, 0x08u, 0x00u, 0x09u, 0x00u, 0x14u, 0x00u, 0x0Fu, 0x00u, 0x01u,
    0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x3Bu, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x00u,
    0x00u, 0x07u, 0xB8u, 0x00u, 0x0Au, 0xACu, 0x4Bu, 0x02u, 0xACu, 0x00u, 0x01u, 0x00u,
    0x00u, 0x00u, 0x03u, 0x00u, 0x04u, 0x00u, 0x10u, 0x00u, 0x02u, 0x00u, 0x13u, 0x00u,
    0x00u, 0x00u, 0x0Eu, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x0Du, 0x00u, 0x04u, 0x00u,
    0x0Eu, 0x00u, 0x05u, 0x00u, 0x0Fu, 0x00u, 0x15u, 0x00u, 0x00u, 0x00u, 0x06u, 0x00u,
    0x01u, 0x44u, 0x07u, 0x00u, 0x10u, 0x00u, 0x09u, 0x00u, 0x16u, 0x00u, 0x17u, 0x00u,
    0x01u, 0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x23u, 0x00u, 0x01u, 0x00u, 0x02u, 0x00u,
    0x00u, 0x00u, 0x07u, 0x1Au, 0xBCu, 0x08u, 0x4Cu, 0x2Bu, 0xBEu, 0xACu, 0x00u, 0x00u,
    0x00u, 0x01u, 0x00u, 0x13u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x00u, 0x02u, 0x00u, 0x00u,
    0x00u, 0x14u, 0x00u, 0x04u, 0x00u, 0x15u, 0x00u, 0x01u, 0x00u, 0x18u, 0x00u, 0x00u,
    0x00u, 0x02u, 0x00u, 0x19u,
};

static void test_constructor_arg_window(nj_vm *vm)
{
    nj_value a[2];
    nj_value ret;

    OK(nj_class_load(vm, "S", S_class, (nj_u4)sizeof(S_class)) != NULL);
    clean(vm, "loading the argument-window fixture");

    a[0] = vint(5);
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, "S", "probe", "(I)I", a, &ret) == NJ_OK,
           "ByteArrayOutputStream() under a live operand stack");
    OK_MSG(ret.i == 5,
           "probe(5) returned %ld: the no-argument constructor left the stack "
           "above its frame alone", (long)ret.i);
    clean(vm, "probe(5)");

    /* The (int) form still has to take its size from the argument, and a
     * negative one has to be rejected the way the API says. */
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, "S", "capacity", "()I", NULL, &ret) == NJ_OK,
           "ByteArrayOutputStream(7) written past its capacity");
    OK_MSG(ret.i == 20, "twenty bytes went in and size() is %ld", (long)ret.i);
    clean(vm, "capacity()");

    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, "S", "negative", "()I", NULL, &ret) == NJ_OK,
           "ByteArrayOutputStream(-1) is caught as IllegalArgumentException");
    OK_MSG(ret.i == 1, "the negative capacity was rejected: %ld", (long)ret.i);
    clean(vm, "negative()");

    clean(vm, "the argument window");
}

static void test_alloc_gives_up(nj_vm *vm)
{
    nj_value a[2];
    nj_value ret;
    const char *d;

    OK(nj_class_load(vm, "O", O_class, (nj_u4)sizeof(O_class)) != NULL);
    clean(vm, "loading the allocation fixture");

    /* The catchable half.  This is the call that used to spin forever. */
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, "O", "tryFrag", "()I", NULL, &ret) == NJ_OK,
           "an unsatisfiable allocation returns instead of collecting forever");
    OK_MSG(ret.i == -1,
           "tryFrag() returned %ld: the OutOfMemoryError reached the game's "
           "catch block", (long)ret.i);
    clean(vm, "tryFrag()");

    /* The throwing half: the same failure without a handler leaves
     * OutOfMemoryError pending for the interpreter to propagate. */
    a[0] = vint(16777216);
    ret.bits = 0u;
    (void)nj_call_static(vm, "O", "alloc", "(I)I", a, &ret);
    d = nj_exception_describe(vm);
    OK_MSG(nj_has_exception(vm) && d && strstr(d, "OutOfMemoryError") != NULL,
           "the unmet allocation raised OutOfMemoryError: %s",
           d ? d : "(no exception)");
    nj_clear_exception(vm);

    clean(vm, "the allocation give-up");
}

/*
 * Connector.open(name, mode, timeouts) - MIDP 2.0's form, and the one eight
 * corpus titles call to read a data file.  The port serves file:// and
 * nothing else, so the interesting part is that the call arrives at the
 * scheme check at all: before the overload existed it raised
 * NoSuchMethodError, and the title never got as far as its data.
 */
static void test_connector_open_mode(nj_vm *vm)
{
    static const char *CONN = "javax/microedition/io/Connector";
    nj_value a[3];
    nj_value ret;
    const char *d;

    /* Connector.READ == 1, timeouts == true.
     *
     * The URL used to be "http://", which this runtime now serves through the
     * port's http_request hook, so the example of a scheme that stays refused
     * moved to one no port can serve without a socket stack.  The point of the
     * check is the three-argument overload reaching the scheme check and
     * naming the scheme, not which scheme it is.
     */
    a[0] = vref(nj_string_new(vm, "socket://example.invalid:80/data"));
    a[1] = vint(1);
    a[2] = vint(1);
    ret.bits = 0u;
    if (nj_call_static(vm, CONN, "open",
                       "(Ljava/lang/String;IZ)"
                       "Ljavax/microedition/io/Connection;",
                       a, &ret) == NJ_OK) {
        g_fail++;
        fprintf(stderr, "FAIL %s:%d: a scheme this port cannot serve opened\n",
                __FILE__, __LINE__);
        clean(vm, "Connector.open with a mode");
        return;
    }
    d = nj_exception_describe(vm);
    OK_MSG(d != NULL && strstr(d, "ConnectionNotFoundException") != NULL,
           "the three-argument open reaches the scheme check: %s",
           d ? d : "(nothing)");
    nj_clear_exception(vm);
    clean(vm, "Connector.open with a mode");
}

/*
 * CLDC 1.1 declares DataInput and DataOutput as interfaces, and MIDlets call
 * their methods through an interface reference: 拳皇98终极之战_7534 does
 * `invokeinterface java/io/DataInput.readByte:()B` on a DataInputStream and
 * used to stop there with NoClassDefFoundError.
 */
static void test_data_interfaces(nj_vm *vm)
{
    static const char *DIN = "java/io/DataInput";
    static const char *DOUT = "java/io/DataOutput";
    nj_value a[3];
    nj_value ret;
    nj_ref bytes, obj;

    OK_MSG(nj_class_lookup(vm, DIN) != NULL, "java/io/DataInput is declared");
    OK_MSG(nj_class_lookup(vm, DOUT) != NULL, "java/io/DataOutput is declared");

    /* One byte, read back through the interface. */
    bytes = nj_byte_array_new(vm, 1u);
    OK_MSG(bytes != NULL, "a one-byte array");
    ((nj_u1 *)nj_array_data(bytes))[0] = (nj_u1)0x2Au;
    a[0].r = bytes;
    obj = nj_object_new(vm, BAIS);
    OK_MSG(obj != NULL, "a ByteArrayInputStream object");
    OK_MSG(nj_call_special(vm, obj, BAIS, "<init>", "([B)V", a, NULL) == NJ_OK,
           "over the array");
    a[0].r = obj;
    obj = nj_object_new(vm, DIS);
    OK_MSG(obj != NULL, "a DataInputStream object");
    OK_MSG(nj_call_special(vm, obj, DIS, "<init>",
                           "(Ljava/io/InputStream;)V", a, NULL) == NJ_OK,
           "over that stream");
    ret.bits = 0u;
    OK_MSG(nj_call(vm, obj, DIN, "readByte", "()B", NULL, &ret) == NJ_OK,
           "readByte() through java/io/DataInput");
    OK_MSG(ret.i == 0x2A, "and it returned the byte: got %d", ret.i);

    /* And the writing side through java/io/DataOutput. */
    obj = nj_object_new(vm, BAOS);
    OK_MSG(obj != NULL, "a ByteArrayOutputStream object");
    OK_MSG(nj_call_special(vm, obj, BAOS, "<init>", "()V", NULL, NULL) == NJ_OK,
           "constructs");
    a[0].r = obj;
    { nj_ref dos = nj_object_new(vm, DOS);
      OK_MSG(dos != NULL, "a DataOutputStream object");
      OK_MSG(nj_call_special(vm, dos, DOS, "<init>",
                             "(Ljava/io/OutputStream;)V", a, NULL) == NJ_OK,
             "over the byte stream");
      a[0] = vint(7);
      OK_MSG(nj_call(vm, dos, DOUT, "writeByte", "(I)V", a, &ret) == NJ_OK,
             "writeByte(int) through java/io/DataOutput");
    }
    OK_MSG(nj_call_int(vm, obj, BAOS, "size", "()I", NULL) == 1,
           "and one byte reached the stream");
    clean(vm, "DataInput/DataOutput");
}

/*
 * A big Image's pixel array can force a collection between the moment the
 * Image object exists and the moment its fields are written into it.  The
 * object lives only in a C local until the function returns, so a collector
 * that cannot see it sweeps it and links its block into a free list - right
 * over its class pointer.  The old code handed that dangling Image back;
 * 火影忍者-终极格斗_6604 stored it into an Image[] and got
 * ArrayStoreException, swallowed its own loader failure and painted a null
 * Image every frame after that.
 */
/* setClip() with a negative size leaves an empty clip; it does not throw.
 * The comment above g_setClip() names the two corpus titles that do this once
 * per frame. */
/*
 * Image.createImage(Image, x, y, w, h, transform) is the copy-and-transform
 * constructor.  The quarter turns produce an image h wide and w high: every
 * pixel of the w x h source region is rotated into it.  The port allocated
 * w x h for every transform and ran the copy over the source dimensions, so
 * for TRANS_ROT270 the inverse mapping read the source at ux = w - 1 - j,
 * which goes negative as soon as the destination row index reaches w - and a
 * negative index in nj_size arithmetic is a wild read, i.e. a segmentation
 * fault rather than an exception (tests/games/三国赤壁小乔秘史_7976.jar died that
 * way after printing its first two log lines).
 */
/*
 * Long.toString has a long argument, and the port had no such method at all:
 * tests/games/CS反恐精英(蓝牙对战)_6858.jar died with
 *   NoSuchMethodError: java/lang/Long.toString(J)Ljava/lang/String;
 * at the first score it formatted.  The magnitude is taken in unsigned
 * arithmetic, so Long.MIN_VALUE prints as the JDK prints it.
 */
static void test_long_to_string(nj_vm *vm)
{
    nj_value a[3];
    nj_value ret;

    a[0].j = 0;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(J)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "0"),
           "Long.toString(0)");
    if (nj_has_exception(vm)) { nj_clear_exception(vm); return; }

    a[0].j = -1;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(J)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "-1"),
           "Long.toString(-1)");

    a[0].j = 9007199254740993LL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(J)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "9007199254740993"),
           "Long.toString keeps every digit of a 53-bit value");

    a[0].j = (-9223372036854775807LL - 1LL);   /* Long.MIN_VALUE */
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(J)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "-9223372036854775808"),
           "Long.toString(Long.MIN_VALUE) does not overflow");

    a[0].j = 255;
    a[1].bits = 0u;
    a[2] = vint(16);
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(JI)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "ff"),
           "Long.toString(255, 16)");

    a[2] = vint(99);                            /* out of range: base ten */
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, LONGW, "toString", "(JI)Ljava/lang/String;", a, &ret) == NJ_OK
           && nj_string_equals_utf8(ret.r, "255"),
           "an out-of-range radix means ten, the way the JDK defaults it");
}

static void test_image_transform_dimensions(nj_vm *vm)
{
    static const struct { int trans; int dw; int dh; } cases[] = {
        { 0, 3, 2 }, { 1, 3, 2 }, { 2, 3, 2 }, { 3, 3, 2 },
        { 4, 2, 3 }, { 5, 2, 3 }, { 6, 2, 3 }, { 7, 2, 3 }
    };
    nj_value a[6];
    nj_value ret;
    nj_ref src;
    nj_u4 *px;
    int n;

    a[0] = vint(3);
    a[1] = vint(2);
    ret.bits = 0u;
    if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &ret) != NJ_OK
        || ret.r == NULL) {
        OK_MSG(0, "a 3x2 mutable image: %s", nj_exception_describe(vm));
        nj_clear_exception(vm);
        return;
    }
    src = ret.r;
    px = nj_midp_image_pixels(vm, src);
    OK_MSG(px != NULL, "the source image exposes its pixel store");
    if (px == NULL) {
        return;
    }
    for (n = 0; n < 6; n++) {
        px[n] = 0xFF000000u | (nj_u4)n;   /* six distinguishable pixels */
    }

    for (n = 0; n < (int)(sizeof(cases) / sizeof(cases[0])); n++) {
        nj_u4 *dpx;
        a[0].r = src;
        a[1] = vint(0);
        a[2] = vint(0);
        a[3] = vint(3);
        a[4] = vint(2);
        a[5] = vint(cases[n].trans);
        ret.bits = 0u;
        if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                           "(Ljavax/microedition/lcdui/Image;IIIII)"
                           "Ljavax/microedition/lcdui/Image;", a, &ret) != NJ_OK
            || ret.r == NULL) {
            OK_MSG(0, "transform %d: %s", cases[n].trans, nj_exception_describe(vm));
            nj_clear_exception(vm);
            continue;
        }
        OK_MSG(nj_call_int(vm, ret.r, "javax/microedition/lcdui/Image",
                           "getWidth", "()I", NULL) == cases[n].dw,
               "transform %d is %d wide", cases[n].trans, cases[n].dw);
        OK_MSG(nj_call_int(vm, ret.r, "javax/microedition/lcdui/Image",
                           "getHeight", "()I", NULL) == cases[n].dh,
               "transform %d is %d high", cases[n].trans, cases[n].dh);

        dpx = nj_midp_image_pixels(vm, ret.r);
        if (dpx == NULL) {
            OK_MSG(0, "transform %d hands out a pixel store", cases[n].trans);
            continue;
        }
        if (cases[n].trans == 5) {          /* TRANS_ROT90: dst(i,j) = src(j, h-1-i) */
            OK_MSG((dpx[0] & 0xFFu) == 3u, "TRANS_ROT90 puts src(0,1) at dst(0,0)");
            OK_MSG((dpx[1] & 0xFFu) == 0u, "TRANS_ROT90 puts src(0,0) at dst(1,0)");
        } else if (cases[n].trans == 6) {   /* TRANS_ROT270: dst(i,j) = src(w-1-j, i) */
            OK_MSG((dpx[0] & 0xFFu) == 2u, "TRANS_ROT270 puts src(2,0) at dst(0,0)");
            OK_MSG((dpx[4] & 0xFFu) == 0u, "TRANS_ROT270 puts src(0,0) at dst(0,2)");
            OK_MSG((dpx[3] & 0xFFu) == 4u, "TRANS_ROT270 puts src(1,1) at dst(1,1)");
        }
    }
}

static void test_clip_negative_is_empty(nj_vm *vm)
{
    nj_value a[4];
    nj_value ret;
    nj_ref img;
    nj_ref g;

    a[0] = vint(8);
    a[1] = vint(8);
    ret.bits = 0u;
    if (nj_call_static(vm, "javax/microedition/lcdui/Image", "createImage",
                       "(II)Ljavax/microedition/lcdui/Image;", a, &ret) != NJ_OK
        || ret.r == NULL) {
        OK_MSG(0, "an 8x8 mutable image: %s", nj_exception_describe(vm));
        nj_clear_exception(vm);
        return;
    }
    img = ret.r;
    g = nj_call_ref(vm, img, "javax/microedition/lcdui/Image", "getGraphics",
                    "()Ljavax/microedition/lcdui/Graphics;", NULL);
    OK_MSG(g != NULL, "the image hands out a Graphics");

    a[0] = vint(1);
    a[1] = vint(1);
    a[2] = vint(4);
    a[3] = vint(4);
    OK_MSG(nj_call(vm, g, "javax/microedition/lcdui/Graphics", "setClip",
                   "(IIII)V", a, NULL) == NJ_OK,
           "setClip(1,1,4,4)");
    OK_MSG(nj_call_int(vm, g, "javax/microedition/lcdui/Graphics",
                       "getClipWidth", "()I", NULL) == 4,
           "and the clip is four wide");

    a[2] = vint(-2);
    a[3] = vint(-2);
    OK_MSG(nj_call(vm, g, "javax/microedition/lcdui/Graphics", "setClip",
                   "(IIII)V", a, NULL) == NJ_OK,
           "setClip(1,1,-2,-2) does not throw");
    OK_MSG(nj_call_int(vm, g, "javax/microedition/lcdui/Graphics",
                       "getClipWidth", "()I", NULL) == 0,
           "and the clip is empty");
    clean(vm, "Graphics.setClip");
}

/* ByteArrayOutputStream.close() is documented as having no effect;
 * 寻秦皇陵的诅咒_12450 wrote its image bytes, closed, and asked for them back -
 * bound to reset() it got a zero-length image every frame. */
static void test_baos_close_keeps_data(nj_vm *vm)
{
    static const unsigned char src[4] = { 0x50, 0x4C, 0x54, 0x45 };
    nj_value a[3];
    nj_ref arr = nj_byte_array_new(vm, 4);
    nj_ref baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    nj_ref out;

    OK_MSG(arr != NULL && baos != NULL, "a four-byte array and a byte stream");
    if (!arr || !baos) {
        return;
    }
    memcpy(arr->u.bytes, src, sizeof(src));
    a[0] = vref(arr);
    a[1] = vint(0);
    a[2] = vint(4);
    OK_MSG(nj_call(vm, baos, BAOS, "write", "([BII)V", a, NULL) == NJ_OK,
           "write(byte[], 0, 4)");
    OK_MSG(nj_call_int(vm, baos, BAOS, "size", "()I", NULL) == 4,
           "and the count is four");

    OK_MSG(nj_call(vm, baos, BAOS, "close", "()V", NULL, NULL) == NJ_OK,
           "close()");
    OK_MSG(nj_call_int(vm, baos, BAOS, "size", "()I", NULL) == 4,
           "close() does not reset the buffer");
    out = nj_call_ref(vm, baos, BAOS, "toByteArray", "()[B", NULL);
    OK_MSG(out != NULL && out->length == 4,
           "toByteArray() still has all four bytes");
    if (out && out->length == 4) {
        OK_MSG(memcmp(out->u.bytes, src, 4) == 0, "with their values");
    }

    OK_MSG(nj_call(vm, baos, BAOS, "reset", "()V", NULL, NULL) == NJ_OK,
           "reset()");
    OK_MSG(nj_call_int(vm, baos, BAOS, "size", "()I", NULL) == 0,
           "and reset() is the one that clears");
    clean(vm, "ByteArrayOutputStream.close()");
}

/* DataInputStream.available() is the underlying stream's count, not
 * InputStream's default of zero: 寻秦皇陵的诅咒_12450 sized a buffer from it and
 * read a zero-length image ("no image data") once per frame. */
static void test_data_input_available(nj_vm *vm)
{
    static const unsigned char data[5] = { 1, 2, 3, 4, 5 };
    nj_value a[2];
    nj_value ret;
    nj_ref bais = bais_over(vm, data, sizeof(data));
    nj_ref dis;

    OK_MSG(bais != NULL, "a ByteArrayInputStream over five bytes");
    dis = construct(vm, DIS, "(Ljava/io/InputStream;)V", (a[0].r = bais, a),
                    "DataInputStream over it");
    if (dis == NULL) {
        return;
    }
    OK(nj_call(vm, dis, DIS, "available", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 5, "available() is the underlying count, got %d", (int)ret.i);

    a[0].i = 0;
    OK(nj_call(vm, dis, DIS, "read", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 1, "and the first byte reads back, got %d", (int)ret.i);
    OK(nj_call(vm, dis, DIS, "available", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 4, "then it counts down, got %d", (int)ret.i);
}

/* Build a mono 8-bit PCM WAV whose data chunk is `frames` bytes long. */
static nj_u1 *wav_of(nj_u4 frames)
{
    nj_u4 total = 44u + frames;
    nj_u1 *b = (nj_u1 *)malloc(total);
    nj_u4 i;

    if (b == NULL) {
        return NULL;
    }
    memset(b, 0, total);
    memcpy(b, "RIFF", 4);
    b[4] = (nj_u1)((36u + frames) & 0xFFu);
    b[5] = (nj_u1)(((36u + frames) >> 8) & 0xFFu);
    b[6] = (nj_u1)(((36u + frames) >> 16) & 0xFFu);
    b[7] = (nj_u1)(((36u + frames) >> 24) & 0xFFu);
    memcpy(b + 8, "WAVEfmt ", 8);
    b[16] = 16;
    b[20] = 1;                  /* PCM */
    b[22] = 1;                  /* mono */
    b[24] = 0x40; b[25] = 0x1F; /* 8000 Hz */
    b[28] = 0x40; b[29] = 0x1F;
    b[32] = 1;                  /* block align */
    b[34] = 8;                  /* bits per sample */
    memcpy(b + 36, "data", 4);
    b[40] = (nj_u1)(frames & 0xFFu);
    b[41] = (nj_u1)((frames >> 8) & 0xFFu);
    b[42] = (nj_u1)((frames >> 16) & 0xFFu);
    b[43] = (nj_u1)((frames >> 24) & 0xFFu);
    for (i = 0; i < frames; i++) {
        b[44u + i] = (nj_u1)(i & 0x7Fu);
    }
    return b;
}

/* Manager.createPlayer(InputStream, String) reads the whole stream and then
 * allocates the Player.  Both the growing buffer inside the read and the file
 * itself were held only in C locals, so a collection during the pixel copy
 * freed the very bytes being parsed: 愤怒的小鸟-高清版_2353 died with SIGSEGV
 * inside libc, in the memcpy in media_createPlayer_stream. */
static void test_player_survives_collection(nj_vm *vm)
{
    const nj_u4 FRAMES = 400000u;       /* 400000 frames at 8000 Hz = 50 s */
    nj_u1 *wav = wav_of(FRAMES);
    nj_vm_stats before, after;
    nj_value a[2];
    nj_value ret;
    nj_value dur;
    nj_ref in;
    int i;

    if (wav == NULL) {
        OK_MSG(0, "cannot allocate the WAV fixture");
        return;
    }
    in = bais_over(vm, wav, 44u + FRAMES);
    free(wav);
    OK_MSG(in != NULL, "a ByteArrayInputStream over a 400 KB WAV");

    /* Leave less room than the PCM array needs, so allocating it collects. */
    for (i = 0; i < 4000; i++) {
        nj_vm_stats st;
        nj_vm_get_stats(vm, &st);
        if (st.heap_size - st.heap_used < 700000u) {
            break;
        }
        if (!nj_byte_array_new(vm, 65536u)) {
            nj_clear_exception(vm);
            break;
        }
    }
    memset(&before, 0, sizeof(before));
    nj_vm_get_stats(vm, &before);

    a[0].r = in;
    a[1].r = NULL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, MEDIA_MGR, "createPlayer",
                          "(Ljava/io/InputStream;Ljava/lang/String;)L"
                          "javax/microedition/media/Player;", a, &ret) == NJ_OK,
           "createPlayer on a heap that has to collect mid-call");
    OK_MSG(ret.r != NULL, "a Player for 400000 frames of 8-bit PCM");

    memset(&after, 0, sizeof(after));
    nj_vm_get_stats(vm, &after);
    OK_MSG(after.gc_count > before.gc_count,
           "allocating the PCM array really did run a collection");

    if (ret.r != NULL) {
        dur.bits = 0u;
        OK(nj_call(vm, ret.r, PLAYER, "getDuration", "()J", NULL, &dur) == NJ_OK);
        OK_MSG(dur.j == 50000000LL,
               "the parsed duration is the file's own (got %lld us)",
               (long long)dur.j);
    }
}

/* An 8-bit WAV has to be widened to the 16-bit the port takes.  pcm_push()
 * reads pData as 16-bit samples, so a file pushed as it stands plays every
 * byte pair as ONE sample: noise at half speed.  The observable is the
 * Player's own pData array - four 8-bit samples become eight bytes, and the
 * unsigned-to-signed conversion is visible in the bytes themselves. */
static void test_eight_bit_wav_is_widened(nj_vm *vm)
{
    static const nj_u1 expect[8] = {
        0x00u, 0x80u,             /* sample 0   -> -32768 */
        0x00u, 0x81u,             /* sample 1   -> -32512 */
        0x00u, 0x82u,             /* sample 2   -> -32256 */
        0x00u, 0x83u              /* sample 3   -> -32000 */
    };
    nj_u1 *w = wav_of(4u);
    nj_ref bar;
    nj_ref in;
    nj_value a[2];
    nj_value ret;
    nj_field *f;
    nj_ref data;
    int i;
    int same = 1;

    OK_MSG(w != NULL, "a four-frame 8-bit WAV");
    if (w == NULL) {
        return;
    }
    bar = nj_byte_array_new(vm, 48u);
    OK_MSG(bar != NULL, "an array for the file");
    if (bar == NULL) {
        free(w);
        return;
    }
    memcpy(bar->u.bytes, w, 48u);
    free(w);

    a[0].r = bar;
    a[1].bits = 0u;
    in = construct(vm, BAIS, "([B)V", a, "a stream over the WAV");
    OK_MSG(in != NULL, "the stream exists");

    a[0].r = in;
    a[1].r = NULL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, MEDIA_MGR, "createPlayer",
                          "(Ljava/io/InputStream;Ljava/lang/String;)L"
                          "javax/microedition/media/Player;", a, &ret) == NJ_OK,
           "Manager.createPlayer(8-bit WAV)");
    OK_MSG(ret.r != NULL, "a Player for the 8-bit WAV");

    f = nj_field_lookup(vm, PLAYER, "pData", "[B", 0);
    OK_MSG(f != NULL, "the Player's pData field");
    data = (f != NULL) ? nj_field_read_ref(vm, ret.r, f) : NULL;
    OK_MSG(data != NULL, "the Player holds PCM");
    if (data != NULL) {
        OK_MSG(data->length == 8u,
               "four 8-bit frames are eight bytes of 16-bit PCM (got %u)",
               (unsigned)data->length);
        if (data->length == 8u) {
            for (i = 0; i < 8; i++) {
                if (data->u.bytes[i] != expect[i]) {
                    same = 0;
                }
            }
            OK_MSG(same, "the samples were widened, not copied "
                         "(%02x %02x %02x %02x ...)",
                   data->u.bytes[0], data->u.bytes[1],
                   data->u.bytes[2], data->u.bytes[3]);
        }
    }
}

/* Music in Angry Birds exceeds 4 MB. The whole track, including the last
 * sample, must survive loading rather than becoming a truncated Player. */
static void test_large_wav(void)
{
    const nj_u4 frames = 5u * 1024u * 1024u;
    nj_port port;
    nj_vm *large;
    nj_u1 *wav;
    nj_ref bytes, in, pcm;
    nj_value args[2], ret;
    nj_u4 mark;
    nj_field *f;
    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        OK_MSG(0, "headless port for large WAV");
        return;
    }
    large = nj_vm_create(&port, 24u * 1024u * 1024u);
    OK_MSG(large != NULL, "24 MB VM for large WAV");
    if (!large) { nj_port_shutdown(&port); return; }
    mark = nj_root_mark(large);
    wav = wav_of(frames);
    bytes = nj_root(large, nj_byte_array_new(large, frames + 44u));
    OK_MSG(wav && bytes, "large WAV input");
    if (wav && bytes) {
        memcpy(bytes->u.bytes, wav, frames + 44u);
        args[0].r = bytes;
        in = nj_root(large, construct(large, BAIS, "([B)V", args,
                                     "large WAV stream"));
        args[0].r = in;
        args[1].r = NULL;
        ret.r = NULL;
        OK_MSG(nj_call_static(large, MEDIA_MGR, "createPlayer",
                             "(Ljava/io/InputStream;Ljava/lang/String;)L"
                             "javax/microedition/media/Player;", args, &ret)
               == NJ_OK && ret.r != NULL, "load WAV beyond 4 MB");
        if (ret.r) {
            f = nj_field_lookup(large, PLAYER, "pData", "[B", 0);
            pcm = nj_field_read_ref(large, ret.r, f);
            OK_MSG(pcm && pcm->length == frames * 2u,
                   "all large WAV samples are retained");
            if (pcm && pcm->length == frames * 2u) {
                OK_MSG(pcm->u.bytes[frames * 2u - 2u] == 0u &&
                       pcm->u.bytes[frames * 2u - 1u] ==
                           (nj_u1)(wav[frames + 43u] ^ 0x80u),
                       "last sample survives beyond the old limit");
            }
        }
    }
    free(wav);
    nj_root_release(large, mark);
    nj_vm_destroy(large);
    nj_port_shutdown(&port);
}

/* A 16-bit WAV already uses the port's PCM format. Keeping the caller's
 * stream, a private read buffer AND another PCM copy live at once made Angry
 * Birds HD's 13 MB music track exhaust a 32 MB heap. A 6 MB fixture on a 16 MB
 * heap distinguishes the two-copy peak from the old three-copy peak. */
static void test_sixteen_bit_wav_heap(void)
{
    const nj_u4 samples = 6u * 1024u * 1024u;
    nj_port port;
    nj_vm *small;
    nj_u1 *wav;
    nj_ref bytes, in, pcm;
    nj_value args[2], ret, duration;
    nj_field *field;
    nj_u4 mark, i;
    int same = 1;

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        OK_MSG(0, "headless port for 16-bit WAV");
        return;
    }
    small = nj_vm_create(&port, 16u * 1024u * 1024u);
    OK_MSG(small != NULL, "16 MB VM for 6 MB stereo WAV");
    if (!small) { nj_port_shutdown(&port); return; }
    mark = nj_root_mark(small);
    /* wav_of initially writes 8-bit mono. Retain its data bytes, but declare
     * 16-bit stereo at 8000 Hz; append a JUNK chunk after the actual samples. */
    wav = wav_of(samples + 12u);
    bytes = nj_root(small, nj_byte_array_new(small, samples + 56u));
    OK_MSG(wav && bytes, "16-bit WAV input");
    if (wav && bytes) {
        wav[22] = 2u;
        wav[28] = 0x00u; wav[29] = 0x7Du; /* 32000 bytes per second */
        wav[32] = 4u;
        wav[34] = 16u;
        for (i = 0u; i < 4u; i++) {
            wav[40u + i] = (nj_u1)(samples >> (i * 8u));
        }
        memcpy(wav + 44u + samples, "JUNK\004\000\000\000tail", 12u);
        memcpy(bytes->u.bytes, wav, samples + 56u);
        args[0].r = bytes;
        in = nj_root(small, construct(small, BAIS, "([B)V", args,
                                     "16-bit WAV stream"));
        args[0].r = in;
        args[1].r = NULL;
        ret.r = NULL;
        OK_MSG(nj_call_static(small, MEDIA_MGR, "createPlayer",
                             "(Ljava/io/InputStream;Ljava/lang/String;)L"
                             "javax/microedition/media/Player;", args, &ret)
               == NJ_OK && ret.r != NULL,
               "16-bit WAV loads without a third full-size allocation");
        if (ret.r) {
            ret.r = nj_root(small, ret.r);
            field = nj_field_lookup(small, PLAYER, "pData", "[B", 0);
            pcm = field ? nj_field_read_ref(small, ret.r, field) : NULL;
            OK_MSG(pcm && pcm != bytes && pcm->length == samples,
                   "PCM is private and excludes RIFF header and trailing JUNK");
            if (pcm && pcm->length == samples) {
                for (i = 0u; i < samples; i++) {
                    if (pcm->u.bytes[i] != wav[44u + i]) { same = 0; break; }
                }
                OK_MSG(same, "all stereo samples retain their original bytes");
                pcm->u.bytes[0] ^= 0xFFu;
            }
            OK_MSG(memcmp(bytes->u.bytes, wav, samples + 56u) == 0,
                   "the caller's stream backing array is unchanged and unaliased");
            duration.bits = 0u;
            OK(nj_call(small, ret.r, PLAYER, "getDuration", "()J", NULL,
                       &duration) == NJ_OK);
            OK_MSG(duration.j == (nj_s8)(samples / 4u) * 1000000LL / 8000,
                   "16-bit stereo duration uses complete frames");
        }
        nj_clear_exception(small);
    }
    free(wav);
    nj_root_release(small, mark);
    nj_vm_destroy(small);
    nj_port_shutdown(&port);
}

/* A .mid handed to createPlayer() is rendered to PCM, not turned into a
 * silent Player.  105 of the 238 titles in tests/games/ reach the MIDI branch
 * within their first 200 frames, and the difference between the two outcomes
 * is visible on the Player itself: the silent one has no duration and no PCM,
 * the rendered one has both. */
/*
 * A MIDI file that renders to more than the heap can hold must still answer a
 * Player.  createPlayer() falls back to the silent Player instead of failing,
 * because a title treats a failed createPlayer() as a bug worth stopping for
 * while it treats silence as a quiet moment - and 愤怒的小鸟-高清版_2353 asks
 * for its music exactly when the heap is tight.
 *
 * The file is the same shape as the one above, with the note held for 66
 * quarter notes: 33 s at 8 kHz is 528800 bytes, which cannot fit a 512 KB VM.
 */
static void test_midi_too_big_for_heap(void)
{
    static const nj_u1 longsmf[43] = {
        'M', 'T', 'h', 'd', 0u, 0u, 0u, 6u,
        0u, 0u, 0u, 1u, 0x01u, 0xE0u,
        'M', 'T', 'r', 'k', 0u, 0u, 0u, 21u,
        0x00u, 0xFFu, 0x51u, 0x03u, 0x07u, 0xA1u, 0x20u,
        0x00u, 0x90u, 0x45u, 0x64u,
        0x81u, 0xF7u, 0x40u, 0x80u, 0x45u, 0x40u,
        0x00u, 0xFFu, 0x2Fu, 0x00u
    };
    nj_port port;
    nj_vm *small;
    nj_ref bar;
    nj_ref in;
    nj_value a[2];
    nj_value ret;

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        OK_MSG(0, "the second headless port starts");
        return;
    }
    small = nj_vm_create(&port, 512u * 1024u);
    OK_MSG(small != NULL, "a 512 KB VM");
    if (small == NULL) {
        nj_port_shutdown(&port);
        return;
    }

    bar = nj_byte_array_new(small, sizeof(longsmf));
    OK_MSG(bar != NULL, "an array for the long MIDI file");
    if (bar == NULL) {
        nj_vm_destroy(small);
        nj_port_shutdown(&port);
        return;
    }
    memcpy(bar->u.bytes, longsmf, sizeof(longsmf));

    a[0].r = bar;
    a[1].bits = 0u;
    in = construct(small, BAIS, "([B)V", a, "a stream over the long MIDI file");

    a[0].r = in;
    a[1].r = NULL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(small, MEDIA_MGR, "createPlayer",
                          "(Ljava/io/InputStream;Ljava/lang/String;)L"
                          "javax/microedition/media/Player;", a, &ret) == NJ_OK,
           "createPlayer() on a MIDI too big for the heap");
    OK_MSG(!nj_has_exception(small),
           "and the failed music did not leave an exception behind");
    OK_MSG(ret.r != NULL, "and it answered a Player anyway");

    nj_vm_destroy(small);
    nj_port_shutdown(&port);
}

/*
 * A song longer than the old sixty second ceiling has to keep playing.  The
 * whole-file renderer stopped at NJ_MIDI_MAX_FRAMES - 1,323,000 frames, one
 * minute at 22050 Hz - and the Player called that the end of the media, which
 * cut off a tenth of the corpus mid-song.  A streamed Player reports the
 * song's real length and its pushed count runs past the old ceiling.
 */
static void test_midi_long_song_streams(nj_vm *vm)
{
    static const unsigned char smf[] = {
        0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
        0x01, 0xE0,                                      /* division 480 */
        0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x18, /* MTrk, 24 bytes */
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,        /* tempo: 500000 us */
        0x00, 0xC0, 40,                                  /* strings: held */
        0x00, 0x90, 69, 100,                             /* A4 on */
        0x87, 0xA3, 0x00, 0x80, 69, 64,                  /* +115200 ticks = 120 s */
        0x00, 0xFF, 0x2F, 0x00                           /* end of track */
    };
    nj_ref bar, in;
    nj_value ret;
    nj_value a[2];
    nj_value dur;
    long before;
    long pushed = 0;
    int i;

    bar = nj_byte_array_new(vm, (nj_u4)sizeof(smf));
    OK(bar != NULL);
    if (bar == NULL) {
        return;
    }
    memcpy(bar->u.bytes, smf, sizeof(smf));
    a[0].r = bar;
    a[1].bits = 0u;
    in = construct(vm, BAIS, "([B)V", a, "a stream over a 120 second MIDI file");

    a[0].r = in;
    a[1].r = NULL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, MEDIA_MGR, "createPlayer",
                          "(Ljava/io/InputStream;Ljava/lang/String;)L"
                          "javax/microedition/media/Player;", a, &ret) == NJ_OK,
           "Manager.createPlayer(a 120 second MIDI file)");
    OK_MSG(ret.r != NULL, "a Player for the long MIDI file");
    if (ret.r == NULL) {
        return;
    }

    dur.bits = 0u;
    OK(nj_call(vm, ret.r, PLAYER, "getDuration", "()J", NULL, &dur) == NJ_OK);
    OK_MSG(dur.j > 115000000LL,
           "the long song's duration is its own, not the ceiling (got %lld us)",
           (long long)dur.j);

    OK(nj_call(vm, ret.r, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    before = nj_port_headless_pcm_frames(vm->port);
    for (i = 0; i < 600; i++) {
        pushed = nj_port_headless_pcm_frames(vm->port) - before;
        if (pushed > 1323000L) {
            break;
        }
        nj_media_frame(vm);
    }
    pushed = nj_port_headless_pcm_frames(vm->port) - before;
    OK_MSG(pushed > 1323000L,
           "the Player streams past the old sixty second ceiling (%ld frames)",
           pushed);
    OK(nj_call(vm, ret.r, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
}

static void test_midi_player_is_pcm(nj_vm *vm)
{
    /* The same file tests/test_midi.c renders: 480 ticks per quarter, 500000
     * microseconds per quarter, one A4 held for one quarter note. */
    static const nj_u1 smf[42] = {
        'M', 'T', 'h', 'd', 0u, 0u, 0u, 6u,
        0u, 0u, 0u, 1u, 0x01u, 0xE0u,
        'M', 'T', 'r', 'k', 0u, 0u, 0u, 20u,
        0x00u, 0xFFu, 0x51u, 0x03u, 0x07u, 0xA1u, 0x20u,
        0x00u, 0x90u, 0x45u, 0x64u,
        0x83u, 0x60u, 0x80u, 0x45u, 0x40u,
        0x00u, 0xFFu, 0x2Fu, 0x00u
    };
    nj_ref bar;
    nj_ref in;
    nj_value a[2];
    nj_value ret;
    nj_value dur;
    nj_field *f;
    nj_ref data;

    bar = nj_byte_array_new(vm, sizeof(smf));
    OK_MSG(bar != NULL, "an array for the MIDI file");
    if (bar == NULL) {
        return;
    }
    memcpy(bar->u.bytes, smf, sizeof(smf));

    a[0].r = bar;
    a[1].bits = 0u;
    in = construct(vm, BAIS, "([B)V", a, "a stream over the MIDI file");

    a[0].r = in;
    a[1].r = NULL;
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, MEDIA_MGR, "createPlayer",
                          "(Ljava/io/InputStream;Ljava/lang/String;)L"
                          "javax/microedition/media/Player;", a, &ret) == NJ_OK,
           "Manager.createPlayer(a MIDI file)");
    OK_MSG(ret.r != NULL, "a Player for the MIDI file");
    if (ret.r == NULL) {
        return;
    }

    /* 0.5 s of note plus the renderer's tail for the release: the longest
     * release any note in this file asked for, which is the piano bank's
     * 140 ms.  The frame count follows the renderer's sample rate, the
     * duration does not. */
    dur.bits = 0u;
    OK(nj_call(vm, ret.r, PLAYER, "getDuration", "()J", NULL, &dur) == NJ_OK);
    /* 14105 frames at 22050 Hz is 639682 us; allow the rounding either way. */
    OK_MSG(dur.j >= 630000LL && dur.j <= 650000LL,
           "the MIDI Player knows its duration (got %lld us)",
           (long long)dur.j);

    /*
     * The music is not in the Java heap any more: pData is a handle holding
     * the C stream plus the pump's scratch, so a fifty minute song costs the
     * same few kilobytes as a short one.  Anything close to the PCM length
     * (14105 frames here) would mean the whole song was rendered again.
     */
    f = nj_field_lookup(vm, PLAYER, "pData", "[B", 0);
    OK_MSG(f != NULL, "the Player's pData field");
    data = (f != NULL) ? nj_field_read_ref(vm, ret.r, f) : NULL;
    OK_MSG(data != NULL, "the MIDI Player holds a handle");
    if (data != NULL) {
        OK_MSG(data->length < 2u * NJ_MIDI_RATE,
               "the handle is not the song's PCM (%u bytes for %u frames)",
               (unsigned)data->length,
               (unsigned)(NJ_MIDI_RATE / 2u + 140u * (NJ_MIDI_RATE / 1000u)));
    }
}

/* A MIDlet handed a file this runtime cannot decode is told WHICH file.
 *
 * Eight corpus titles ship AMR effects and five ship MP3, and every one of
 * them was told its WAV was malformed - a message that sends whoever reads
 * the log looking for a bug in the wrong decoder.  The container is already
 * sniffed; the refusal now says so. */
static void test_media_refusal_names_the_container(nj_vm *vm)
{
    static const nj_u1 amr[8] = { '#', '!', 'A', 'M', 'R', '\n', 0x00u, 0x00u };
    static const nj_u1 mp3[6] = { 'I', 'D', '3', 0x04u, 0x00u, 0x00u };
    static const nj_u1 junk[4] = { 0x01u, 0x02u, 0x03u, 0x04u };
    static const struct {
        const nj_u1 *bytes;
        nj_u4 len;
        const char *want;
    } cases[2] = {
        { mp3,  (nj_u4)sizeof(mp3),  "MPEG" },
        { junk, (nj_u4)sizeof(junk), "not a container" }
    };
    int i;

    for (i = 0; i < 2; i++) {
        nj_ref arr = nj_byte_array_new(vm, cases[i].len);
        nj_ref in;
        nj_value a[2];
        nj_value ret;
        const char *d;

        OK_MSG(arr != NULL, "an array for case %d", i);
        if (arr == NULL) {
            return;
        }
        memcpy(arr->u.bytes, cases[i].bytes, cases[i].len);
        a[0].r = arr;
        a[1].bits = 0u;
        in = construct(vm, BAIS, "([B)V", a, "a stream over the file");
        if (nj_has_exception(vm)) {
            return;
        }

        a[0].r = in;
        a[1].r = NULL;
        ret.bits = 0u;
        (void)nj_call_static(vm, MEDIA_MGR, "createPlayer",
                             "(Ljava/io/InputStream;Ljava/lang/String;)L"
                             "javax/microedition/media/Player;", a, &ret);
        OK_MSG(nj_has_exception(vm), "createPlayer refuses case %d", i);
        d = nj_exception_describe(vm);
        OK_MSG(d != NULL && strstr(d, cases[i].want) != NULL,
               "the refusal for case %d names %s (got: %s)",
               i, cases[i].want, d != NULL ? d : "(none)");
        nj_clear_exception(vm);
    }

    /*
     * AMR used to be the first case in that list, refused by name.  With the
     * built-in decoder (NJ_ENABLE_AMR, on by default) it is not refused any
     * more: a stream that carries the magic but holds no complete frame is the
     * silent Player every unplayable file gets - a Player the MIDlet can play,
     * so there is nothing for it to catch.
     *
     * That is the whole shape of the bug this test used to encode: The Sims 3
     * creates a Player per sound effect, and a createPlayer() that threw left
     * its loader holding a null it dereferenced on the spot.  test_audio.c
     * asserts what a real AMR asset produces; this only asserts that these
     * bytes no longer throw.  Built without the decoder the old answer is the
     * right one, so it is asserted there too.
     */
    {
        nj_ref arr = nj_byte_array_new(vm, (nj_u4)sizeof(amr));
        nj_value a[2];
        nj_value ret;
        nj_ref in;

        OK_MSG(arr != NULL, "an array for the AMR case");
        if (arr == NULL) {
            return;
        }
        memcpy(arr->u.bytes, amr, sizeof(amr));
        a[0].r = arr;
        a[1].bits = 0u;
        in = construct(vm, BAIS, "([B)V", a, "a stream over the AMR file");
        if (nj_has_exception(vm)) {
            return;
        }
        a[0].r = in;
        a[1].r = NULL;
        ret.bits = 0u;
        (void)nj_call_static(vm, MEDIA_MGR, "createPlayer",
                             "(Ljava/io/InputStream;Ljava/lang/String;)L"
                             "javax/microedition/media/Player;", a, &ret);
#if NJ_ENABLE_AMR
        OK_MSG(!nj_has_exception(vm),
               "an AMR stream with no complete frame is not refused");
        nj_clear_exception(vm);
        OK_MSG(ret.r != NULL, "the AMR stream still produces a Player");
#else
        {
            const char *d = nj_exception_describe(vm);

            OK_MSG(d != NULL && strstr(d, "AMR") != NULL,
                   "with the decoder built out, AMR is refused by name (got: %s)",
                   d != NULL ? d : "(none)");
            nj_clear_exception(vm);
        }
#endif
    }
}

/* MIDP 2.0 documents the initial contents of TextBox and TextField as "the
 * text content, or null if the text is empty".  三国杀2．2美化版_1347 builds its
 * user-creation TextBox with a null text, so a runtime that rejected null made
 * the game die before its first frame. */
static void test_null_initial_text(nj_vm *vm)
{
    const char TB[] = "javax/microedition/lcdui/TextBox";
    const char TF[] = "javax/microedition/lcdui/TextField";
    const char *DESC = "(Ljava/lang/String;Ljava/lang/String;II)V";
    nj_value a[4];
    nj_ref title = nj_string_new(vm, "title");
    nj_ref tb, tf, s;

    a[0].r = title;
    a[1].r = NULL;
    a[2].i = 10;
    a[3].i = 0;

    tb = construct(vm, TB, DESC, a, "TextBox with a null text");
    if (tb != NULL) {
        s = nj_call_ref(vm, tb, TB, "getString", "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_length(s) == 0u,
               "a null initial text becomes the empty string, got %u chars",
               s ? (unsigned)nj_string_length(s) : 0u);
        a[0].r = nj_string_new(vm, "abc");
        OK(nj_call(vm, tb, TB, "setString", "(Ljava/lang/String;)V", a, NULL) == NJ_OK);
        s = nj_call_ref(vm, tb, TB, "getString", "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_equals_utf8(s, "abc"),
               "and the field is writable afterwards");
    }

    a[0].r = title;
    a[1].r = NULL;
    tf = construct(vm, TF, DESC, a, "TextField with a null text");
    if (tf != NULL) {
        s = nj_call_ref(vm, tf, TF, "getString", "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_length(s) == 0u,
               "a TextField takes a null initial text too");
    }

    /* A null string is still not a legal font or an illegal maxSize. */
    a[0].r = title;
    a[1].r = NULL;
    a[2].i = 0;
    a[3].i = 0;
    {
        nj_ref o = nj_object_new(vm, TB);
        OK(o != NULL);
        if (o != NULL) {
            OK_MSG(nj_call_special(vm, o, TB, "<init>", DESC, a, NULL) != NJ_OK,
                   "a non-positive maxSize is still rejected");
            nj_clear_exception(vm);
        }
    }
}

static void test_image_survives_collection(nj_vm *vm)
{
    static const char *IMG = "javax/microedition/lcdui/Image";
    nj_vm_stats before, after;
    nj_class *want;
    nj_value a[2];
    nj_value ret;
    int i;

    want = nj_class_lookup(vm, IMG);
    OK_MSG(want != NULL, "javax/microedition/lcdui/Image is registered");

    /*
     * Fill the bump area with garbage the collector may reclaim, so the
     * 295x279 pixel array (329 KB) cannot be placed without collecting.
     *
     * Stop while the bump still has room for the next chunk but not for the
     * array, so the array is placed by a collection rather than by the bump
     * pointer.  Stopping at a FAILED allocation would not do: that failure
     * collects already and leaves a large free block behind, and the
     * collection this test is about never happens.
     */
    for (i = 0; i < 4000; i++) {
        nj_vm_get_stats(vm, &before);
        if (before.heap_size - before.heap_used < 270000u) {
            break;
        }
        if (!nj_byte_array_new(vm, 65536u)) {
            nj_clear_exception(vm);
            break;
        }
    }
    memset(&before, 0, sizeof(before));
    nj_vm_get_stats(vm, &before);

    a[0] = vint(295);
    a[1] = vint(279);
    ret.bits = 0u;
    OK_MSG(nj_call_static(vm, IMG, "createImage",
                          "(II)Ljavax/microedition/lcdui/Image;", a, &ret) == NJ_OK,
           "Image.createImage(295,279) on a heap that needs a collection");

    memset(&after, 0, sizeof(after));
    nj_vm_get_stats(vm, &after);
    OK_MSG(after.gc_count > before.gc_count,
           "placing the pixel array really did run a collection");

    OK_MSG(ret.r != NULL, "it returned an Image");
    if (ret.r) {
        OK_MSG(ret.r->cls == want,
               "the Image still knows its class after the collection: got %p, want %p",
               (void *)ret.r->cls, (void *)want);
        OK_MSG(ret.r->kind == NJ_OBJ_PLAIN, "and it is still a plain object");
    }
    clean(vm, "Image.createImage on a full heap");
}

/*
 * A frame slot may hold the address of a block that has since been freed.
 *
 * "Inside the arena" is not enough to call that address an object: the sweep
 * clears the block's magic and reuses its first payload word as the free-list
 * link - and that word is nj_object.cls, so a collector that trusted the range
 * alone walked a link pointer as if it were a class.  tests/games/
 * 都市赛车3_10192.jar died that way inside the MIDlet's own System.gc(), with
 * cls == 0x9000000000.
 *
 * The stale pointer is manufactured here because nothing in C keeps a
 * reference to an object it has dropped: the object is allocated, dropped,
 * collected, and then written into the scanned slot region of a frame.
 */
static void test_gc_ignores_freed_slot(nj_vm *vm)
{
    static const char *IMG = "javax/microedition/lcdui/Image";
    nj_thread *th;
    nj_object *freed;
    nj_ref dead;

    OK_MSG(nj_class_lookup(vm, IMG) != NULL,
           "javax/microedition/lcdui/Image is registered");

    dead = nj_object_new(vm, IMG);
    OK_MSG(dead != NULL, "an object is allocated so it can be dropped");
    if (!dead) {
        clean(vm, "freed-slot setup");
        return;
    }
    freed = (nj_object *)(void *)dead;

    /* Nothing roots it: the next collection reclaims the block. */
    OK_MSG(nj_gc_run(vm) == NJ_OK, "the first collection runs");
    OK_MSG(freed->mark == 0u, "the dropped block is not marked any more");

    /*
     * Put the stale address where the collector scans.  A frame's slot region
     * is [locals, stack_limit); a C-bridge frame has no method, so the test
     * records the limit the way the interpreter would.
     */
    th = nj_thread_create(vm, NULL, 0);
    OK_MSG(th != NULL, "a thread to hang the stale slot on");
    if (th) {
        th->nframes = 1;
        memset(&th->frames[0], 0, sizeof(th->frames[0]));
        th->frames[0].locals = th->stack_mem;
        th->frames[0].stack = th->stack_mem + 8;
        th->frames[0].sp = th->stack_mem + 9;
        th->frames[0].stack_limit = th->stack_mem + 16;
        th->stack_mem[8].r = dead;
    }

    OK_MSG(nj_gc_run(vm) == NJ_OK, "a collection with the stale slot runs");
    OK_MSG(freed->mark == 0u,
           "the freed block is not traced as an object (mark=%u)",
           (unsigned)freed->mark);
    clean(vm, "stale pointer in a scanned slot");
}

/* UC Browser's bundled page parser calls Integer.shortValue().  Narrowing
 * keeps the low 16 bits and sign-extends them, including out-of-range ints. */
static void test_integer_short_value(nj_vm *vm)
{
    static const struct { nj_s4 value; nj_s4 expected; } cases[] = {
        { 0, 0 }, { 1, 1 }, { -1, -1 },
        { 32767, 32767 }, { 32768, -32768 },
        { 65535, -1 }, { 65536, 0 },
        { -32768, -32768 }, { -32769, 32767 }, { -65536, 0 },
        { 0x12348001, -32767 }, { 2147483647, -1 },
        { -2147483647 - 1, 0 }
    };
    unsigned i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        nj_value a[1], r;
        nj_ref o;
        a[0] = vint(cases[i].value);
        o = construct(vm, INTEGER, "(I)V", a, "Integer for shortValue");
        r.bits = 0u;
        OK_MSG(o && nj_call(vm, o, INTEGER, "shortValue", "()S", NULL, &r)
                      == NJ_OK && r.i == cases[i].expected,
               "Integer(%ld).shortValue() is %ld (got %ld)",
               (long)cases[i].value, (long)cases[i].expected, (long)r.i);
        clean(vm, "Integer.shortValue");
        OK_MSG(o && nj_call_int(vm, o, INTEGER, "intValue", "()I", NULL)
                      == cases[i].value,
               "shortValue leaves the wrapped int unchanged");
        clean(vm, "Integer after shortValue");
    }
}

/*
 * Integer.valueOf(String, int) and Long.valueOf(String)/valueOf(String, int).
 * CLDC 1.1 declares the radix forms next to parseInt/parseLong, and only the
 * parse forms existed: tests/games/QQ飞车2010_5772.jar boxes a value it parsed
 * with a radix and never instantiated for the missing method.
 */
static void test_boxing_valueof_radix(nj_vm *vm)
{
    nj_value a[2];
    nj_value r;
    nj_value r2;
    nj_ref o;

    a[0] = vref(nj_string_new_n(vm, "ff", 2));
    a[1].i = 16;
    OK_MSG(nj_call_static(vm, INTEGER, "valueOf",
                          "(Ljava/lang/String;I)Ljava/lang/Integer;", a, &r)
               == NJ_OK,
           "Integer.valueOf(\"ff\", 16) resolves");
    o = r.r;
    OK_MSG(nj_call_int(vm, o, INTEGER, "intValue", "()I", NULL) == 255,
           "and it boxes 255");

    a[0] = vref(nj_string_new_n(vm, "zz", 2));
    a[1].i = 16;
    OK_MSG(nj_call_static(vm, INTEGER, "valueOf",
                          "(Ljava/lang/String;I)Ljava/lang/Integer;", a, &r)
               != NJ_OK,
           "a digit outside the radix is a NumberFormatException");
    nj_clear_exception(vm);

    a[0] = vref(nj_string_new_n(vm, "101", 3));
    a[1].i = 2;
    OK_MSG(nj_call_static(vm, LONGW, "valueOf", "(Ljava/lang/String;I)Ljava/lang/Long;",
                          a, &r) == NJ_OK,
           "Long.valueOf(\"101\", 2) resolves");
    r2.bits = 0u;
    OK_MSG(nj_call(vm, r.r, LONGW, "longValue", "()J", NULL, &r2) == NJ_OK
               && r2.j == 5,
           "and it boxes 5");

    a[0] = vref(nj_string_new_n(vm, "42", 2));
    OK_MSG(nj_call_static(vm, LONGW, "valueOf", "(Ljava/lang/String;)Ljava/lang/Long;",
                          a, &r) == NJ_OK,
           "Long.valueOf(\"42\") resolves");
    r2.bits = 0u;
    OK_MSG(nj_call(vm, r.r, LONGW, "longValue", "()J", NULL, &r2) == NJ_OK
               && r2.j == 42,
           "and it boxes 42");
    clean(vm, "boxing valueOf with a radix");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
/* --------------------------------------------------------------- resources
 * Class.getResourceAsStream("D0") asked from package Z must find the JAR
 * entry "Z/D0": the title 蛋蛋历险记_11460 asks for its data that way and
 * retries forever when the answer is NULL. The suite builds a one-entry
 * stored ZIP in memory (the reader supports method 0) so that the test needs
 * no fixture file on disk and no fixture generator. */
static const unsigned char A_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x47, 0x00, 0x29, 0x0a, 0x00,
    0x02, 0x00, 0x03, 0x07, 0x00, 0x04, 0x0c, 0x00, 0x05, 0x00, 0x06, 0x01,
    0x00, 0x10, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f,
    0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e,
    0x69, 0x74, 0x3e, 0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x0a, 0x00, 0x02,
    0x00, 0x08, 0x0c, 0x00, 0x09, 0x00, 0x0a, 0x01, 0x00, 0x08, 0x67, 0x65,
    0x74, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x01, 0x00, 0x13, 0x28, 0x29, 0x4c,
    0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x43, 0x6c,
    0x61, 0x73, 0x73, 0x3b, 0x08, 0x00, 0x0c, 0x01, 0x00, 0x02, 0x44, 0x30,
    0x0a, 0x00, 0x0e, 0x00, 0x0f, 0x07, 0x00, 0x10, 0x0c, 0x00, 0x11, 0x00,
    0x12, 0x01, 0x00, 0x0f, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e,
    0x67, 0x2f, 0x43, 0x6c, 0x61, 0x73, 0x73, 0x01, 0x00, 0x13, 0x67, 0x65,
    0x74, 0x52, 0x65, 0x73, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x41, 0x73, 0x53,
    0x74, 0x72, 0x65, 0x61, 0x6d, 0x01, 0x00, 0x29, 0x28, 0x4c, 0x6a, 0x61,
    0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x53, 0x74, 0x72, 0x69,
    0x6e, 0x67, 0x3b, 0x29, 0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x69, 0x6f,
    0x2f, 0x49, 0x6e, 0x70, 0x75, 0x74, 0x53, 0x74, 0x72, 0x65, 0x61, 0x6d,
    0x3b, 0x0a, 0x00, 0x14, 0x00, 0x15, 0x07, 0x00, 0x16, 0x0c, 0x00, 0x17,
    0x00, 0x18, 0x01, 0x00, 0x13, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x69, 0x6f,
    0x2f, 0x49, 0x6e, 0x70, 0x75, 0x74, 0x53, 0x74, 0x72, 0x65, 0x61, 0x6d,
    0x01, 0x00, 0x04, 0x72, 0x65, 0x61, 0x64, 0x01, 0x00, 0x03, 0x28, 0x29,
    0x49, 0x0a, 0x00, 0x14, 0x00, 0x1a, 0x0c, 0x00, 0x1b, 0x00, 0x06, 0x01,
    0x00, 0x05, 0x63, 0x6c, 0x6f, 0x73, 0x65, 0x07, 0x00, 0x1d, 0x01, 0x00,
    0x13, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x54,
    0x68, 0x72, 0x6f, 0x77, 0x61, 0x62, 0x6c, 0x65, 0x08, 0x00, 0x1f, 0x01,
    0x00, 0x05, 0x2f, 0x5a, 0x2f, 0x44, 0x30, 0x07, 0x00, 0x21, 0x01, 0x00,
    0x03, 0x5a, 0x2f, 0x41, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65, 0x01,
    0x00, 0x0f, 0x4c, 0x69, 0x6e, 0x65, 0x4e, 0x75, 0x6d, 0x62, 0x65, 0x72,
    0x54, 0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x04, 0x6c, 0x6f, 0x61, 0x64,
    0x01, 0x00, 0x0d, 0x53, 0x74, 0x61, 0x63, 0x6b, 0x4d, 0x61, 0x70, 0x54,
    0x61, 0x62, 0x6c, 0x65, 0x01, 0x00, 0x07, 0x6c, 0x6f, 0x61, 0x64, 0x41,
    0x62, 0x73, 0x01, 0x00, 0x0a, 0x53, 0x6f, 0x75, 0x72, 0x63, 0x65, 0x46,
    0x69, 0x6c, 0x65, 0x01, 0x00, 0x06, 0x41, 0x2e, 0x6a, 0x61, 0x76, 0x61,
    0x00, 0x21, 0x00, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x01, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x22, 0x00, 0x00,
    0x00, 0x1d, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x2a, 0xb7,
    0x00, 0x01, 0xb1, 0x00, 0x00, 0x00, 0x01, 0x00, 0x23, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x24, 0x00,
    0x18, 0x00, 0x01, 0x00, 0x22, 0x00, 0x00, 0x00, 0x7a, 0x00, 0x02, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x1f, 0x2a, 0xb6, 0x00, 0x07, 0x12, 0x0b, 0xb6,
    0x00, 0x0d, 0x4c, 0x2b, 0xc7, 0x00, 0x05, 0x02, 0xac, 0x2b, 0xb6, 0x00,
    0x13, 0x3d, 0x2b, 0xb6, 0x00, 0x19, 0x1c, 0xac, 0x4c, 0x10, 0xfe, 0xac,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x1b, 0x00, 0x1c, 0x00, 0x10,
    0x00, 0x1a, 0x00, 0x1b, 0x00, 0x1c, 0x00, 0x02, 0x00, 0x23, 0x00, 0x00,
    0x00, 0x1e, 0x00, 0x07, 0x00, 0x00, 0x00, 0x06, 0x00, 0x0a, 0x00, 0x07,
    0x00, 0x10, 0x00, 0x08, 0x00, 0x15, 0x00, 0x09, 0x00, 0x19, 0x00, 0x0a,
    0x00, 0x1b, 0x00, 0x0b, 0x00, 0x1c, 0x00, 0x0c, 0x00, 0x25, 0x00, 0x00,
    0x00, 0x15, 0x00, 0x02, 0xfc, 0x00, 0x10, 0x07, 0x00, 0x14, 0xff, 0x00,
    0x0a, 0x00, 0x01, 0x07, 0x00, 0x20, 0x00, 0x01, 0x07, 0x00, 0x1c, 0x00,
    0x01, 0x00, 0x26, 0x00, 0x18, 0x00, 0x01, 0x00, 0x22, 0x00, 0x00, 0x00,
    0x7a, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x1f, 0x2a, 0xb6, 0x00,
    0x07, 0x12, 0x1e, 0xb6, 0x00, 0x0d, 0x4c, 0x2b, 0xc7, 0x00, 0x05, 0x02,
    0xac, 0x2b, 0xb6, 0x00, 0x13, 0x3d, 0x2b, 0xb6, 0x00, 0x19, 0x1c, 0xac,
    0x4c, 0x10, 0xfe, 0xac, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x1b,
    0x00, 0x1c, 0x00, 0x10, 0x00, 0x1a, 0x00, 0x1b, 0x00, 0x1c, 0x00, 0x02,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x07, 0x00, 0x00, 0x00, 0x12,
    0x00, 0x0a, 0x00, 0x13, 0x00, 0x10, 0x00, 0x14, 0x00, 0x15, 0x00, 0x15,
    0x00, 0x19, 0x00, 0x16, 0x00, 0x1b, 0x00, 0x17, 0x00, 0x1c, 0x00, 0x18,
    0x00, 0x25, 0x00, 0x00, 0x00, 0x15, 0x00, 0x02, 0xfc, 0x00, 0x10, 0x07,
    0x00, 0x14, 0xff, 0x00, 0x0a, 0x00, 0x01, 0x07, 0x00, 0x20, 0x00, 0x01,
    0x07, 0x00, 0x1c, 0x00, 0x01, 0x00, 0x27, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x28,
};

static void put_u16c(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put_u32c(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFFul);
    p[1] = (unsigned char)((v >> 8) & 0xFFul);
    p[2] = (unsigned char)((v >> 16) & 0xFFul);
    p[3] = (unsigned char)((v >> 24) & 0xFFul);
}

static unsigned long crc32c(const unsigned char *d, unsigned long n)
{
    unsigned long c = 0xFFFFFFFFul;
    unsigned long i;
    int k;

    for (i = 0ul; i < n; i++) {
        c ^= (unsigned long)d[i];
        for (k = 0; k < 8; k++) {
            c = (c & 1ul) ? ((c >> 1) ^ 0xEDB88320ul) : (c >> 1);
        }
    }
    return c ^ 0xFFFFFFFFul;
}

static unsigned long build_zip(unsigned char *out, unsigned long cap,
                               const char *name, const unsigned char *data,
                               unsigned long dlen)
{
    unsigned long nlen = (unsigned long)strlen(name);
    unsigned long crc = crc32c(data, dlen);
    unsigned long o = 0ul, cd, cdsize;

    if (cap < 30ul + nlen + dlen + 46ul + nlen + 22ul) {
        return 0ul;
    }
    put_u32c(out + o, 0x04034b50ul); o += 4ul;
    put_u16c(out + o, 20u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u32c(out + o, crc); o += 4ul;
    put_u32c(out + o, dlen); o += 4ul;
    put_u32c(out + o, dlen); o += 4ul;
    put_u16c(out + o, (unsigned)nlen); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    memcpy(out + o, name, (size_t)nlen); o += nlen;
    memcpy(out + o, data, (size_t)dlen); o += dlen;

    cd = o;
    put_u32c(out + o, 0x02014b50ul); o += 4ul;
    put_u16c(out + o, 20u); o += 2ul;
    put_u16c(out + o, 20u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u32c(out + o, crc); o += 4ul;
    put_u32c(out + o, dlen); o += 4ul;
    put_u32c(out + o, dlen); o += 4ul;
    put_u16c(out + o, (unsigned)nlen); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u32c(out + o, 0ul); o += 4ul;
    put_u32c(out + o, 0ul); o += 4ul;
    memcpy(out + o, name, (size_t)nlen); o += nlen;
    cdsize = o - cd;

    put_u32c(out + o, 0x06054b50ul); o += 4ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 0u); o += 2ul;
    put_u16c(out + o, 1u); o += 2ul;
    put_u16c(out + o, 1u); o += 2ul;
    put_u32c(out + o, cdsize); o += 4ul;
    put_u32c(out + o, cd); o += 4ul;
    put_u16c(out + o, 0u); o += 2ul;
    return o;
}

/*
 * The resolver has to match a Methodref by name and descriptor, not by
 * staticness.  javac cannot emit the shape (a subclass may not declare a
 * static method that hides an inherited instance method), so H_instance.class
 * and J_static.class were produced by compiling J against a static H and then
 * appending a static G()V to J with a byte-level patch:
 *
 *   class H { public void G() { } }                    // instance, in the super
 *   class J extends H { public void b(); private static void G(); }
 *
 * J.b() is `invokestatic #7 // Method G:()V` with the owner being J itself.
 * Resolving by staticness first finds H.G (an instance method) and the call
 * fails; resolving by name and descriptor finds J.G.  三国争霸之赤壁大战_11681
 * hit exactly this and printed 606 "invokestatic on an instance method" lines.
 */
static const unsigned char H_instance_class[] = {
    202,254,186,190,0,0,0,71,0,14,10,0,2,0,3,7,0,4,12,0,5,0,6,1,0,16,106,97,118,97,47,108,97,110,103,
    47,79,98,106,101,99,116,1,0,6,60,105,110,105,116,62,1,0,3,40,41,86,7,0,8,1,0,1,72,1,0,4,67,111,100,
    101,1,0,15,76,105,110,101,78,117,109,98,101,114,84,97,98,108,101,1,0,1,71,1,0,10,83,111,117,114,99,
    101,70,105,108,101,1,0,6,72,46,106,97,118,97,0,33,0,7,0,2,0,0,0,0,0,2,0,1,0,5,0,6,0,1,0,9,0,0,0,29,
    0,1,0,1,0,0,0,5,42,183,0,1,177,0,0,0,1,0,10,0,0,0,6,0,1,0,0,0,1,0,1,0,11,0,6,0,1,0,9,0,0,0,25,0,0,
    0,1,0,0,0,1,177,0,0,0,1,0,10,0,0,0,6,0,1,0,0,0,1,0,1,0,12,0,0,0,2,0,13,
};

static const unsigned char J_static_class[] = {
    202,254,186,190,0,0,0,71,0,17,10,0,2,0,3,7,0,4,12,0,5,0,6,1,0,1,72,1,0,6,60,105,110,105,116,62,1,
    0,3,40,41,86,10,0,8,0,9,7,0,10,12,0,11,0,6,1,0,1,74,1,0,1,71,1,0,4,67,111,100,101,1,0,15,76,105,110,
    101,78,117,109,98,101,114,84,97,98,108,101,1,0,1,98,1,0,10,83,111,117,114,99,101,70,105,108,101,1,
    0,6,74,46,106,97,118,97,0,33,0,8,0,2,0,0,0,0,0,3,0,1,0,5,0,6,0,1,0,12,0,0,0,29,0,1,0,1,0,0,0,5,42,
    183,0,1,177,0,0,0,1,0,13,0,0,0,6,0,1,0,0,0,1,0,1,0,14,0,6,0,1,0,12,0,0,0,28,0,0,0,1,0,0,0,4,184,0,
    7,177,0,0,0,1,0,13,0,0,0,6,0,1,0,0,0,1,0,10,0,11,0,6,0,1,0,12,0,0,0,13,0,0,0,0,0,0,0,1,177,0,0,0,
    0,0,1,0,15,0,0,0,2,0,16,
};

static void test_invokestatic_resolution(nj_vm *vm)
{
    nj_value noargs[1];
    nj_value ret;
    nj_ref obj;

    noargs[0].i = 0;
    OK(nj_class_load(vm, "H", H_instance_class, sizeof(H_instance_class)) != NULL);
    OK(nj_class_load(vm, "J", J_static_class, sizeof(J_static_class)) != NULL);
    obj = nj_object_new(vm, "J");
    OK(obj != NULL);
    OK(nj_call_special(vm, obj, "J", "<init>", "()V", NULL, NULL) == NJ_OK);
    ret.i = 1;
    OK(nj_call(vm, obj, "J", "b", "()V", noargs, &ret) == NJ_OK);
    clean(vm, "J.b() resolves invokestatic to its own static G");
}

static void test_class_relative_resource(nj_vm *vm)
{
    static const unsigned char payload[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G' };
    unsigned char zip[512];
    unsigned long zlen;
    nj_app *app;
    nj_ref inst;
    nj_value ret;

    zlen = build_zip(zip, (unsigned long)sizeof(zip), "Z/D0", payload,
                     (unsigned long)sizeof(payload));
    OK_MSG(zlen > 0ul, "the one-entry ZIP was built");
    if (zlen == 0ul) {
        return;
    }

    app = nj_app_open(vm, zip, (nj_u4)zlen);
    OK_MSG(app != NULL, "the ZIP opens as a suite");
    if (!app) {
        return;
    }
    clean(vm, "opening the resource suite");

    OK_MSG(nj_class_load(vm, "Z/A", A_class, (nj_u4)sizeof(A_class)) != NULL,
           "the package fixture Z/A loads");
    clean(vm, "loading the package fixture");

    inst = construct(vm, "Z/A", "()V", NULL, "the relative-resource fixture");
    if (inst) {
        ret.bits = 0u;
        OK_MSG(nj_call(vm, inst, "Z/A", "load", "()I", NULL, &ret) == NJ_OK,
               "Z/A.load() completes");
        OK_MSG(ret.i == (nj_s4)'A',
               "a relative name in package Z reached Z/D0 (got %ld)",
               (long)ret.i);
        clean(vm, "Z/A.load()");

        ret.bits = 0u;
        OK_MSG(nj_call(vm, inst, "Z/A", "loadAbs", "()I", NULL, &ret) == NJ_OK,
               "Z/A.loadAbs() completes");
        OK_MSG(ret.i == (nj_s4)'A',
               "the absolute name /Z/D0 reached the same entry (got %ld)",
               (long)ret.i);
        clean(vm, "Z/A.loadAbs()");
    }

    nj_app_close(vm, app);
    clean(vm, "the relative-resource fixture");
}

/* Arrays are covariant when their elements are references: int[][] IS an
 * Object[], int[] is not.  法拉利GT3－世界赛道BT版_776 casts the tables it
 * deserialises that way and would not start without it. */
static void test_array_assignability(nj_vm *vm)
{
    nj_class *int1 = nj_class_resolve(vm, "[I");
    nj_class *int2 = nj_class_resolve(vm, "[[I");
    nj_class *objarr = nj_class_resolve(vm, "[Ljava/lang/Object;");
    nj_class *strarr = nj_class_resolve(vm, "[Ljava/lang/String;");
    nj_class *object = nj_class_resolve(vm, "java/lang/Object");
    nj_class *cloneable = nj_class_resolve(vm, "java/lang/Cloneable");

    OK(int1 != NULL && int2 != NULL && objarr != NULL && strarr != NULL &&
       object != NULL);
    OK_MSG(nj_class_is_assignable(int2, objarr), "int[][] is an Object[]");
    OK_MSG(!nj_class_is_assignable(int1, objarr), "int[] is not an Object[]");
    OK_MSG(nj_class_is_assignable(int2, object), "int[][] is an Object");
    OK_MSG(nj_class_is_assignable(strarr, objarr), "String[] is an Object[]");
    OK_MSG(!nj_class_is_assignable(objarr, strarr), "Object[] is not a String[]");
    OK_MSG(!nj_class_is_assignable(strarr, int1), "String[] is not an int[]");
    OK_MSG(nj_class_is_assignable(int1, int1), "int[] is an int[]");
    if (cloneable != NULL) {
        OK_MSG(nj_class_is_assignable(int2, cloneable),
               "an array of arrays is a Cloneable");
    }
    clean(vm, "array assignability");
}

/*
 * --dump-frames takes a prefix, and "out" where out/ is a directory used to
 * write "out00001.ppm" NEXT TO it while the run still exited 0, so a script
 * that forgot the trailing slash saw a silent no-op.  A prefix that names a
 * directory gets the separator; a real prefix keeps its old meaning.
 */
static void test_dump_frames_directory(nj_vm *vm)
{
    const char *dir = "tests/out/dumpdir";
    char inside[256];
    char beside[256];
    struct stat st;

    (void)mkdir("tests/out", 0777);
    (void)mkdir(dir, 0777);
    (void)remove("tests/out/dumpdir/00001.ppm");
    (void)remove("tests/out/dumpdir00001.ppm");

    nj_port_headless_set_dump(vm->port->ud, dir, 1);
    vm->port->display_present(vm->port->ud, 0, 0, 176, 208);

    snprintf(inside, sizeof(inside), "%s/00001.ppm", dir);
    snprintf(beside, sizeof(beside), "%s00001.ppm", dir);
    OK_MSG(stat(inside, &st) == 0, "a directory prefix dumps inside it");
    OK_MSG(stat(beside, &st) != 0, "and not next to it");

    nj_port_headless_set_dump(vm->port->ud, NULL, 0);
    (void)remove(inside);
    clean(vm, "frame dump prefix");
}

/*
 * The CLDC/MIDP members tools/api_audit.py used to report as unresolvable:
 * String's double/slice valueOf and intern(), Class.isAssignableFrom,
 * Thread(String), the MIDP 2.0 four-argument Command constructor, the caret
 * reader TextBox and TextField share, Player.getContentType and RecordStore's
 * MIDP 1.0 setMode/getMode pair.
 */
static void test_njlib_gaps(nj_vm *vm)
{
    const char STR[] = "java/lang/String";
    const char CLS[] = "java/lang/Class";
    const char THR[] = "java/lang/Thread";
    const char CMD[] = "javax/microedition/lcdui/Command";
    const char TB[] = "javax/microedition/lcdui/TextBox";
    const char TF[] = "javax/microedition/lcdui/TextField";
    const char RS[] = "javax/microedition/rms/RecordStore";
    const char OPEN4[] = "(Ljava/lang/String;ZIZ)L"
                         "javax/microedition/rms/RecordStore;";
    const char CTOR_TB[] = "(Ljava/lang/String;Ljava/lang/String;II)V";
    nj_value a[4];
    nj_value ret;
    nj_ref s, arr, thr, cmd, tb, tf, rs;

    /* String.valueOf(double) has to be Double.toString's formatting - a "%g"
     * would answer "1" where Java says "1.0". */
    memset(a, 0, sizeof(a));
    a[0].d = 1.0;
    ret.bits = 0u;
    OK(nj_call_static(vm, STR, "valueOf", "(D)Ljava/lang/String;",
                      a, &ret) == NJ_OK);
    OK_MSG(ret.r != NULL && nj_string_equals_utf8(ret.r, "1.0"),
           "String.valueOf(1.0) is \"1.0\"");

    a[0].d = -2.5;
    ret.bits = 0u;
    OK(nj_call_static(vm, STR, "valueOf", "(D)Ljava/lang/String;",
                      a, &ret) == NJ_OK);
    OK_MSG(ret.r != NULL && nj_string_equals_utf8(ret.r, "-2.5"),
           "String.valueOf(-2.5) is \"-2.5\"");

    /* valueOf(char[], int, int) copies exactly the requested slice. */
    arr = nj_char_array_new(vm, 4u);
    OK(arr != NULL);
    if (arr) {
        nj_array_set_int(arr, 0u, 'a');
        nj_array_set_int(arr, 1u, 'b');
        nj_array_set_int(arr, 2u, 'c');
        nj_array_set_int(arr, 3u, 'd');
        memset(a, 0, sizeof(a));
        a[0] = vref(arr);
        a[1] = vint(1);
        a[2] = vint(2);
        ret.bits = 0u;
        OK(nj_call_static(vm, STR, "valueOf", "([CII)Ljava/lang/String;",
                          a, &ret) == NJ_OK);
        OK_MSG(ret.r != NULL && nj_string_equals_utf8(ret.r, "bc"),
               "String.valueOf({'a','b','c','d'}, 1, 2) is \"bc\"");
    }

    /* intern() answers the receiver: the runtime's literals are interned
     * already, so there is no table to consult. */
    s = nj_string_new(vm, "interned");
    OK(s != NULL);
    ret.bits = 0u;
    OK(nj_call(vm, s, STR, "intern", "()Ljava/lang/String;", NULL, &ret) ==
       NJ_OK);
    OK_MSG(ret.r == s, "String.intern() returns the receiver");

    /* Class.isAssignableFrom: the argument is the "from" side. */
    {
        nj_ref strc = nj_class_object(vm, "java/lang/String");
        nj_ref objc = nj_class_object(vm, "java/lang/Object");

        a[0] = vref(strc);
        OK_MSG(nj_call_int(vm, objc, CLS, "isAssignableFrom",
                           "(Ljava/lang/Class;)Z", a) == 1,
               "Object.class.isAssignableFrom(String.class)");
        a[0] = vref(objc);
        OK_MSG(nj_call_int(vm, strc, CLS, "isAssignableFrom",
                           "(Ljava/lang/Class;)Z", a) == 0,
               "String.class.isAssignableFrom(Object.class) is false");
    }

    /* Thread(String) stores its name the way the Runnable form does. */
    a[0] = vref(nj_string_new(vm, "worker"));
    thr = construct(vm, THR, "(Ljava/lang/String;)V", a, "Thread(\"worker\")");
    if (thr != NULL) {
        s = nj_call_ref(vm, thr, THR, "getName", "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_equals_utf8(s, "worker"),
               "new Thread(\"worker\").getName()");
    }

    /* Command(String shortLabel, String longLabel, int type, int priority) -
     * the MIDP 2.0 constructor, which is not the device's 4-arg overload. */
    a[0] = vref(nj_string_new(vm, "Go"));
    a[1] = vref(nj_string_new(vm, "Go long"));
    a[2] = vint(1);                     /* Command.SCREEN */
    a[3] = vint(7);
    cmd = construct(vm, CMD, "(Ljava/lang/String;Ljava/lang/String;II)V", a,
                    "Command(\"Go\", \"Go long\", 1, 7)");
    if (cmd != NULL) {
        s = nj_call_ref(vm, cmd, CMD, "getLabel", "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_equals_utf8(s, "Go"),
               "getLabel() is the short label");
        s = nj_call_ref(vm, cmd, CMD, "getLongLabel",
                        "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_equals_utf8(s, "Go long"),
               "getLongLabel() is the long one");
        OK_MSG(nj_call_int(vm, cmd, CMD, "getPriority", "()I", NULL) == 7,
               "and getPriority() is the fourth argument");
    }

    /* TextBox and TextField share the text machinery, so setString() leaves
     * the caret at the end of the new text. */
    a[0] = vref(nj_string_new(vm, "title"));
    a[1].r = NULL;
    a[2] = vint(10);
    a[3] = vint(0);
    tb = construct(vm, TB, CTOR_TB, a, "TextBox for the caret");
    if (tb != NULL) {
        a[0] = vref(nj_string_new(vm, "abc"));
        OK(nj_call(vm, tb, TB, "setString", "(Ljava/lang/String;)V", a,
                   NULL) == NJ_OK);
        OK_MSG(nj_call_int(vm, tb, TB, "getCaretPosition", "()I", NULL) == 3,
               "TextBox.getCaretPosition() follows the text");
    }
    a[0] = vref(nj_string_new(vm, "title"));
    a[1].r = NULL;
    a[2] = vint(10);
    a[3] = vint(0);
    tf = construct(vm, TF, CTOR_TB, a, "TextField for the caret");
    if (tf != NULL) {
        a[0] = vref(nj_string_new(vm, "abcd"));
        OK(nj_call(vm, tf, TF, "setString", "(Ljava/lang/String;)V", a,
                   NULL) == NJ_OK);
        OK_MSG(nj_call_int(vm, tf, TF, "getCaretPosition", "()I", NULL) == 4,
               "TextField.getCaretPosition() uses the same reader");
    }

    /* Player.getContentType(): a tone locator player knows its own type. */
    a[0] = vref(nj_string_new(vm, "tone://"));
    ret.bits = 0u;
    if (nj_call_static(vm, MEDIA_MGR, "createPlayer",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Player;",
                       a, &ret) == NJ_OK && ret.r != NULL) {
        s = nj_call_ref(vm, ret.r, PLAYER, "getContentType",
                        "()Ljava/lang/String;", NULL);
        OK_MSG(s != NULL && nj_string_equals_utf8(s, "audio/x-tone-seq"),
               "a tone Player reports audio/x-tone-seq");
    } else {
        g_fail++;
        fprintf(stderr, "FAIL test_lib.c: createPlayer(tone://) for "
                "getContentType\n");
        nj_clear_exception(vm);
    }

    /* RecordStore's MIDP 1.0 access mode: setMode() stores what getMode()
     * reads back, and only the two documented constants are accepted. */
    {
        nj_field *f = nj_field_lookup(vm, RS, "AUTHMODE_ANY", "I", 1);

        OK_MSG(f != NULL && nj_field_read_int(vm, NULL, f) == 1,
               "RecordStore.AUTHMODE_ANY is 1");
    }
    a[0] = vref(nj_string_new(vm, "njlib_setmode"));
    a[1] = vint(1);                     /* create if necessary */
    a[2] = vint(0);                     /* AUTHMODE_PRIVATE */
    a[3] = vint(1);                     /* writable */
    ret.bits = 0u;
    if (nj_call_static(vm, RS, "openRecordStore", OPEN4, a, &ret) == NJ_OK) {
        rs = ret.r;
        OK_MSG(nj_call_int(vm, rs, RS, "getMode", "()I", NULL) == 0,
               "a privately opened store reports AUTHMODE_PRIVATE");
        a[0] = vint(1);
        a[1] = vint(1);
        OK(nj_call(vm, rs, RS, "setMode", "(IZ)V", a, NULL) == NJ_OK);
        OK_MSG(nj_call_int(vm, rs, RS, "getMode", "()I", NULL) == 1,
               "and setMode(AUTHMODE_ANY, true) round-trips");
        a[0] = vint(9);
        a[1] = vint(1);
        OK_MSG(nj_call(vm, rs, RS, "setMode", "(IZ)V", a, NULL) != NJ_OK,
               "an authmode that is neither constant is refused");
        nj_clear_exception(vm);
    } else {
        g_fail++;
        fprintf(stderr, "FAIL test_lib.c: openRecordStore for setMode\n");
        nj_clear_exception(vm);
    }

    clean(vm, "the audited class-library members");
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    const char *rms_dir;

    printf("== nano_j2me class library test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }

    /*
     * RMS writes one file per store into nj_port.rms_dir, and the default is
     * the working directory, so the record store cases below would leave
     * njlib_overloads.rms wherever the suite happened to run.
     */
    rms_dir = getenv("NANO_J2ME_RMS");
    if (!rms_dir || !*rms_dir) {
        rms_dir = "tests/out/rms";
    }
    (void)mkdir(rms_dir, 0777);
    port.rms_dir = rms_dir;

    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    test_null_data_input(vm);
    test_round_trip(vm);
    test_read_utf_static(vm);
    test_read_fully(vm);
    test_skips(vm);
    test_write_chars(vm);
    test_media(vm);
    test_strings(vm);
    test_numbers(vm);
    test_integer_short_value(vm);
    test_float_parse(vm);
    test_boxing_valueof_radix(vm);
    test_random(vm);
    test_static_field_init(vm);
    test_rms_overloads(vm);
    test_rms_listing(&port, 0);
    test_rms_listing(&port, 1);
    test_constructor_arg_window(vm);
    test_connector_open_mode(vm);
    /* Its own 2 MB heap: the fixture deliberately fills one, and the
     * fragmentation it leaves behind would otherwise distort the
     * heap-pressure tests that follow. */
    {
        nj_vm *small = nj_vm_create(&port, 2u * 1024u * 1024u);

        OK_MSG(small != NULL, "create the allocation fixture VM");
        if (small) {
            test_alloc_gives_up(small);
            nj_vm_destroy(small);
        }
    }
    test_data_interfaces(vm);
    test_null_initial_text(vm);
    test_data_input_available(vm);
    test_baos_close_keeps_data(vm);
    test_clip_negative_is_empty(vm);
    test_image_transform_dimensions(vm);
    test_long_to_string(vm);
    test_player_survives_collection(vm);
    test_eight_bit_wav_is_widened(vm);
    test_large_wav();
    test_sixteen_bit_wav_heap();
    test_midi_player_is_pcm(vm);
    test_midi_long_song_streams(vm);
    test_midi_too_big_for_heap();
    test_media_refusal_names_the_container(vm);
    test_image_survives_collection(vm);
    test_gc_ignores_freed_slot(vm);
    test_class_relative_resource(vm);
    test_invokestatic_resolution(vm);
    test_array_assignability(vm);
    test_njlib_gaps(vm);
    test_dump_frames_directory(vm);
    /* Last, because it allocates into the shared VM and a heap-pressure
     * test above expects the heap to be where it left it. */
    test_bais_range(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
