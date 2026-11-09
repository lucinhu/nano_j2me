/* MIDP encoded Image overloads have different format-error contracts. */
#include "nj2me/nj.h"
#include "nj2me/nj_codec.h"
#include "../src/midp/nj_midp.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE "javax/microedition/lcdui/Image"
#define DESC "Ljavax/microedition/lcdui/Image;"
#define STREAM "test/ImageInput"
#define CHECK(c, msg) do { if (c) { ++passed; } else { ++failed; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, msg); } } while (0)
static unsigned passed, failed;

/* Independent 3x2 RGBA fixture: alpha 0,1,127,128,254,255. */
static const nj_u1 png[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,
    0x49,0x48,0x44,0x52,0,0,0,3,0,0,0,2,8,6,0,0,0,
    0x9d,0x74,0x66,0x1a,0,0,0,0x23,0x49,0x44,0x41,0x54,
    0x78,0x9c,0x63,0x10,0x32,0x09,0x63,0x28,0x5e,0x2d,0xc9,
    0xf8,0x4f,0xc0,0xab,0x9e,0x41,0xe0,0x7e,0x66,0xc3,0x11,
    0xa6,0xc6,0x7f,0xa6,0x3e,0x13,0xfe,0x03,0,0x69,0x44,
    0x09,0xd9,0x92,0xe5,0x12,0x2e,0,0,0,0,0x49,0x45,
    0x4e,0x44,0xae,0x42,0x60,0x82
};
static const nj_u4 argb[] = {
    0x00123456u,0x0173ab19u,0x7ffe104au,
    0x8010df69u,0xfec40281u,0xff354c90u
};

static void expect_exception(nj_vm *vm, nj_status st, const char *name)
{
    const char *actual = nj_ref_class_name(vm->exception);
    CHECK(st != NJ_OK && actual && strcmp(actual, name) == 0,
          "documented exception type");
    if (actual && strcmp(actual, name)) {
        fprintf(stderr, "  expected %s, got %s\n", name, actual);
    }
    nj_clear_exception(vm);
}

static nj_ref data_array(nj_vm *vm, const void *data, nj_u4 len, nj_u4 pad)
{
    nj_ref arr = nj_byte_array_new(vm, len + 2u * pad);
    CHECK(arr != NULL, "encoded array allocation");
    if (arr) {
        memset(nj_array_data(arr), 0xa5, nj_array_length(arr));
        if (len) { memcpy((nj_u1 *)nj_array_data(arr) + pad, data, len); }
    }
    return arr;
}

static nj_status decode_bytes(nj_vm *vm, nj_ref arr, int off, int len,
                              int legacy, nj_ref *image)
{
    nj_value a[4], r;
    a[0].r = arr; a[1].i = off; a[2].i = len; a[3].i = 0;
    r.r = NULL;
    {
        nj_status st = nj_call_static(vm, IMAGE, "createImage",
            legacy ? "([BIII)" DESC : "([BII)" DESC, a, &r);
        if (image) { *image = st == NJ_OK ? r.r : NULL; }
        return st;
    }
}

static void expect_image(nj_vm *vm, nj_ref image)
{
    nj_value a[7];
    nj_u4 mark = nj_root_mark(vm);
    nj_ref pixels;
    CHECK(image != NULL, "decoded image returned");
    if (!image) { return; }
    nj_root(vm, image);
    CHECK(nj_call_int(vm, image, IMAGE, "getWidth", "()I", NULL) == 3,
          "decoded width");
    CHECK(nj_call_int(vm, image, IMAGE, "getHeight", "()I", NULL) == 2,
          "decoded height");
    CHECK(nj_call_int(vm, image, IMAGE, "isMutable", "()Z", NULL) == 0,
          "MIDP encoded image is immutable");
    pixels = nj_root(vm, nj_int_array_new(vm, 8));
    CHECK(pixels != NULL, "readback array");
    if (pixels) {
        nj_u4 *p = (nj_u4 *)nj_array_data(pixels);
        p[0] = p[7] = 0xa55ac33cu;
        a[0].r = pixels; a[1].i = 1; a[2].i = 3;
        a[3].i = a[4].i = 0; a[5].i = 3; a[6].i = 2;
        CHECK(nj_call(vm, image, IMAGE, "getRGB", "([IIIIIII)V", a, NULL)
              == NJ_OK, "read decoded pixels");
        CHECK(memcmp(p + 1, argb, sizeof(argb)) == 0,
              "all original RGB and alpha bits preserved");
        CHECK(p[0] == 0xa55ac33cu && p[7] == 0xa55ac33cu,
              "readback padding unchanged");
    }
    expect_exception(vm, nj_call(vm, image, IMAGE, "getGraphics",
        "()Ljavax/microedition/lcdui/Graphics;", NULL, NULL),
        "java/lang/IllegalStateException");
    CHECK(nj_gc_run(vm) == NJ_OK && nj_heap_owns_object(vm, image),
          "image survives collection after decode");
    nj_root_release(vm, mark);
}

