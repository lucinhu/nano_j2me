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
 *
 * Usage:  test_lib
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

#include "nj_internal.h" /* nj_class_load: the fixtures below are raw classes */

#include <stdio.h>
#include <string.h>

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

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    nj_port port;
    nj_vm *vm;

    printf("== nano_j2me class library test ==\n");

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

    test_round_trip(vm);
    test_read_fully(vm);
    test_skips(vm);
    test_media(vm);
    test_strings(vm);
    test_numbers(vm);
    test_static_field_init(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
