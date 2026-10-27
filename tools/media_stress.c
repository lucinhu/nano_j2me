/*
 * media_stress.c - sanitizer stress driver for the MP4V video Player paths.
 *
 * Not part of the repository test suite: this is the sanitizer harness used to
 * reproduce and then confirm the defects found in the movie-Player robustness
 * pass.  It drives Manager.createPlayer / Player lifecycle / setMediaTime /
 * VideoControl through the same native entry points a MIDlet reaches, against
 * a real movie file.  Run it from the repository root; the optional third
 * argument is a mode-specific round count (life default 60).
 *
 * Build (sanitizer build of the library):
 *   cc -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer \
 *      -I src/core -I src/midp -I src/gfx -I include tools/media_stress.c \
 *      /tmp/nj-media-san/libnj2me.a -lz -lm -lcurl -o /tmp/media_stress
 */
#include "nj2me/nj.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"
#include "nj_midp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAIS      "java/io/ByteArrayInputStream"
#define MEDIA_MGR "javax/microedition/media/Manager"
#define PLAYER    "javax/microedition/media/Player"

static int checks;
static int failures;
static nj_vm *g_vm;

static void start_player(nj_vm *vm, nj_ref p);
static void pump(nj_vm *vm, int n);
static int call_void(nj_vm *vm, nj_ref p, const char *name);

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);                   \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static nj_ref load_array(nj_vm *vm, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    nj_ref bar;

    if (!f) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bar = nj_byte_array_new(vm, (nj_u4)n);
    if (!bar) {
        fclose(f);
        return NULL;
    }
    if (fread(bar->u.bytes, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    return bar;
}

static nj_ref create_player(nj_vm *vm, const char *path)
{
    nj_ref bar = load_array(vm, path);
    nj_ref in, player;
    nj_value a[2], ret;

    if (!bar) {
        return NULL;
    }
    in = nj_object_new(vm, BAIS);
    a[0].r = bar;
    if (nj_call_special(vm, in, BAIS, "<init>", "([B)V", a, NULL) != NJ_OK) {
        return NULL;
    }
    a[0].r = in;
    a[1].r = nj_string_new(vm, "video/3gpp");
    ret.bits = 0u;
    if (nj_call_static(vm, MEDIA_MGR, "createPlayer",
                       "(Ljava/io/InputStream;Ljava/lang/String;)L"
                       "javax/microedition/media/Player;", a, &ret) != NJ_OK) {
        printf("  createPlayer threw: %s\n", nj_exception_describe(vm));
        nj_clear_exception(vm);
        return NULL;
    }
    player = ret.r;
    return player;
}

/* Invoke a Player method and report whether it threw. */
static int call(nj_vm *vm, nj_ref p, const char *name, const char *sig,
                nj_value *args, nj_value *ret)
{
    int rc;

    if (ret) {
        ret->bits = 0u;
    }
    rc = nj_call(vm, p, PLAYER, name, sig, args, ret);
    if (rc != NJ_OK && nj_has_exception(vm)) {
        printf("  %s threw: %s\n", name, nj_exception_describe(vm));
        nj_clear_exception(vm);
        return 0;
    }
    return 1;
}

static int call_void(nj_vm *vm, nj_ref p, const char *name)
{
    return call(vm, p, name, "()V", NULL, NULL);
}

#define VCTL "javax/microedition/media/control/VideoControl"

/* VideoControl methods live on the control object, not on the Player. */
static int call_vc(nj_vm *vm, nj_ref vc, const char *name, const char *sig,
                   nj_value *args, nj_value *ret)
{
    int rc;

    if (ret) {
        ret->bits = 0u;
    }
    rc = nj_call(vm, vc, VCTL, name, sig, args, ret);
    if (rc != NJ_OK && nj_has_exception(vm)) {
        printf("  %s threw: %s\n", name, nj_exception_describe(vm));
        nj_clear_exception(vm);
        return 0;
    }
    return 1;
}

static int call_vc_void(nj_vm *vm, nj_ref vc, const char *name)
{
    return call_vc(vm, vc, name, "()V", NULL, NULL);
}

/* Warm up the lazily-initialised media classes once, so the host-allocation
 * baseline below measures decoder state rather than first-use class data. */
static void warm_up(nj_vm *vm, const char *path)
{
    nj_ref p = create_player(vm, path);

    if (p != NULL) {
        start_player(vm, p);
        pump(vm, 2);
        (void)call_void(vm, p, "stop");
        (void)call_void(vm, p, "deallocate");
        (void)call_void(vm, p, "close");
    }
}

static nj_s8 get_long(nj_vm *vm, nj_ref p, const char *name)
{
    nj_field *f = nj_field_lookup(vm, PLAYER, name, "J", 0);
    return f ? nj_field_read_long(vm, p, f) : -1;
}

static nj_s4 get_int(nj_vm *vm, nj_ref p, const char *name)
{
    nj_field *f = nj_field_lookup(vm, PLAYER, name, "I", 0);
    return f ? nj_field_read_int(vm, p, f) : -9999;
}

static nj_s8 seek_to(nj_vm *vm, nj_ref p, nj_s8 want)
{
    nj_value a[2], ret;     /* a long argument takes two JVM slots */
    nj_s8 got = -1;

    memset(a, 0, sizeof(a));
    a[0].j = want;
    if (call(vm, p, "setMediaTime", "(J)J", a, &ret)) {
        got = ret.j;
    }
    return got;
}

static nj_s8 media_time(nj_vm *vm, nj_ref p)
{
    nj_value ret;

    if (call(vm, p, "getMediaTime", "()J", NULL, &ret)) {
        return ret.j;
    }
    return -1;
}

static void start_player(nj_vm *vm, nj_ref p)
{
    (void)call_void(vm, p, "realize");
    (void)call_void(vm, p, "prefetch");
    (void)call_void(vm, p, "start");
}

static void pump(nj_vm *vm, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        nj_media_frame(vm);
    }
}

/* ---------------------------------------------------------------- */
/* VideoControl churn                                                */
/* ---------------------------------------------------------------- */
static nj_ref get_video_control(nj_vm *vm, nj_ref p)
{
    nj_value a[1], ret;

    a[0].r = nj_string_new(vm, "VideoControl");
    if (!call(vm, p, "getControl",
              "(Ljava/lang/String;)Ljavax/microedition/media/Control;", a,
              &ret)) {
        return NULL;
    }
    return ret.r;
}

static void stress_control(nj_vm *vm, const char *path)
{
    nj_ref p;
    nj_ref vc, canvas;
    nj_value a[3], ret;
    int w, h, sw, sh;
    nj_u4 blocks_before;
    nj_u8 host_before;

    printf("== control: %s ==\n", path);
    warm_up(vm, path);
    blocks_before = vm->host_count;
    host_before = vm->host_bytes;
    printf("  after warm-up: %u blocks\n", (unsigned)blocks_before);
    p = create_player(vm, path);
    printf("  after create: %u blocks\n", (unsigned)vm->host_count);
    CHECK(p != NULL, "create a video Player");
    if (!p) {
        return;
    }
    (void)call_void(vm, p, "realize");
    (void)call_void(vm, p, "prefetch");
    vc = get_video_control(vm, p);
    CHECK(vc != NULL, "the video Player has a VideoControl");
    if (!vc) {
        (void)call_void(vm, p, "close");
        return;
    }
    canvas = nj_object_new(vm, "javax/microedition/lcdui/Canvas");
    CHECK(canvas != NULL, "a Canvas exists");

    /* USE_GUI_PRIMITIVE is refused with IllegalArgumentException. */
    a[0].i = 0;
    a[1].r = NULL;
    if (nj_call(vm, vc, "javax/microedition/media/control/VideoControl",
                "initDisplayMode", "(ILjava/lang/Object;)Ljava/lang/Object;",
                a, &ret) != NJ_OK) {
        CHECK(nj_has_exception(vm), "initDisplayMode(0,null) throws");
        nj_clear_exception(vm);
    } else {
        CHECK(0, "initDisplayMode(0,null) must be refused");
    }
    a[0].i = 1;
    a[1].r = canvas;
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "initDisplayMode",
               "(ILjava/lang/Object;)Ljava/lang/Object;", a, &ret),
          "initDisplayMode(USE_DIRECT_VIDEO, canvas)");
    CHECK(call_vc(vm, vc, "setDisplayLocation", "(II)V", (a[0].i = -5, a[1].i = 999, a),
               NULL),
          "setDisplayLocation(-5,999) clips instead of refusing");
    a[0].i = 64;
    a[1].i = 48;
    CHECK(call_vc(vm, vc, "setDisplaySize", "(II)V", a, NULL),
          "setDisplaySize(64,48)");
    a[0].i = 0;
    a[1].i = 0;
    if (nj_call(vm, vc, "javax/microedition/media/control/VideoControl",
                "setDisplaySize", "(II)V", a, NULL) != NJ_OK &&
        nj_has_exception(vm)) {
        CHECK(1, "setDisplaySize(0,0) throws");
        nj_clear_exception(vm);
    } else {
        CHECK(0, "setDisplaySize(0,0) must be refused");
    }
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "getDisplayWidth", "()I", NULL, &ret) && ret.i == 64,
          "getDisplayWidth after setDisplaySize");
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "getDisplayHeight", "()I", NULL, &ret) && ret.i == 48,
          "getDisplayHeight after setDisplaySize");
    a[0].i = 1;
    CHECK(call_vc(vm, vc, "setDisplayFullScreen", "(Z)V", a, NULL),
          "setDisplayFullScreen(true)");
    ret.bits = 0u;
    (void)call_vc(vm, vc, "getDisplayWidth", "()I", NULL, &ret);
    w = ret.i;
    ret.bits = 0u;
    (void)call_vc(vm, vc, "getDisplayHeight", "()I", NULL, &ret);
    h = ret.i;
    CHECK(w >= 0 && h >= 0, "full-screen display size is non-negative (%dx%d)",
          w, h);
    ret.bits = 0u;
    (void)call_vc(vm, vc, "getSourceWidth", "()I", NULL, &ret);
    sw = ret.i;
    ret.bits = 0u;
    (void)call_vc(vm, vc, "getSourceHeight", "()I", NULL, &ret);
    sh = ret.i;
    CHECK(sw > 0 && sh > 0, "source size is (%dx%d)", sw, sh);

    a[0].i = 1;
    CHECK(call_vc(vm, vc, "setVisible", "(Z)V", a, NULL), "setVisible(true)");
    a[0].r = nj_string_new(vm, "RGB888");
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "getSnapshot",
               "(Ljava/lang/String;)Ljava/lang/Object;", a, &ret),
          "getSnapshot(RGB888) before the first frame");
    if (ret.r) {
        CHECK(ret.r->length == (nj_u4)(sw * sh * 3),
              "snapshot is w*h*3 bytes (%u)", (unsigned)ret.r->length);
    }
    (void)call_void(vm, p, "start");
    /* Doom's first frames are black (a fade-in), so pump enough of the intro
     * that a blank snapshot would mean a broken decode rather than dark art. */
    pump(vm, 40);
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "getSnapshot",
               "(Ljava/lang/String;)Ljava/lang/Object;", a, &ret),
          "getSnapshot(RGB888) after frames");
    if (ret.r) {
        nj_u4 i, sum = 0;
        for (i = 0; i < ret.r->length; i++) {
            sum += ret.r->u.bytes[i];
        }
        CHECK(sum != 0u, "the RGB888 snapshot is not blank");
    }
    a[0].r = nj_string_new(vm, "RGB565");
    ret.bits = 0u;
    if (nj_call(vm, vc, "javax/microedition/media/control/VideoControl",
                "getSnapshot", "(Ljava/lang/String;)Ljava/lang/Object;", a, &ret) != NJ_OK &&
        nj_has_exception(vm)) {
        CHECK(1, "an unsupported snapshot encoding throws");
        nj_clear_exception(vm);
    } else {
        CHECK(0, "an unsupported snapshot encoding must be refused");
    }
    (void)call_void(vm, p, "stop");
    a[0].r = nj_string_new(vm, "RGB888");
    ret.bits = 0u;
    CHECK(call_vc(vm, vc, "getSnapshot",
               "(Ljava/lang/String;)Ljava/lang/Object;", a, &ret),
          "getSnapshot on a stopped Player");
    (void)call_void(vm, p, "start");
    (void)call_void(vm, p, "stop");
    a[0].i = 0;
    CHECK(call_vc(vm, vc, "setVisible", "(Z)V", a, NULL), "setVisible(false)");
    (void)call_void(vm, p, "deallocate");
    (void)call_void(vm, p, "close");
    printf("  after close: %u blocks (baseline %u)\n",
           (unsigned)vm->host_count, (unsigned)blocks_before);
    /* host_bytes is the monotonic total handed out; the live count is the
     * leak counter (nj_host_free does not subtract bytes). */
    CHECK(vm->host_count == blocks_before,
          "closing the Player released every host block (%u -> %u)",
          (unsigned)blocks_before, (unsigned)vm->host_count);
}