static void test_bytes(nj_vm *vm)
{
    static const int ranges[][2] = {
        {-1,1},{0,-1},{INT_MIN,INT_MAX},{INT_MAX,1},
        {1,INT_MAX},{INT_MAX,INT_MAX},{1,(int)sizeof(png)},
        {(int)sizeof(png)+1,0}
    };
    static const nj_u1 garbage[] = {1,2,3,4,5};
    nj_u4 mark = nj_root_mark(vm), i;
    nj_ref data = nj_root(vm, data_array(vm, png, sizeof(png), 0));
    nj_ref padded = nj_root(vm, data_array(vm, png, sizeof(png), 5));
    nj_ref empty = nj_root(vm, data_array(vm, NULL, 0, 0));
    nj_ref bad = nj_root(vm, data_array(vm, garbage, sizeof(garbage), 0));
    nj_ref image = NULL;
    CHECK(decode_bytes(vm, data, 0, sizeof(png), 0, &image) == NJ_OK,
          "public byte overload decodes entire array");
    expect_image(vm, image);
    CHECK(decode_bytes(vm, padded, 5, sizeof(png), 0, &image) == NJ_OK,
          "public byte overload respects exact slice");
    expect_image(vm, image);
    CHECK(memcmp((nj_u1 *)nj_array_data(padded) + 5, png, sizeof(png)) == 0 &&
          ((nj_u1 *)nj_array_data(padded))[0] == 0xa5 &&
          ((nj_u1 *)nj_array_data(padded))[sizeof(png)+9] == 0xa5,
          "decode leaves input and padding unchanged");
    expect_exception(vm, decode_bytes(vm, NULL, INT_MAX, INT_MAX, 0, NULL),
                     "java/lang/NullPointerException");
    for (i = 0; i < sizeof(ranges)/sizeof(ranges[0]); ++i) {
        expect_exception(vm, decode_bytes(vm, data, ranges[i][0], ranges[i][1],
                         0, NULL), "java/lang/ArrayIndexOutOfBoundsException");
    }
    expect_exception(vm, decode_bytes(vm, data, 0, 0, 0, NULL),
                     "java/lang/IllegalArgumentException");
    expect_exception(vm, decode_bytes(vm, data, sizeof(png), 0, 0, NULL),
                     "java/lang/IllegalArgumentException");
    expect_exception(vm, decode_bytes(vm, empty, 0, 0, 0, NULL),
                     "java/lang/IllegalArgumentException");
    CHECK(decode_bytes(vm, data, 0, 0, 1, &image) == NJ_OK,
          "legacy four-argument extension retains zero-length remainder policy");
    expect_image(vm, image);
    expect_exception(vm, decode_bytes(vm, bad, 0, sizeof(garbage), 0, NULL),
                     "java/lang/IllegalArgumentException");
    expect_exception(vm, decode_bytes(vm, data, 0, 8, 0, NULL),
                     "java/lang/IllegalArgumentException");
    expect_exception(vm, decode_bytes(vm, data, 0, 40, 0, NULL),
                     "java/lang/IllegalArgumentException");
    expect_exception(vm, decode_bytes(vm, data, INT_MIN, 0, 1, NULL),
                     "java/lang/ArrayIndexOutOfBoundsException");
    expect_exception(vm, decode_bytes(vm, data, INT_MAX, 0, 1, NULL),
                     "java/lang/ArrayIndexOutOfBoundsException");
    nj_root_release(vm, mark);
}

