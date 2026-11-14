/* Real Manager/Player regression for IMA WAV frame counts and LR interleaving.
 * Expected PCM is complete independent FFmpeg output from original tiny tones;
 * no runtime decoder function or copied decoding algorithm is the oracle. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include "private/wav_stereo_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER "javax/microedition/media/Player"
#define STREAM "java/io/ByteArrayInputStream"
#define MANAGER "javax/microedition/media/Manager"
static unsigned checks, cases;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
static struct {
    nj_s2 samples[2048];
    unsigned frames;
    int channels, opens, closes, bounded;
} sink;

static int open_pcm(void *ud, int rate, int channels)
{
    (void)ud;
    CHECK(rate == 8000);
    CHECK(channels == 1 || channels == 2);
    sink.channels = channels;
    ++sink.opens;
    return 0;
}
static int room_pcm(void *ud) { (void)ud; return 1024; }
static int write_pcm(void *ud, const nj_s2 *samples, int frames)
{
    (void)ud;
    CHECK(frames >= 0 && sink.channels > 0);
    if (sink.bounded && frames > 7) frames = 7;
    CHECK((sink.frames + (unsigned)frames) * (unsigned)sink.channels <= 2048u);
    memcpy(sink.samples + sink.frames * (unsigned)sink.channels, samples,
           (size_t)frames * (unsigned)sink.channels * sizeof(*samples));
    sink.frames += (unsigned)frames;
    return frames;
}
static void close_pcm(void *ud) { (void)ud; ++sink.closes; }

static nj_u4 le32(const unsigned char *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) | ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}
static nj_u4 chunk(const unsigned char *bytes, nj_u4 length, const char *name)
{
    nj_u4 p = 12;
    while (p <= length && length - p >= 8) {
        nj_u4 size = le32(bytes + p + 4);
        if (memcmp(bytes + p, name, 4) == 0) return p + 8;
        CHECK(size <= length - p - 8);
        p += 8 + size + (size & 1u);
    }
    CHECK(0 && "fixture chunk missing");
    return 0;
}
static nj_s4 integer(nj_vm *vm, nj_ref player, const char *name)
{
    const nj_field *f = nj_field_lookup(vm, PLAYER, name, "I", 0);
    CHECK(f != NULL);
    return nj_field_read_int(vm, player, f);
}
static nj_status create_player(nj_vm *vm, const unsigned char *file, nj_u4 size,
                              nj_ref *player)
{
    nj_ref bytes = nj_root(vm, nj_byte_array_new(vm, size));
    nj_ref stream;
    nj_value a[2], result;
    CHECK(bytes != NULL);
    memcpy(nj_array_data(bytes), file, size);
    stream = nj_root(vm, nj_object_new(vm, STREAM));
    CHECK(stream != NULL);
    a[0].r = bytes;
    CHECK(nj_call_special(vm, stream, STREAM, "<init>", "([B)V", a, NULL) == NJ_OK);
    a[0].r = stream; a[1].r = NULL; result.r = NULL;
    {
        nj_status rc = nj_call_static(vm, MANAGER, "createPlayer",
            "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;", a, &result);
        *player = rc == NJ_OK ? nj_root(vm, result.r) : NULL;
        return rc;
    }
}
static void compare_pcm(const nj_s2 *actual, const unsigned char *expected, unsigned samples)
{
    unsigned i;
    for (i = 0; i < samples; ++i) {
        nj_s2 want = (nj_s2)((nj_u2)expected[i * 2] | ((nj_u2)expected[i * 2 + 1] << 8));
        if (actual[i] != want) {
            fprintf(stderr, "PCM sample %u: got %d expected %d\n", i, (int)actual[i], (int)want);
            CHECK(actual[i] == want);
        }
        ++checks;
    }
}
static void run_case(const unsigned char *file, nj_u4 size,
                     const unsigned char *expected, unsigned frames, int channels,
                     int bounded, int accepted)
{
    nj_port port;
    nj_vm *vm;
    nj_ref player = NULL;
    nj_status rc;
    unsigned round;
    ++cases;
    memset(&sink, 0, sizeof(sink)); sink.bounded = bounded;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.audio_decode = NULL;
    port.pcm_open = open_pcm; port.pcm_free = room_pcm;
    port.pcm_write = write_pcm; port.pcm_close = close_pcm;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    rc = create_player(vm, file, size, &player);
    if (!accepted) {
        CHECK(rc != NJ_OK && nj_has_exception(vm));
        CHECK(player == NULL && sink.opens == 0 && sink.frames == 0);
        nj_clear_exception(vm);
        nj_vm_destroy(vm); nj_port_shutdown(&port);
        return;
    }
    CHECK(rc == NJ_OK && player != NULL && !nj_has_exception(vm));
    CHECK(integer(vm, player, "pFrames") == (nj_s4)frames);
    CHECK(integer(vm, player, "pChans") == channels);
    CHECK(integer(vm, player, "pRate") == 8000);
    {
        const nj_field *f = nj_field_lookup(vm, PLAYER, "pData", "[B", 0);
        nj_ref pcm;
        nj_value result;
        CHECK(f != NULL);
        pcm = nj_field_read_ref(vm, player, f);
        CHECK(pcm != NULL);
        /* The previous stereo decoder published almost twice its allocation. */
        CHECK(nj_array_length(pcm) == frames * (unsigned)channels * 2u);
        CHECK(memcmp(nj_array_data(pcm), expected, frames * (unsigned)channels * 2u) == 0);
        CHECK(nj_call(vm, player, PLAYER, "getDuration", "()J", NULL, &result) == NJ_OK);
        CHECK(result.j == (nj_s8)frames * 1000000 / 8000);
    }
    for (round = 0; round < 2; ++round) {
        nj_value result;
        unsigned i;
        sink.frames = 0;
        CHECK(nj_call(vm, player, PLAYER, "start", "()V", NULL, NULL) == NJ_OK);
        for (i = 0; i < 1024; ++i) {
            nj_media_frame(vm);
            CHECK(nj_call(vm, player, PLAYER, "getState", "()I", NULL, &result) == NJ_OK);
            if (result.i != 400) break;
        }
        CHECK(i < 1024 && result.i == 300);
        CHECK(sink.channels == channels && sink.frames == frames);
        compare_pcm(sink.samples, expected, frames * (unsigned)channels);
        CHECK(nj_call(vm, player, PLAYER, "getMediaTime", "()J", NULL, &result) == NJ_OK);
        CHECK(result.j == (nj_s8)frames * 1000000 / 8000);
    }
    CHECK(sink.opens == 2 && sink.closes == 2);
    CHECK(nj_call(vm, player, PLAYER, "close", "()V", NULL, NULL) == NJ_OK);
    CHECK(!nj_has_exception(vm));
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}