/* ---------------------------------------------------------------- */
/* seek / getMediaTime churn                                         */
/* ---------------------------------------------------------------- */
static void stress_seek(nj_vm *vm, const char *path)
{
    nj_ref p;
    nj_s8 dur, got, now;
    nj_s4 pushed, state;
    static const nj_s8 wants[] = { 0, 500000, 2000000, -5, 15000000,
                                   0, 300000, 999999999 };
    unsigned i;
    nj_u4 blocks_before;

    printf("== seek: %s ==\n", path);
    warm_up(vm, path);
    blocks_before = vm->host_count;
    p = create_player(vm, path);
    CHECK(p != NULL, "create a video Player");
    if (!p) {
        return;
    }
    (void)call_void(vm, p, "realize");
    (void)call_void(vm, p, "prefetch");
    dur = get_long(vm, p, "pDuration");
    CHECK(dur > 0, "the Player knows its duration (%lld us)",
          (long long)dur);
    for (i = 0; i < sizeof(wants) / sizeof(wants[0]); i++) {
        got = seek_to(vm, p, wants[i]);
        now = media_time(vm, p);
        pushed = get_int(vm, p, "pPushed");
        CHECK(got >= 0 && got <= dur, "setMediaTime(%lld) -> %lld",
              (long long)wants[i], (long long)got);
        CHECK(now == got, "getMediaTime after seek returns the same position");
        CHECK(pushed >= 0, "pPushed is sane after seek (%d)", (int)pushed);
    }
    /* Seek, then start and pump: the picture must continue from the seek. */
    got = seek_to(vm, p, dur / 2);
    (void)call_void(vm, p, "start");
    pump(vm, 1);
    state = get_int(vm, p, "pState");
    CHECK(state == 400, "the Player is STARTED after a seek");
    now = media_time(vm, p);
    CHECK(now >= got - 100000, "playback resumed near the sought position");
    /* Seek while started, then stop/start again. */
    (void)seek_to(vm, p, 200000);
    (void)call_void(vm, p, "stop");
    (void)call_void(vm, p, "start");
    pump(vm, 2);
    /* Loop count churn: video ignores it, but it must not crash. */
    {
        nj_value a[1];
        a[0].i = -1;
        CHECK(call(vm, p, "setLoopCount", "(I)V", a, NULL),
              "setLoopCount(-1) on a video Player");
        a[0].i = 1;
        CHECK(call(vm, p, "setLoopCount", "(I)V", a, NULL),
              "setLoopCount(1) on a video Player");
    }
    /* Pump past the end: endOfMedia, then seek back and play again. */
    pump(vm, 100000);
    (void)call_void(vm, p, "stop");
    got = seek_to(vm, p, 0);
    CHECK(got == 0, "seek back to 0 after the movie ended");
    (void)call_void(vm, p, "start");
    pump(vm, 2);
    (void)call_void(vm, p, "deallocate");
    (void)call_void(vm, p, "close");
    CHECK(vm->host_count == blocks_before,
          "closing a seeked Player released every host block (%u -> %u)",
          (unsigned)blocks_before, (unsigned)vm->host_count);
}

