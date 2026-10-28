/* Compare complete MIDI output with a port that accepts irregular prefixes,
 * occasionally refuses a write, and advertises more room than it can take. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include "nj_midi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER "javax/microedition/media/Player"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;
static struct {
    nj_s2 samples[120000];
    int frames, channels, calls, closes, bounded;
    int buffered, queued, capacity;
} sink;

static int pcm_open(void *ud, int rate, int channels)
{
    (void)ud;
    CHECK(rate == (int)NJ_MIDI_RATE);
    CHECK(channels == 1 || channels == 2);
    sink.channels = channels;
    return 0;
}
static int pcm_free(void *ud)
{
    (void)ud;
    return sink.buffered ? sink.capacity - sink.queued : 1024;
}
static int pcm_queued(void *ud) { (void)ud; return sink.queued; }
static int pcm_write(void *ud, const nj_s2 *samples, int frames)
{
    static const int limits[] = { 1, 17, 0, 73, 257, 5, 0, 512 };
    int n = frames;
    (void)ud;
    sink.calls++;
    if (sink.bounded) {
        if (sink.calls == 1) return 0;
        if (sink.calls == 2) return -1;
        if (sink.calls == 3) return frames + 1; /* invalid port result */
        if (n > limits[sink.calls % 8]) n = limits[sink.calls % 8];
    }
    if (sink.buffered && n > sink.capacity - sink.queued)
        n = sink.capacity - sink.queued;
    CHECK(n >= 0 && (sink.frames + n) * sink.channels <= 120000);
    if (n < 0 || (sink.frames + n) * sink.channels > 120000) return 0;
    memcpy(sink.samples + sink.frames * sink.channels, samples,
           (size_t)n * sink.channels * sizeof(*samples));
    sink.frames += n;
    if (sink.buffered) sink.queued += n;
    return n;
}
static void pcm_close(void *ud)
{
    (void)ud;
    sink.closes++;
    /* Only heard samples survive a close, exactly as on a real device. */
    sink.frames -= sink.queued;
    sink.queued = 0;
}
static void *allocate(void *ud, nj_size n) { (void)ud; return malloc(n); }
static void release(void *ud, void *p) { (void)ud; free(p); }