/* Native subclass allows injected read errors and collection during reads. */
static const nj_u1 *input;
static unsigned input_len, input_pos, read_calls, close_calls, gc_reads;
static nj_ref injected;
static int throw_at;
NJ_NATIVE_FN(input_read)
{
    (void)th; (void)args;
    ++read_calls;
    if (gc_reads && (input_pos == 0 || input_pos == 8192u)) {
        CHECK(nj_gc_run(vm) == NJ_OK, "GC inside stream read callback");
    }
    if ((int)input_pos == throw_at) { vm->exception = injected; return NJ_ERR; }
    ret->i = input_pos < input_len ? input[input_pos++] : -1;
    return NJ_OK;
}
NJ_NATIVE_FN(input_close)
{
    (void)vm; (void)th; (void)args; (void)ret;
    ++close_calls; return NJ_OK;
}
static const nj_builtin_method stream_methods[] = {
    {"read","()I",NJ_ACC_PUBLIC}, {"close","()V",NJ_ACC_PUBLIC}
};
static const nj_builtin_class stream_class = {
    STREAM,"java/io/InputStream",0,NULL,0,stream_methods,2
};
static nj_ref make_stream(nj_vm *vm, const nj_u1 *data, unsigned len)
{
    nj_class *c = nj_class_create_builtin(vm, &stream_class);
    CHECK(c != NULL, "native input class");
    if (!c) { return NULL; }
    c->methods[0].native = input_read; c->methods[1].native = input_close;
    input = data; input_len = len; input_pos = read_calls = close_calls = 0;
    throw_at = -1; gc_reads = 0;
    return nj_object_new(vm, STREAM);
}
static nj_status decode_stream(nj_vm *vm, nj_ref in, nj_ref *image)
{
    nj_value a, r; nj_status st;
    a.r = in; r.r = NULL;
    st = nj_call_static(vm, IMAGE, "createImage", "(Ljava/io/InputStream;)" DESC,
                        &a, &r);
    if (image) { *image = st == NJ_OK ? r.r : NULL; }
    return st;
}
static void test_streams(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref in, image;
    nj_u1 *large = (nj_u1 *)calloc(17000, 1);
    const char *errors[] = {"java/io/IOException","java/lang/OutOfMemoryError",
                            "java/lang/IllegalStateException"};
    unsigned i;
    expect_exception(vm, decode_stream(vm, NULL, NULL), "java/lang/NullPointerException");
    in = nj_root(vm, make_stream(vm, NULL, 0));
    expect_exception(vm, decode_stream(vm, in, NULL), "java/io/IOException");
    CHECK(close_calls == 0, "empty stream remains open");
    in = nj_root(vm, make_stream(vm, png, 8));
    expect_exception(vm, decode_stream(vm, in, NULL), "java/io/IOException");
    CHECK(close_calls == 0, "invalid stream remains open");
    for (i = 0; i < sizeof(errors)/sizeof(errors[0]); ++i) {
        in = nj_root(vm, make_stream(vm, png, sizeof(png)));
        injected = nj_root(vm, nj_object_new(vm, errors[i])); throw_at = 4;
        CHECK(decode_stream(vm, in, NULL) != NJ_OK && vm->exception == injected,
              "original read exception identity survives");
        CHECK(close_calls == 0, "stream remains open after read error");
        nj_clear_exception(vm);
    }
    in = nj_root(vm, make_stream(vm, png, sizeof(png))); gc_reads = 1;
    CHECK(decode_stream(vm, in, &image) == NJ_OK, "valid stream decode");
    expect_image(vm, image);
    CHECK(close_calls == 0, "successful stream remains open");
    if (large) {
        memcpy(large, png, sizeof(png));
        in = nj_root(vm, make_stream(vm, large, 17000)); gc_reads = 1;
        CHECK(decode_stream(vm, in, &image) == NJ_OK,
              "stream buffer grows twice without corrupting image");
        expect_image(vm, image);
        CHECK(close_calls == 0, "grown stream remains open");
        free(large);
    } else { CHECK(0, "large stream fixture allocation"); }
    nj_root_release(vm, mark);
}