/* ---------------------------------------------------------------- */
/* lifecycle loops                                                   */
/* ---------------------------------------------------------------- */
static void stress_life(nj_vm *vm, const char *path, int rounds)
{
    int i;
    nj_ref p;
    nj_u4 blocks_before;
    nj_u4 worst = 0;

    printf("== life: %s x%d ==\n", path, rounds);
    warm_up(vm, path);
    blocks_before = vm->host_count;
    for (i = 0; i < rounds; i++) {
        p = create_player(vm, path);
        CHECK(p != NULL, "create a video Player");
        if (!p) {
            return;
        }
        start_player(vm, p);
        pump(vm, 3);
        (void)call_void(vm, p, "stop");
        (void)call_void(vm, p, "start");
        pump(vm, 2);
        (void)call_void(vm, p, "stop");
        /* deallocate releases the decoder, start rebuilds it. */
        (void)call_void(vm, p, "deallocate");
        (void)call_void(vm, p, "start");
        pump(vm, 2);
        (void)call_void(vm, p, "close");
        if (vm->host_count > worst) {
            worst = vm->host_count;
        }
    }
    CHECK(vm->host_count == blocks_before,
          "every Player round-trip returns the host block count to its "
          "baseline (%u -> %u, worst %u)",
          (unsigned)blocks_before, (unsigned)vm->host_count,
          (unsigned)worst);
}

int main(int argc, char **argv)
{
    nj_port port;
    const char *mode = argc > 1 ? argv[1] : "all";
    const char *path = argc > 2 ? argv[2] : "/tmp/m4v-samples/1942.3gp";

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    port.rms_dir = "/tmp";
    g_vm = nj_vm_create(&port, 8u * 1024u * 1024u);
    if (!g_vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }
    printf("== media stress (%s, %s) ==\n", mode, path);
    if (strcmp(mode, "control") == 0 || strcmp(mode, "all") == 0) {
        stress_control(g_vm, path);
    }
    if (strcmp(mode, "seek") == 0 || strcmp(mode, "all") == 0) {
        stress_seek(g_vm, path);
    }
    if (strcmp(mode, "life") == 0 || strcmp(mode, "all") == 0) {
        stress_life(g_vm, path, 60);
    }
    nj_vm_destroy(g_vm);
    nj_port_shutdown(&port);
    printf("== %d checks, %d failure(s) ==\n", checks, failures);
    return failures ? 1 : 0;
}