static void check_playback(nj_vm *vm, nj_ref player, const nj_u1 *expected,
                           nj_u4 frames, nj_u4 channels, nj_u4 start, int loops)
{
    nj_value ret;
    int i;
    for (i = 0; i < 3000; i++) {
        if (sink.buffered) sink.queued -= sink.queued < 137 ? sink.queued : 137;
        nj_media_frame(vm);
        CHECK(nj_call(vm, player, PLAYER, "getMediaTime", "()J", NULL, &ret) == NJ_OK);
        {
            nj_u4 position = start + (nj_u4)(sink.frames - sink.queued);
            if (loops > 1) position = position < frames * (nj_u4)loops ? position % frames : frames;
            CHECK(ret.j == (nj_s8)position * 1000000 / NJ_MIDI_RATE);
        }
        CHECK(nj_call(vm, player, PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
        if (ret.i != 400) break;
    }
    CHECK(ret.i == 300 && i < 3000);
    CHECK(sink.channels == (int)channels && sink.frames == (int)(frames * loops - start));
    if (sink.frames == (int)(frames * loops - start)) {
        nj_u4 j;
        for (j = 0; j < (nj_u4)sink.frames; j++) {
            if (memcmp(sink.samples + j * channels,
                       expected + ((start + j) % frames) * channels * 2u,
                       channels * 2u) != 0) {
                CHECK(0 && "PCM differs from the full renderer");
                break;
            }
        }
    }
}

static void run_case(int stereo, int bounded, int seek_pending, int pause_pending,
                     int loops, int queued_mode)
{
    static const nj_u1 mono[] = {
        'M','T','h','d',0,0,0,6,0,0,0,1,1,224,
        'M','T','r','k',0,0,0,23,
        0,255,81,3,7,161,32, 0,192,40, 0,144,69,100,
        131,96,128,69,64, 0,255,47,0
    };
    nj_u1 smf[sizeof(mono) + 4];
    nj_u4 size = sizeof(mono), frames = 0, channels = 0, start = 0;
    nj_midi_mem mem = { NULL, allocate, release };
    nj_u1 *expected;
    nj_port port;
    nj_vm *vm;
    nj_ref bytes, stream, player;
    nj_value a[2], ret;

    memcpy(smf, mono, sizeof(mono));
    if (stereo) {
        /* CC10 before the note selects the interleaved stereo renderer. */
        memmove(smf + 26, smf + 22, sizeof(mono) - 22);
        smf[22] = 0; smf[23] = 176; smf[24] = 10; smf[25] = 24;
        smf[21] += 4;
        size += 4;
    }
    expected = nj_midi_render_ex(smf, size, &frames, &channels, &mem);
    CHECK(expected != NULL && frames > 4096 && frames * channels <= 120000);
    if (!expected) return;
    memset(&sink, 0, sizeof(sink));
    sink.bounded = bounded;
    sink.buffered = queued_mode != 0;
    sink.capacity = queued_mode == 1 ? 1024 : 6000;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.pcm_open = pcm_open; port.pcm_write = pcm_write;
    port.pcm_free = pcm_free; port.pcm_close = pcm_close;
    if (sink.buffered) port.pcm_queued = pcm_queued;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) { free(expected); nj_port_shutdown(&port); return; }
    bytes = nj_root(vm, nj_byte_array_new(vm, size));
    CHECK(bytes != NULL);
    if (!bytes) goto cleanup;
    memcpy(nj_array_data(bytes), smf, size);
    stream = nj_root(vm, nj_object_new(vm, "java/io/ByteArrayInputStream"));
    a[0].r = bytes;
    CHECK(nj_call_special(vm, stream, "java/io/ByteArrayInputStream", "<init>",
                         "([B)V", a, NULL) == NJ_OK);
    a[0].r = stream; a[1].r = NULL;
    CHECK(nj_call_static(vm, "javax/microedition/media/Manager", "createPlayer",
        "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
        a, &ret) == NJ_OK);
    player = nj_root(vm, ret.r);
    CHECK(player != NULL);
    if (!player) goto cleanup;
    a[0].i = loops;
    CHECK(nj_call(vm, player, PLAYER, "setLoopCount", "(I)V", a, NULL) == NJ_OK);
    CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    if (seek_pending) {
        /* start() rendered a block but the first write accepted nothing. */
        a[0].j = 100000;
        CHECK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
        CHECK(ret.j == 100000);
        start = NJ_MIDI_RATE / 10u;
    }
    if (pause_pending) {
        CHECK(nj_call(vm, player, PLAYER, "stop", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    }
    if (queued_mode) {
        int i, heard;
        nj_media_frame(vm);                /* fill the device, beyond scratch */
        if (queued_mode == 4) {
            for (i = 0; i < 100 && sink.frames < (int)frames; i++) {
                sink.queued -= sink.queued < 1024 ? sink.queued : 1024;
                nj_media_frame(vm);
            }
            CHECK(i < 100 && sink.queued > 0);
        } else {
            sink.queued -= 123;
            nj_media_frame(vm);            /* replace scratch, but not the queue */
        }
        heard = sink.frames - sink.queued;
        CHECK(nj_call(vm, player, PLAYER, "getMediaTime", "()J", NULL, &ret) == NJ_OK);
        CHECK(ret.j == (nj_s8)heard * 1000000 / NJ_MIDI_RATE);
        if (queued_mode == 3) {
            nj_ref other;
            CHECK(nj_call(vm, stream, "java/io/ByteArrayInputStream", "reset",
                          "()V", NULL, NULL) == NJ_OK);
            a[0].r = stream; a[1].r = NULL;
            CHECK(nj_call_static(vm, "javax/microedition/media/Manager", "createPlayer",
                "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
                a, &ret) == NJ_OK);
            other = nj_root(vm, ret.r);
            CHECK(other != NULL);
            if (other) {
                CHECK(nj_call(vm, other, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
                CHECK(nj_call(vm, other, PLAYER, "stop", "()V", NULL, NULL) == NJ_OK);
                CHECK(nj_call(vm, other, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
            }
        } else {
            CHECK(nj_call(vm, player, PLAYER, "stop", "()V", NULL, NULL) == NJ_OK);
            CHECK(nj_call(vm, player, PLAYER, "getMediaTime", "()J", NULL, &ret) == NJ_OK);
            CHECK(ret.j == (nj_s8)heard * 1000000 / NJ_MIDI_RATE);
            CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        }
    }
    check_playback(vm, player, expected, frames, channels, start, loops);
    CHECK(sink.closes == 1 + pause_pending + seek_pending +
                         (queued_mode == 3 ? 2 : queued_mode != 0));
    /* The same Player remains seekable after reaching the end. */
    memset(&sink, 0, sizeof(sink));
    sink.bounded = bounded;
    a[0].j = 0;
    CHECK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    CHECK(ret.j == 0);
    CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    check_playback(vm, player, expected, frames, channels, 0, loops);
    CHECK(sink.closes == 1);
    CHECK(nj_call(vm, player, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
    CHECK(!nj_has_exception(vm));
cleanup:
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    free(expected);
}

int main(void)
{
    int stereo;
    for (stereo = 0; stereo <= 1; stereo++) {
        run_case(stereo, 0, 0, 0, 1, 0);
        run_case(stereo, 1, 0, 0, 1, 0);
        run_case(stereo, 1, 1, 0, 1, 0);
        run_case(stereo, 1, 0, 1, 1, 0);
        run_case(stereo, 0, 0, 0, 3, 0);
        run_case(stereo, 1, 0, 0, 3, 0);
        run_case(stereo, 0, 0, 0, 1, 1);    /* retained scratch on pause */
        run_case(stereo, 0, 0, 0, 1, 2);    /* seek back past the scratch */
        run_case(stereo, 0, 0, 0, 1, 3);    /* another Player takes the device */
        run_case(stereo, 0, 0, 0, 3, 4);    /* pause across a loop boundary */
    }
    printf("media backpressure: %d failures\n", failures);
    return failures ? 1 : 0;
}