int main(void)
{
    unsigned ch, bounded;
    for (ch = 1; ch <= 2; ++ch) {
        const unsigned char *file = ch == 1 ? wav_ima_mono_file : wav_ima_stereo_file;
        const unsigned char *pcm = ch == 1 ? wav_ima_mono_pcm : wav_ima_stereo_pcm;
        nj_u4 size = ch == 1 ? sizeof(wav_ima_mono_file) : sizeof(wav_ima_stereo_file);
        unsigned frames = ch == 1 ? WAV_IMA_MONO_FRAMES : WAV_IMA_STEREO_FRAMES;
        nj_u4 data = chunk(file, size, "data");
        nj_u4 fmt = chunk(file, size, "fmt ");
        unsigned char bad[sizeof(wav_ima_stereo_file)];
        unsigned mutation;
        CHECK(size <= sizeof(bad));
        for (bounded = 0; bounded < 2; ++bounded) {
            run_case(file, size, pcm, frames, (int)ch, (int)bounded, 1);
            /* One complete block plus a partial next block: keep only the
             * complete block, which is a prefix of the FFmpeg PCM oracle. */
            run_case(file, data + 64u + 63u, pcm, ch == 1 ? 121u : 57u,
                     (int)ch, (int)bounded, 1);
        }
        run_case(file, data + 63u, NULL, 0, (int)ch, 0, 0);
        run_case(file, 24, NULL, 0, (int)ch, 0, 0);
        for (mutation = 0; mutation < 5; ++mutation) {
            memcpy(bad, file, size);
            switch (mutation) {
            case 0: bad[fmt + 2] = 3; break;             /* three channels */
            case 1: bad[fmt + 14] = 8; break;            /* IMA must be 4-bit */
            case 2: bad[fmt + 12] = 0; bad[fmt + 13] = 0; break;
            case 3: memset(bad + fmt + 4, 0, 4); break; /* zero rate */
            default: bad[fmt] = 0xff; bad[fmt + 1] = 0x7f; break;
            }
            run_case(bad, size, NULL, 0, (int)ch, 0, 0);
        }
    }
    printf("test_wav_stereo: %u actual Player cases, %u checks; complete FFmpeg PCM, mono/stereo, truncation and malformed input\n", cases, checks);
    return 0;
}