/* Measure host-header overhead; fail only the selected allocation. */
static void *(*base_alloc)(void *, nj_size);
static nj_size last_size, fail_size;
static unsigned allocation_failures;
static void *fault_alloc(void *ud, nj_size n)
{
    last_size = n;
    if (fail_size && n == fail_size) {
        fail_size = 0; ++allocation_failures; return NULL;
    }
    return base_alloc ? base_alloc(ud, n) : malloc(n);
}
static nj_size measured_size(nj_vm *vm, nj_size n)
{
    void *p = nj_host_alloc(vm, n);
    nj_size size = last_size;
    CHECK(p != NULL, "allocation-size probe");
    nj_host_free(vm, p); return size;
}
static int codec_mode;
static int fake_accepts(const void *data, nj_u4 len)
{
    return len == 4 && memcmp(data, "TEST", 4) == 0;
}
static nj_image *fake_decode(const nj_codec_req *req)
{
    nj_image *img;
    if (codec_mode == 0) {
        if (req->err) { *req->err = "allocation failed is only a format message"; }
        return NULL;
    }
    if (codec_mode == 1) {
        void *p;
        fail_size = measured_size(req->vm, 123); allocation_failures = 0;
        p = req->alloc(req->ud, 123);
        if (p) { req->dealloc(req->ud, p); }
        CHECK(allocation_failures == 1, "provider host-allocation failure fired");
        if (req->err) { *req->err = "provider rejected the data"; }
        return NULL;
    }
    if (codec_mode == 2) { req->vm->exception = injected; return NULL; }
    CHECK(nj_gc_run(req->vm) == NJ_OK, "provider triggers GC");
    img = nj_codec_image_new(req, codec_mode == 5 ? 295 : 3,
                              codec_mode == 5 ? 279 : 2, NJ_PIXFMT_ARGB8888, 1);
    if (img && codec_mode != 5) { memcpy(img->pixels, argb, sizeof(argb)); }
    if (codec_mode == 4) { req->vm->exception = injected; }
    return img;
}
static const nj_codec fake_codec = {
    "image-test", {NJ_CODEC_FMT_RAW,NJ_CODEC_CAN_ARGB8888,0,0,0,0},
    fake_accepts,NULL,fake_decode,NULL,NULL,NULL
};
static void test_provider(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref data = nj_root(vm, data_array(vm, "TEST", 4, 0)), in, image;
    int stream;
    CHECK(nj_codec_register(vm, &fake_codec, NJ_CODEC_PRIO_PLATFORM) == NJ_OK,
          "install deterministic image provider");
    for (stream = 0; stream <= 1; ++stream) {
        nj_status st;
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        codec_mode = 0;
        st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, 4, 0, NULL);
        expect_exception(vm, st, stream ? "java/io/IOException" : "java/lang/IllegalArgumentException");
        codec_mode = 1;
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, 4, 0, NULL);
        expect_exception(vm, st, "java/lang/OutOfMemoryError");
        codec_mode = 2;
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        injected = nj_root(vm, nj_object_new(vm, "java/io/IOException"));
        st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, 4, 0, NULL);
        CHECK(st != NJ_OK && vm->exception == injected,
              "provider Java exception is preserved without remapping");
        nj_clear_exception(vm);
        codec_mode = 3;
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        st = stream ? decode_stream(vm, in, &image) : decode_bytes(vm, data, 0, 4, 0, &image);
        CHECK(st == NJ_OK, "custom provider decode after GC");
        expect_image(vm, image);
        codec_mode = 4;
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        injected = nj_root(vm, nj_object_new(vm, "java/lang/IllegalStateException"));
        st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, 4, 0, NULL);
        CHECK(st != NJ_OK && vm->exception == injected,
              "provider cannot hide a Java exception by returning an image");
        nj_clear_exception(vm);
    }
    nj_root_release(vm, mark);
}

