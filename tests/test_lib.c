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
 *
 * Usage:  test_lib
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

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

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
