/* Reusing a sampled Player must produce the same PCM, including after EOF. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdio.h>
#include <string.h>

#define PLAYER "javax/microedition/media/Player"
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures, written, channels, limit;
static nj_s2 output[512];

static int open_pcm(void *ud, int rate, int ch)
{ (void)ud; CHECK(rate == 8000); channels = ch; return 0; }
static int room_pcm(void *ud) { (void)ud; return 256; }
static int write_pcm(void *ud, const nj_s2 *data, int frames)
{
    (void)ud;
    if (limit && frames > limit) frames = limit;
    CHECK((written + frames) * channels <= 512);
    if ((written + frames) * channels > 512) return -1;
    memcpy(output + written * channels, data, (size_t)frames * channels * 2u);
    written += frames;
    return frames;
}
static void close_pcm(void *ud) { (void)ud; }
static void pump(nj_vm *vm, nj_ref player)
{
    nj_value ret;
    int i;
    for (i = 0; i < 128; i++) {
        nj_media_frame(vm);
        CHECK(nj_call(vm, player, PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
        if (ret.i != 400) break;
    }
    CHECK(ret.i == 300 && i < 128);
}
static void check_samples(int start, int loops)
{
    int i, ch, count = 32 * loops - start;
    CHECK(written == count);
    if (written != count) return;
    for (i = 0; i < count; i++) for (ch = 0; ch < channels; ch++) {
        nj_s2 want = (nj_s2)(1000 + ((i + start) % 32) * 100 + ch * 50);
        CHECK(output[i * channels + ch] == want);
    }
}
static void run_case(int ch, int chunk)
{
    nj_u1 wav[44 + 32 * 4] = {
        'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
        16,0,0,0,1,0,0,0,0x40,0x1f,0,0,0,0,0,0,0,0,16,0,
        'd','a','t','a',0,0,0,0
    };
    nj_port port;
    nj_vm *vm;
    nj_ref bytes, stream, player;
    nj_value a[2], ret;
    int i, round, size = 32 * ch * 2;
    wav[4] = (nj_u1)(36 + size); wav[22] = (nj_u1)ch;
    wav[28] = (nj_u1)(8000 * ch * 2); wav[29] = (nj_u1)((8000 * ch * 2) >> 8);
    wav[32] = (nj_u1)(ch * 2); wav[40] = (nj_u1)size;
    for (i = 0; i < 32 * ch; i++) {
        int sample = 1000 + (i / ch) * 100 + (i % ch) * 50;
        wav[44 + i * 2] = (nj_u1)sample;
        wav[45 + i * 2] = (nj_u1)(sample >> 8);
    }
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.pcm_open = open_pcm; port.pcm_write = write_pcm;
    port.pcm_free = room_pcm; port.pcm_close = close_pcm;
    vm = nj_vm_create(&port, 1024 * 1024);
    CHECK(vm != NULL);
    if (!vm) { nj_port_shutdown(&port); return; }
    bytes = nj_root(vm, nj_byte_array_new(vm, (nj_u4)(44 + size)));
    CHECK(bytes != NULL);
    if (!bytes) goto done;
    memcpy(nj_array_data(bytes), wav, (size_t)(44 + size));
    stream = nj_root(vm, nj_object_new(vm, "java/io/ByteArrayInputStream"));
    a[0].r = bytes;
    CHECK(nj_call_special(vm, stream, "java/io/ByteArrayInputStream", "<init>", "([B)V", a, NULL) == NJ_OK);
    a[0].r = stream; a[1].r = NULL;
    CHECK(nj_call_static(vm, "javax/microedition/media/Manager", "createPlayer",
        "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;", a, &ret) == NJ_OK);
    player = nj_root(vm, ret.r);
    CHECK(player != NULL);
    if (!player) goto done;
    limit = chunk;
    for (round = 0; round < 3; round++) {
        written = 0;
        /* A completed Player rewinds automatically, even without a seek. */
        CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(vm, player, PLAYER, "prefetch", "()V", NULL, NULL) == NJ_OK);
        CHECK(nj_call(vm, player, PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
        CHECK(ret.i == 400);
        pump(vm, player); check_samples(0, 1);
    }
    /* Seeking changes the sample offset, not just the reported time. */
    for (round = 0; round < 2; round++) {
        int start = round ? 0 : 8;
        written = 0; a[0].j = start * 125;
        CHECK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
        CHECK(ret.j == a[0].j);
        CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        if (chunk) {
            CHECK(nj_call(vm, player, PLAYER, "stop", "()V", NULL, NULL) == NJ_OK);
            CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        }
        pump(vm, player); check_samples(start, 1);
    }
    /* Clamp both ends without overflowing the frame conversion. */
    a[0].j = -1;
    CHECK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    CHECK(ret.j == 0);
    a[0].j = 0x7fffffffffffffffLL;
    CHECK(nj_call(vm, player, PLAYER, "setMediaTime", "(J)J", a, &ret) == NJ_OK);
    CHECK(ret.j == 4000);
    written = 0; a[0].i = 2;
    CHECK(nj_call(vm, player, PLAYER, "setLoopCount", "(I)V", a, NULL) == NJ_OK);
    CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    pump(vm, player); check_samples(0, 2);
    CHECK(nj_call(vm, player, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
done:
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
/* A platform with one MIDI voice reports a displaced player separately
 * from normal EOF, so finite loops cannot steal the output back. */
typedef struct { int state, starts; } midi_slot;
static midi_slot midi_slots[2];
static int midi_count, midi_active, midi_fail;
static void *mock_midi_open(void *ud, const void *data, nj_u4 size, nj_u4 *duration)
{
    (void)ud; (void)data; (void)size; *duration = 1000;
    return midi_count < 2 ? &midi_slots[midi_count++] : NULL;
}
static int mock_midi_start(void *ud, void *handle, int loop)
{
    midi_slot *slot = handle; (void)ud; (void)loop;
    if (midi_fail) return -1;
    if (midi_active >= 0 && &midi_slots[midi_active] != slot)
        midi_slots[midi_active].state = -1;
    midi_active = (int)(slot - midi_slots);
    slot->starts++; slot->state = 0;
    return 0;
}
static int mock_midi_finished(void *ud, void *handle)
{ (void)ud; return ((midi_slot *)handle)->state; }
static void mock_midi_close(void *ud, void *handle) { (void)ud; (void)handle; }
static void test_midi_preemption(int loops)
{
    static const nj_u1 song[] = {
        'M','T','h','d',0,0,0,6,0,0,0,1,0,96,
        'M','T','r','k',0,0,0,12,0,0x90,60,64,96,0x80,60,0,0,0xff,0x2f,0
    };
    nj_port port; nj_vm *vm; nj_ref players[2]; nj_value a[2], ret; int i;
    memset(midi_slots, 0, sizeof(midi_slots));
    midi_active = -1; midi_count = midi_fail = 0;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.midi_open = mock_midi_open; port.midi_start = mock_midi_start;
    port.midi_finished = mock_midi_finished; port.midi_close = mock_midi_close;
    vm = nj_vm_create(&port, 1024 * 1024);
    CHECK(vm != NULL); if (!vm) { nj_port_shutdown(&port); return; }
    for (i = 0; i < 2; ++i) {
        nj_ref data = nj_root(vm, nj_byte_array_new(vm, sizeof(song)));
        nj_ref stream = nj_root(vm, nj_object_new(vm, "java/io/ByteArrayInputStream"));
        memcpy(nj_array_data(data), song, sizeof(song)); a[0].r = data;
        CHECK(nj_call_special(vm, stream, "java/io/ByteArrayInputStream", "<init>", "([B)V", a, NULL) == NJ_OK);
        a[0].r = stream; a[1].r = NULL;
        CHECK(nj_call_static(vm, "javax/microedition/media/Manager", "createPlayer",
            "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;", a, &ret) == NJ_OK);
        players[i] = nj_root(vm, ret.r);
    }
    CHECK(midi_count == 2);
    a[0].i = loops;
    CHECK(nj_call(vm, players[0], PLAYER, "setLoopCount", "(I)V", a, NULL) == NJ_OK);
    CHECK(nj_call(vm, players[0], PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    CHECK(nj_call(vm, players[1], PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    nj_media_frame(vm);
    CHECK(nj_call(vm, players[0], PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
    CHECK(ret.i == 300 && midi_slots[0].starts == 1 && midi_active == 1);
    CHECK(nj_call(vm, players[1], PLAYER, "getState", "()I", NULL, &ret) == NJ_OK);
    CHECK(ret.i == 400);
    a[0].i = 1;
    CHECK(nj_call(vm, players[0], PLAYER, "setLoopCount", "(I)V", a, NULL) == NJ_OK);
    CHECK(nj_call(vm, players[0], PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
    nj_media_frame(vm);
    CHECK(midi_slots[0].starts == 2 && midi_active == 0);
    midi_slots[0].state = 1; nj_media_frame(vm);
    CHECK(nj_call(vm, players[0], PLAYER, "getState", "()I", NULL, &ret) == NJ_OK && ret.i == 300);
    midi_fail = 1;
    CHECK(nj_call(vm, players[0], PLAYER, "start", "()V", NULL, NULL) != NJ_OK);
    vm->exception = NULL;
    CHECK(nj_call(vm, players[0], PLAYER, "getState", "()I", NULL, &ret) == NJ_OK && ret.i == 300);
    midi_fail = 0;
    for (i = 0; i < 2; ++i) CHECK(nj_call(vm, players[i], PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
int main(void)
{
    run_case(1, 0); run_case(2, 0); run_case(1, 3); run_case(2, 3);
    test_midi_preemption(1); test_midi_preemption(3); test_midi_preemption(-1);
    printf("media replay: %d failures\n", failures);
    return failures ? 1 : 0;
}