static void test_root_failures(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_ref data = nj_root(vm, data_array(vm, png, sizeof(png), 0));
    nj_u1 *large = (nj_u1 *)calloc(9000, 1);
    int stream, available;
    CHECK(large != NULL, "root-growth stream fixture");
    if (!large) { nj_root_release(vm, mark); return; }
    memcpy(large, png, sizeof(png));
    for (stream = 0; stream <= 1; ++stream) {
        for (available = 0; available < 2; ++available) {
            nj_ref in = nj_root(vm, make_stream(vm, large, 9000));
            nj_u4 saved = nj_root_mark(vm), padded;
            nj_status st;
            if (vm->nroots == vm->root_cap) { nj_root(vm, NULL); }
            while (vm->nroots < vm->root_cap - (nj_u4)available) { nj_root(vm, NULL); }
            padded = nj_root_mark(vm);
            fail_size = measured_size(vm, (nj_size)vm->root_cap * 2u * sizeof(nj_ref));
            allocation_failures = 0;
            st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, sizeof(png), 0, NULL);
            fail_size = 0;
            CHECK(allocation_failures == 1, "root growth allocation failed once");
            expect_exception(vm, st, "java/lang/OutOfMemoryError");
            CHECK(nj_root_mark(vm) == padded, "all temporary roots released after failure");
            if (stream) {
                CHECK(read_calls == (available ? 8193u : 0u),
                      "no reads after unprotected stream buffer allocation");
                CHECK(close_calls == 0, "root failure leaves stream open");
            }
            nj_root_release(vm, saved);
        }
    }
    free(large); nj_root_release(vm, mark);
}

static void test_heap_pressure(nj_port *port)
{
    int stream;
    for (stream = 0; stream <= 1; ++stream) {
        nj_vm *vm = nj_vm_create(port, 1024u * 1024u);
        nj_ref data, in, image;
        nj_u4 gc;
        nj_size tail;
        CHECK(vm != NULL, "GC-pressure image VM");
        if (!vm) { continue; }
        data = nj_root(vm, data_array(vm, png, sizeof(png), 0));
        in = nj_root(vm, make_stream(vm, png, sizeof(png)));
        CHECK(decode_bytes(vm, data, 0, sizeof(png), 0, &image) == NJ_OK,
              "warm image metadata before deterministic GC pressure");
        tail = vm->heap.size - vm->heap.bump;
        CHECK(tail > NJ_BLOCK_HDR + 32u, "room to create pressure block");
        if (tail <= NJ_BLOCK_HDR + 32u) {
            nj_root_release(vm, 0); nj_vm_destroy(vm); continue;
        }
        CHECK(nj_alloc(vm, tail - NJ_BLOCK_HDR - 32u) != NULL,
              "unreachable pressure block fills remaining arena");
        gc = vm->heap.gc_count;
        CHECK((stream ? decode_stream(vm, in, &image) :
                        decode_bytes(vm, data, 0, sizeof(png), 0, &image)) == NJ_OK,
              "decode completes across arena collection");
        CHECK(vm->heap.gc_count > gc, "GC actually ran during image construction");
        expect_image(vm, image);
        nj_root_release(vm, 0); nj_vm_destroy(vm);
    }
    for (stream = 0; stream <= 1; ++stream) {
        nj_vm *vm = nj_vm_create(port, 256u * 1024u);
        nj_ref data, in;
        nj_status st;
        CHECK(vm != NULL, "small-heap image VM");
        if (!vm) { continue; }
        data = nj_root(vm, data_array(vm, "TEST", 4, 0));
        in = nj_root(vm, make_stream(vm, (const nj_u1 *)"TEST", 4));
        CHECK(nj_codec_register(vm, &fake_codec, NJ_CODEC_PRIO_PLATFORM) == NJ_OK,
              "install oversized-image provider");
        codec_mode = 5;
        st = stream ? decode_stream(vm, in, NULL) : decode_bytes(vm, data, 0, 4, 0, NULL);
        expect_exception(vm, st, "java/lang/OutOfMemoryError");
        CHECK(nj_root_mark(vm) == 2u, "failed image adoption restores temporary roots");
        nj_root_release(vm, 0); nj_vm_destroy(vm);
    }
}

int main(void)
{
    nj_port port; nj_vm *vm;
    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) { return 2; }
    base_alloc = port.alloc; port.alloc = fault_alloc;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    test_bytes(vm); test_streams(vm); test_provider(vm); test_root_failures(vm);
    CHECK(!nj_has_exception(vm), "no pending exception after image cases");
    CHECK(nj_root_mark(vm) == 0, "image calls restore roots");
    nj_vm_destroy(vm);
    test_heap_pressure(&port);
    nj_port_shutdown(&port);
    printf("Image decode: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
