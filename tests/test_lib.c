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

#include "nj_internal.h" /* nj_class_load: the fixtures below are raw classes */
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
    nj_value a[1];
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

/* A DataInputStream over an existing stream. */
static nj_ref dis_over(nj_vm *vm, nj_ref in)
{
    nj_value a[1];

    a[0] = vref(in);
    return construct(vm, DIS, "(Ljava/io/InputStream;)V", a,
                     "DataInputStream(InputStream)");
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
    clean(vm, "DataInputStream reads");
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

    /* InputStream.skip, on the inherited implementation (nj_call_special
     * bypasses ByteArrayInputStream's own override, which is what a subclass
     * providing only read(byte[],int,int) would inherit). */
    bais = bais_over(vm, data, 4u);
    OK(bais != NULL);
    a[0] = vlong(2);
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
    ret.bits = 0u;
    OK(nj_call_special(vm, bais, ISTREAM, "skip", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 4, "skip(100) on 4 bytes = %lld, expected 4",
           (long long)ret.j);
    clean(vm, "InputStream.skip past the end");
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
    nj_ref player, ctl, uri;

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
    ret.bits = 0u;
    OK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.j == 5000, "Player.setMediaTime returns the time it set, got %lld",
           (long long)ret.j);

    a[0] = vlong(-7);
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
    nj_value a[1];
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
    0xCAu, 0xFEu, 0xBAu, 0xBEu, 0x00u, 0x00u, 0x00u, 0x47u, 0x00u, 0x19u, 0x0Au, 0x00u,
    0x02u, 0x00u, 0x03u, 0x07u, 0x00u, 0x04u, 0x0Cu, 0x00u, 0x05u, 0x00u, 0x06u, 0x01u,
    0x00u, 0x10u, 0x6Au, 0x61u, 0x76u, 0x61u, 0x2Fu, 0x6Cu, 0x61u, 0x6Eu, 0x67u, 0x2Fu,
    0x4Fu, 0x62u, 0x6Au, 0x65u, 0x63u, 0x74u, 0x01u, 0x00u, 0x06u, 0x3Cu, 0x69u, 0x6Eu,
    0x69u, 0x74u, 0x3Eu, 0x01u, 0x00u, 0x03u, 0x28u, 0x29u, 0x56u, 0x03u, 0x00u, 0x16u,
    0xE3u, 0x60u, 0x03u, 0x00u, 0x18u, 0x6Au, 0x00u, 0x0Au, 0x00u, 0x0Au, 0x00u, 0x0Bu,
    0x07u, 0x00u, 0x0Cu, 0x0Cu, 0x00u, 0x0Du, 0x00u, 0x0Eu, 0x01u, 0x00u, 0x01u, 0x4Fu,
    0x01u, 0x00u, 0x08u, 0x66u, 0x72u, 0x61u, 0x67u, 0x6Du, 0x65u, 0x6Eu, 0x74u, 0x01u,
    0x00u, 0x03u, 0x28u, 0x29u, 0x49u, 0x07u, 0x00u, 0x10u, 0x01u, 0x00u, 0x1Au, 0x6Au,
    0x61u, 0x76u, 0x61u, 0x2Fu, 0x6Cu, 0x61u, 0x6Eu, 0x67u, 0x2Fu, 0x4Fu, 0x75u, 0x74u,
    0x4Fu, 0x66u, 0x4Du, 0x65u, 0x6Du, 0x6Fu, 0x72u, 0x79u, 0x45u, 0x72u, 0x72u, 0x6Fu,
    0x72u, 0x01u, 0x00u, 0x04u, 0x43u, 0x6Fu, 0x64u, 0x65u, 0x01u, 0x00u, 0x0Fu, 0x4Cu,
    0x69u, 0x6Eu, 0x65u, 0x4Eu, 0x75u, 0x6Du, 0x62u, 0x65u, 0x72u, 0x54u, 0x61u, 0x62u,
    0x6Cu, 0x65u, 0x01u, 0x00u, 0x07u, 0x74u, 0x72u, 0x79u, 0x46u, 0x72u, 0x61u, 0x67u,
    0x01u, 0x00u, 0x0Du, 0x53u, 0x74u, 0x61u, 0x63u, 0x6Bu, 0x4Du, 0x61u, 0x70u, 0x54u,
    0x61u, 0x62u, 0x6Cu, 0x65u, 0x01u, 0x00u, 0x05u, 0x61u, 0x6Cu, 0x6Cu, 0x6Fu, 0x63u,
    0x01u, 0x00u, 0x04u, 0x28u, 0x49u, 0x29u, 0x49u, 0x01u, 0x00u, 0x0Au, 0x53u, 0x6Fu,
    0x75u, 0x72u, 0x63u, 0x65u, 0x46u, 0x69u, 0x6Cu, 0x65u, 0x01u, 0x00u, 0x06u, 0x4Fu,
    0x2Eu, 0x6Au, 0x61u, 0x76u, 0x61u, 0x00u, 0x21u, 0x00u, 0x0Au, 0x00u, 0x02u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x01u, 0x00u, 0x05u, 0x00u, 0x06u, 0x00u,
    0x01u, 0x00u, 0x11u, 0x00u, 0x00u, 0x00u, 0x1Du, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u,
    0x00u, 0x00u, 0x05u, 0x2Au, 0xB7u, 0x00u, 0x01u, 0xB1u, 0x00u, 0x00u, 0x00u, 0x01u,
    0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x06u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x01u,
    0x00u, 0x09u, 0x00u, 0x0Du, 0x00u, 0x0Eu, 0x00u, 0x01u, 0x00u, 0x11u, 0x00u, 0x00u,
    0x00u, 0x33u, 0x00u, 0x01u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x0Fu, 0x12u, 0x07u,
    0xBCu, 0x08u, 0x4Bu, 0x01u, 0x4Bu, 0x12u, 0x08u, 0xBCu, 0x08u, 0x4Cu, 0x2Bu, 0xBEu,
    0xACu, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x12u, 0x00u,
    0x04u, 0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x05u, 0x00u, 0x04u, 0x00u, 0x07u, 0x00u,
    0x05u, 0x00u, 0x0Cu, 0x00u, 0x06u, 0x00u, 0x09u, 0x00u, 0x13u, 0x00u, 0x0Eu, 0x00u,
    0x01u, 0x00u, 0x11u, 0x00u, 0x00u, 0x00u, 0x3Bu, 0x00u, 0x01u, 0x00u, 0x01u, 0x00u,
    0x00u, 0x00u, 0x07u, 0xB8u, 0x00u, 0x09u, 0xACu, 0x4Bu, 0x02u, 0xACu, 0x00u, 0x01u,
    0x00u, 0x00u, 0x00u, 0x03u, 0x00u, 0x04u, 0x00u, 0x0Fu, 0x00u, 0x02u, 0x00u, 0x12u,
    0x00u, 0x00u, 0x00u, 0x0Eu, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x00u, 0x04u,
    0x00u, 0x0Bu, 0x00u, 0x05u, 0x00u, 0x0Cu, 0x00u, 0x14u, 0x00u, 0x00u, 0x00u, 0x06u,
    0x00u, 0x01u, 0x44u, 0x07u, 0x00u, 0x0Fu, 0x00u, 0x09u, 0x00u, 0x15u, 0x00u, 0x16u,
    0x00u, 0x01u, 0x00u, 0x11u, 0x00u, 0x00u, 0x00u, 0x23u, 0x00u, 0x01u, 0x00u, 0x02u,
    0x00u, 0x00u, 0x00u, 0x07u, 0x1Au, 0xBCu, 0x08u, 0x4Cu, 0x2Bu, 0xBEu, 0xACu, 0x00u,
    0x00u, 0x00u, 0x01u, 0x00u, 0x12u, 0x00u, 0x00u, 0x00u, 0x0Au, 0x00u, 0x02u, 0x00u,
    0x00u, 0x00u, 0x10u, 0x00u, 0x04u, 0x00u, 0x11u, 0x00u, 0x01u, 0x00u, 0x17u, 0x00u,
    0x00u, 0x00u, 0x02u, 0x00u, 0x18u,
};

static void test_constructor_arg_window(nj_vm *vm)
{
    nj_value a[1];
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
    nj_value a[1];
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

    /* Connector.READ == 1, timeouts == true */
    a[0] = vref(nj_string_new(vm, "http://example.invalid/data"));
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
    nj_value a[1];
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

    test_round_trip(vm);
    test_read_fully(vm);
    test_skips(vm);
    test_media(vm);
    test_strings(vm);
    test_numbers(vm);
    test_boxing_valueof_radix(vm);
    test_static_field_init(vm);
    test_rms_overloads(vm);
    test_constructor_arg_window(vm);
    test_connector_open_mode(vm);
    test_alloc_gives_up(vm);
    test_data_interfaces(vm);
    test_null_initial_text(vm);
    test_data_input_available(vm);
    test_baos_close_keeps_data(vm);
    test_clip_negative_is_empty(vm);
    test_image_transform_dimensions(vm);
    test_player_survives_collection(vm);
    test_image_survives_collection(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
