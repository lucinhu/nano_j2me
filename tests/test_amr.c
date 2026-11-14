/*
 * nano_j2me - the AMR narrowband decoder, on its own.
 *
 * tests/test_audio.c asserts the Player that comes out of javax.microedition
 * .media when a JAR holds an AMR asset; this asserts the decoder underneath it,
 * where a Player cannot see: which byte strings are frames at all, that each of
 * the eight speech modes decodes, and that the samples are the reference
 * decoder's samples rather than merely plausible ones.
 *
 * The expected PCM below came from another implementation of the same text -
 * FFmpeg's own AMR narrowband decoder, run as
 *
 *     ffmpeg -c:a amrnb -i vector.amr -f s16le -acodec pcm_s16le vector.s16
 *
 * over the same bytes - so a transcription error in the tables or the DSP shows
 * up here as a number that does not match.  On vectors this short the two
 * implementations agree to the last bit; across the 395 JARs in tests/games/
 * they agree to 85-93 dB, which is where a different summation order starts to
 * show.  (ffmpeg's OTHER AMR decoder, libopencore_amrnb, is a further 14-45 dB
 * away from both: the two references disagree with each other by that much, so
 * these assertions retain that historical floating-point baseline. They are
 * regression checks, not TS 26.073 fixed-point conformance vectors.)
 *
 * The vectors are a frame header plus a fixed payload pattern, generated below
 * rather than copied from a game, so nothing here is anybody's asset.
 *
 * Usage: test_amr
 */
#include "nj_amr.h"
#include "nj2me/nj_avcodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLES_PER_FRAME   ((int)NJ_AMR_BLOCK)        /* 160, i.e. 20 ms */
#define BYTES_PER_FRAME     ((int)NJ_AMR_BLOCK * 2)    /* 16-bit mono PCM */

static int checks = 0;
static int fails = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

/*
 * Payload byte i of mode m's frame.  Every mode gets different bytes, and the
 * pattern is spread over the byte range so no mode decodes from an all-zero
 * bitstream - which would exercise the indices 0 of every codebook and nothing
 * else.
 */
static nj_u1 payload(int mode, int i)
{
    return (nj_u1)((i * 37 + 11 + mode * 5) & 0xFF);
}

/* Header of one speech frame: frame type, quality bit set. */
static nj_u1 header(int mode)
{
    return (nj_u1)(((nj_u4)mode << 3) | 0x04u);
}

/* How many payload bytes a speech frame of this mode carries. */
static int payload_bytes(int mode)
{
    static const int bytes[NJ_AMR_MODES] = {
        12, 13, 15, 17, 19, 20, 26, 31, 5
    };
    return bytes[mode];
}

/* Append one speech frame.  Returns the new length. */
static nj_u4 add_frame(nj_u1 *out, nj_u4 len, int mode)
{
    int i;
    out[len++] = header(mode);
    for (i = 0; i < payload_bytes(mode); i++) {
        out[len++] = payload(mode, i);
    }
    return len;
}

/* A file holding the given modes, magic included. */
static nj_u4 build(nj_u1 *out, const int *modes, int count)
{
    nj_u4 len = 0u;
    int i;

    memcpy(out, "#!AMR\n", 6u);
    len = 6u;
    for (i = 0; i < count; i++) {
        len = add_frame(out, len, modes[i]);
    }
    return len;
}

/*
 * What ffmpeg decoded for one frame of each mode: the first eight samples, the
 * sum of the squares over all 160 (a whole-frame check in one number), and the
 * peak absolute sample.
 */
static const struct mode_ref {
    int         mode;
    short       first[8];
    long        sumsq;
    int         peak;
} mode_refs[8] = {
    { 0, { 0, 0, 0, 0, 0, 0, 0, 0 },        332824L,   189 },
    { 1, { 0, 0, 5, 4, -8, -6, -1, 3 },     261353L,   149 },
    { 2, { 0, 0, 0, 0, 0, 0, 7, 4 },       1350215L,   379 },
    { 3, { 0, 2, 2, -4, -2, 1, -2, 1 },     381220L,   247 },
    { 4, { 0, 0, 0, 0, 0, 0, 0, 0 },       1359156L,   479 },
    { 5, { 0, 0, 0, -5, -6, 3, 3, 2 },      746619L,   243 },
    { 6, { 0, 0, 0, 0, 0, 0, 0, -12 },    17765671L,  1286 },
    { 7, { 0, 15, 44, 23, -11, -17, -37, -31 }, 345536L, 121 }
};

/* The four-frame stream the mixed test decodes: 6.7k, 12.2k, 5.15k, 4.75k. */
static const int mix_modes[4] = { 3, 7, 1, 0 };
static const short mix_first16[16] = {
    0, 2, 2, -4, -2, 1, -2, 1, 1, -1, 1, 0, -1, 1, 0, 0
};
static const long  mix_sumsq = 80222105L;
static const int   mix_peak = 2644;

/* Sum of the squares of the decoded samples, as a number two decoders can be
 * compared by without a tolerance argument. */
static long sumsq_of(const nj_u1 *pcm, int samples)
{
    long total = 0L;
    int i;
    for (i = 0; i < samples; i++) {
        short s = (short)((unsigned)pcm[i * 2] | ((unsigned)pcm[i * 2 + 1] << 8));
        total += (long)s * (long)s;
    }
    return total;
}

static int peak_of(const nj_u1 *pcm, int samples)
{
    int peak = 0;
    int i;
    for (i = 0; i < samples; i++) {
        short s = (short)((unsigned)pcm[i * 2] | ((unsigned)pcm[i * 2 + 1] << 8));
        int a = s < 0 ? -(int)s : (int)s;
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static int all_zero(const nj_u1 *pcm, int samples)
{
    int i;
    for (i = 0; i < samples * 2; i++) {
        if (pcm[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static short sample_at(const nj_u1 *pcm, int i)
{
    return (short)((unsigned)pcm[i * 2] | ((unsigned)pcm[i * 2 + 1] << 8));
}


#if NJ_ENABLE_AMR
/* ---- the provider wrapper must hand out exactly the same PCM ---- */
static void *amr_mem_alloc(void *ud, nj_size n) { (void)ud; return malloc(n); }
static void amr_mem_free(void *ud, void *p) { (void)ud; free(p); }

static void test_provider_matches_direct_decode(void)
{
    static nj_u1 file[4096];
    static nj_u1 direct[8 * BYTES_PER_FRAME + 16];
    static nj_u1 via[8 * BYTES_PER_FRAME + 16];
    const nj_av_audio_ops *ops = nj_avcodec_sw_amr();
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    nj_u4 len, got, total = 0u;
    int modes[2] = { 1, 4 };

    ok(ops != NULL, "the AMR provider is registered");
    if (ops == NULL) {
        return;
    }
    len = build(file, modes, 2);
    ok(ops->accepts(file, len) == 1, "the provider accepts #!AMR streams");
    got = nj_amr_frames(file, len);
    if (got != 2u) {
        fprintf(stderr, "  nj_amr_frames gave %u\n", (unsigned)got);
    }
    ok(got == 2u, "two AMR frames on the wire");

    mem.ud = NULL;
    mem.alloc = amr_mem_alloc;
    mem.free = amr_mem_free;
    dec = ops->open(file, len, &mem, &info, &err);
    ok(dec != NULL, "the provider opens the stream");
    if (dec == NULL) {
        return;
    }
    ok(info.format == NJ_AV_AUDIO_AMR && info.rate == 8000u &&
       info.channels == 1u && info.frames == 2u * 160u,
       "the provider reports 8 kHz mono, two 20 ms frames");

    memset(via, 0, sizeof(via));
    for (;;) {
        nj_av_step step;
        int rc;

        memset(&step, 0, sizeof(step));
        step.in = file;
        step.in_len = len;
        step.out = via + total;
        step.out_cap = (nj_u4)(sizeof(via) - total);
        rc = ops->decode(dec, &step);
        if (rc == NJ_AV_ERR_END) {
            break;
        }
        ok(rc == NJ_AV_OK, "the provider decodes a slice");
        total += step.out_used;
    }
    (void)nj_amr_decode(file, len, direct);
    if (total != 2u * 160u * 2u) {
        fprintf(stderr, "  provider handed out %u bytes\n", (unsigned)total);
    }
    ok(total == 2u * 160u * 2u, "the provider hands out every byte");
    ok(memcmp(via, direct, total) == 0,
       "the provider's PCM is byte-identical to nj_amr_decode()");
    ops->close(dec);
}
#endif /* NJ_ENABLE_AMR */

#if NJ_ENABLE_AMR
/* All-one MR67 payloads have legal framing but an unstable gain history.
 * This used to overflow float-to-int, then turn NaN into a PCM integer. Check
 * recovery through the public API, including the next independently coded
 * frame; sanitizer builds diagnose either undefined conversion as well. */
static void test_numeric_recovery(void)
{
    enum { BAD_FRAMES = 128, FRAME_BYTES = 18 };
    nj_u1 input[6 + BAD_FRAMES * FRAME_BYTES + 32];
    nj_u1 pcm[BAD_FRAMES * BYTES_PER_FRAME + BYTES_PER_FRAME + 16];
    nj_u1 good_file[64], good_pcm[BYTES_PER_FRAME];
    nj_u4 len = 6u, good_len, reset = BAD_FRAMES, i;
    memcpy(input, "#!AMR\n", 6u);
    for (i = 0; i < BAD_FRAMES; ++i) {
        input[len++] = (nj_u1)((3u << 3) | 4u);
        memset(input + len, 0xFF, FRAME_BYTES - 1);
        len += FRAME_BYTES - 1;
    }
    memset(pcm, 0xA5, sizeof(pcm));
    ok(nj_amr_decode(input, len, pcm) == BAD_FRAMES,
       "runaway gains preserve the promised frame count");
    ok(pcm[BAD_FRAMES * BYTES_PER_FRAME] == 0xA5,
       "runaway gains do not overwrite the output canary");
    for (i = 0; i < BAD_FRAMES; ++i) {
        if (all_zero(pcm + i * BYTES_PER_FRAME, SAMPLES_PER_FRAME)) {
            reset = i;
            break;
        }
    }
    ok(reset < BAD_FRAMES, "runaway excitation triggers a silent recovery frame");
    if (reset == BAD_FRAMES) return;
    memcpy(good_file, "#!AMR\n", 6u);
    good_len = add_frame(good_file, 6u, 7);
    ok(nj_amr_decode(good_file, good_len, good_pcm) == 1u,
       "standalone recovery reference decodes");
    len = 6u + (reset + 1u) * FRAME_BYTES;
    len = add_frame(input, len, 7);
    memset(pcm, 0xA5, sizeof(pcm));
    ok(nj_amr_decode(input, len, pcm) == reset + 2u,
       "a valid frame following numeric recovery decodes");
    ok(memcmp(pcm + (reset + 1u) * BYTES_PER_FRAME, good_pcm,
              BYTES_PER_FRAME) == 0,
       "numeric recovery clears corrupt history before the next valid frame");
    ok(pcm[(reset + 2u) * BYTES_PER_FRAME] == 0xA5,
       "recovery and the next frame preserve the output canary");
}

/* Build format fields, not a decoder's parameter data. SID bit positions
 * follow TS 26.092 table 1 and TS 26.093 STI/mode indication. */
static void sid_field(nj_u1 *out, int first, int count, unsigned value)
{
    int bit;
    for (bit = 0; bit < count; ++bit) {
        unsigned mask = 1u << (7 - (first + bit) % 8);
        if ((value >> (count - 1 - bit)) & 1u)
            out[1 + (first + bit) / 8] |= (nj_u1)mask;
        else out[1 + (first + bit) / 8] &= (nj_u1)~mask;
    }
}

static nj_u4 add_sid(nj_u1 *out, nj_u4 len, int energy, int update, int good)
{
    memset(out + len, 0, 6);
    out[len] = (nj_u1)((8u << 3) | (good ? 4u : 0u));
    sid_field(out + len, 29, 6, (unsigned)energy);
    sid_field(out + len, 35, 1, (unsigned)update);
    sid_field(out + len, 36, 3, 7u);
    return len + 6u;
}

static void test_sid_and_bad_frames(void)
{
    enum { LONG_FRAMES = 300 };
    static nj_u1 a[4096], b[4096];
    static nj_u1 x[LONG_FRAMES * BYTES_PER_FRAME + 16];
    static nj_u1 y[LONG_FRAMES * BYTES_PER_FRAME + 16];
    nj_u4 len, other, i, end;
    double high_energy, low_energy;
    memcpy(a, "#!AMR\n", 6);
    len = add_sid(a, 6u, 40, 1, 1);
    for (i = 1; i < 32; ++i) a[len++] = header(15);
    memset(x, 0xA5, sizeof(x));
    ok(nj_amr_frames(a, len) == 32u, "SID_UPDATE plus NO_DATA counts 32 frames");
    ok(nj_amr_decode(a, len, x) == 32u, "SID_UPDATE and continuous CN decode");
    ok(x[32 * BYTES_PER_FRAME] == 0xA5, "CN preserves the output canary");
    ok(!all_zero(x + 10 * BYTES_PER_FRAME, 22 * SAMPLES_PER_FRAME),
       "NO_DATA after SID keeps generating comfort noise");
    high_energy = (double)sumsq_of(x + 10 * BYTES_PER_FRAME, 22 * SAMPLES_PER_FRAME);
    /* External OpenCORE reference: RMS 343.140019 for these 3520 samples.
     * Floating-point CNG is not bit exact; assert its decoded SID level. */
    ok(high_energy > 330.0 * 330.0 * 3520.0 &&
       high_energy < 356.0 * 356.0 * 3520.0,
       "SID energy and LP gain agree with the external oracle level");
    ok(nj_amr_decode(a, len, y) == 32u && memcmp(x, y, 32 * BYTES_PER_FRAME) == 0,
       "CN random state is deterministic and local to each decode");
    sid_field(a + 6, 29, 6, 32u);
    ok(nj_amr_decode(a, len, y) == 32u, "lower SID energy decodes");
    low_energy = (double)sumsq_of(y + 10 * BYTES_PER_FRAME, 22 * SAMPLES_PER_FRAME);
    ok(high_energy > 15.5 * low_energy && high_energy < 16.5 * low_energy,
       "eight SID energy steps multiply power by sixteen");

    len = add_sid(a, 6u, 32, 1, 1);
    for (i = 1; i < 8; ++i) a[len++] = header(15);
    end = len;
    len = add_sid(a, len, 40, 1, 1);
    for (i = 9; i < 24; ++i) a[len++] = header(15);
    memcpy(b, a, len);
    sid_field(b + end, 29, 6, 32u);
    ok(nj_amr_decode(a, len, x) == 24u && nj_amr_decode(b, len, y) == 24u,
       "second SID_UPDATE interpolates a changed energy model");
    ok(memcmp(x, y, 8 * BYTES_PER_FRAME) == 0 &&
       memcmp(x + 8 * BYTES_PER_FRAME, y + 8 * BYTES_PER_FRAME,
              16 * BYTES_PER_FRAME) != 0,
       "SID_UPDATE affects only its own frame and subsequent CN");
    high_energy = (double)sumsq_of(x + 16 * BYTES_PER_FRAME, 8 * SAMPLES_PER_FRAME);
    low_energy = (double)sumsq_of(y + 16 * BYTES_PER_FRAME, 8 * SAMPLES_PER_FRAME);
    ok(high_energy > 15.0 * low_energy && high_energy < 17.0 * low_energy,
       "SID_UPDATE converges to the new logarithmic energy");

    /* In CN, SPEECH_BAD has exactly the same effect as NO_DATA, including
     * following good speech. This checks that no unusable payload is read. */
    len = add_sid(a, 6u, 40, 1, 1);
    a[len++] = header(15);
    len = add_frame(a, len, 7);
    memcpy(b, a, 12);
    other = add_frame(b, 12u, 3);
    b[12] &= (nj_u1)~4u;
    memset(b + 13, 0xFF, 17);
    other = add_frame(b, other, 7);
    ok(nj_amr_decode(a, len, x) == 3u && nj_amr_decode(b, other, y) == 3u,
       "CN bad-speech and no-data sequences decode through speech resumption");
    ok(memcmp(x, y, 3 * BYTES_PER_FRAME) == 0,
       "bad speech in CN ignores all payload and preserves CN state");
    ok(!all_zero(x + 2 * BYTES_PER_FRAME, SAMPLES_PER_FRAME),
       "good speech leaves CN and produces speech samples");

    len = add_sid(a, 6u, 40, 1, 1);
    len = add_sid(a, len, 12, 1, 0); /* SID_BAD */
    len = add_frame(a, len, 1);
    memcpy(b, a, len);
    memset(b + 13, 0xFF, 5);
    ok(nj_amr_decode(a, len, x) == 3u && nj_amr_decode(b, len, y) == 3u &&
       memcmp(x, y, 3 * BYTES_PER_FRAME) == 0,
       "SID_BAD cannot replace frequency or energy with corrupt parameters");
    a[12] = b[12] = header(8);
    sid_field(a + 12, 35, 1, 0u);
    sid_field(b + 12, 35, 1, 0u);
    ok(nj_amr_decode(a, len, x) == 3u && nj_amr_decode(b, len, y) == 3u &&
       memcmp(x, y, 3 * BYTES_PER_FRAME) == 0,
       "SID_FIRST ignores its untransmitted codec parameters");

    len = build(a, mix_modes, 1);
    end = len;
    len = add_frame(a, len, 7);
    a[end] &= (nj_u1)~4u;
    memcpy(b, a, len);
    memset(b + end + 1, 0xFF, 31);
    len = add_frame(a, len, 4);
    other = add_frame(b, end + 32u, 4);
    ok(nj_amr_decode(a, len, x) == 3u && nj_amr_decode(b, other, y) == 3u &&
       memcmp(x, y, 3 * BYTES_PER_FRAME) == 0,
       "Q=0 in speech mode cannot feed corrupt payload into later speech");
    ok(all_zero(x + BYTES_PER_FRAME, SAMPLES_PER_FRAME),
       "speech erasure uses explicit conservative muting");

    len = add_sid(a, 6u, 40, 1, 1);
    for (i = 1; i < LONG_FRAMES; ++i) a[len++] = header(15);
    memset(x, 0xA5, sizeof(x));
    ok(nj_amr_decode(a, len, x) == LONG_FRAMES,
       "missing SID updates keep their full output duration");
    ok(!all_zero(x + 10 * BYTES_PER_FRAME, SAMPLES_PER_FRAME) &&
       all_zero(x + (LONG_FRAMES - 1) * BYTES_PER_FRAME, SAMPLES_PER_FRAME),
       "SID timeout gradually mutes CN after prolonged update loss");
    ok(x[LONG_FRAMES * BYTES_PER_FRAME] == 0xA5,
       "long CN loss preserves the output canary");

    len = add_sid(a, 6u, 63, 1, 1);
    for (i = 1; i < LONG_FRAMES; ++i) {
        if (i % 8 == 0) len = add_sid(a, len, 63, 1, 1);
        else a[len++] = header(15);
    }
    memset(x, 0xA5, sizeof(x));
    ok(nj_amr_decode(a, len, x) == LONG_FRAMES &&
       x[LONG_FRAMES * BYTES_PER_FRAME] == 0xA5,
       "maximum SID energy stays finite and bounded under sanitizers");
}
#endif

int main(void)
{
    static nj_u1 file[4096];
    static nj_u1 pcm[8 * BYTES_PER_FRAME + 16];
    nj_u4 len;
    nj_u4 frames;
    nj_u4 got;
    int i;

    printf("== nano_j2me AMR narrowband decoder ==\n");

    /* ---- what is not an AMR file ---- */

    ok(nj_amr_frames(NULL, 0u) == 0u, "a null pointer is no frames");
    ok(nj_amr_frames(file, 5u) == 0u, "five bytes are no file");
    memcpy(file, "RIFF", 4u);
    ok(nj_amr_frames(file, 8u) == 0u, "RIFF is not AMR");
    memcpy(file, "\x00\x00\x00\x18""ftyp", 8u);
    ok(nj_amr_frames(file, 16u) == 0u, "an MP4 container is not AMR");
    /*
     * Wideband is the one that matters: "#!AMR-WB\n" starts with "#!AMR", and a
     * narrowband reader that took it would read wideband frame headers as
     * narrowband modes and hand back noise at the wrong rate.
     */
    memcpy(file, "#!AMR-WB\n", 9u);
    file[9] = header(7);
    ok(nj_amr_frames(file, 10u) == 0u, "AMR wideband is refused, not misread");
    memcpy(file, "#!AMR\n", 6u);
    ok(nj_amr_frames(file, 6u) == 0u, "the magic alone is no frames");

    /* ---- one frame per mode, against the reference samples ---- */

    for (i = 0; i < 8; i++) {
        int m = mode_refs[i].mode;
        int j;
        char what[64];

        len = build(file, &m, 1);
        frames = nj_amr_frames(file, len);
        sprintf(what, "mode %d: one frame on the wire is one frame", m);
        ok(frames == 1u, what);
        if (frames != 1u) {
            continue;
        }
        memset(pcm, 0xAA, sizeof(pcm));
        got = nj_amr_decode(file, len, pcm);
        sprintf(what, "mode %d: decode returns the count nj_amr_frames gave", m);
        ok(got == 1u, what);
        for (j = 0; j < 8; j++) {
            sprintf(what, "mode %d: sample %d is the reference's", m, j);
            ok(sample_at(pcm, j) == mode_refs[i].first[j], what);
        }
        sprintf(what, "mode %d: the whole frame is the reference's", m);
        ok(sumsq_of(pcm, SAMPLES_PER_FRAME) == mode_refs[i].sumsq &&
           peak_of(pcm, SAMPLES_PER_FRAME) == mode_refs[i].peak, what);
        /* The caller owns the buffer and the decoder writes exactly the samples
         * it promised - not a byte more, which the canary past the end says. */
        sprintf(what, "mode %d: nothing is written past the samples", m);
        ok(pcm[BYTES_PER_FRAME] == 0xAAu && pcm[BYTES_PER_FRAME + 1] == 0xAAu,
           what);
    }

    /* ---- four frames in a row, in four different modes ---- */

    len = build(file, mix_modes, 4);
    frames = nj_amr_frames(file, len);
    ok(frames == 4u, "a four-frame stream counts four frames");
    memset(pcm, 0xAA, sizeof(pcm));
    got = nj_amr_decode(file, len, pcm);
    ok(got == 4u, "decoding returns four frames");
    ok(sumsq_of(pcm, 4 * SAMPLES_PER_FRAME) == mix_sumsq &&
       peak_of(pcm, 4 * SAMPLES_PER_FRAME) == mix_peak,
       "the four-frame stream is the reference's, end to end");
    {
        int same = 1;
        for (i = 0; i < 16; i++) {
            if (sample_at(pcm, i) != mix_first16[i]) {
                same = 0;
            }
        }
        ok(same, "the first samples of the stream are the reference's");
        ok(pcm[4 * BYTES_PER_FRAME] == 0xAAu,
           "the buffer is exactly four frames long, no more");
    }

    /* ---- frame types that carry no speech ---- */

    /*
     * A SID frame is six bytes: 39 bits of comfort noise plus the header, which
     * is what RFC 4867 Table 1a says and what the reference demuxer reads.  The
     * SID_FIRST enters comfort noise with its initialized model, and still
     * walks by six bytes so the following speech is read at its own header.
     */
    memcpy(file, "#!AMR\n", 6u);
    len = 6u;
    file[len++] = (nj_u1)((8u << 3) | 0x04u);       /* SID */
    file[len++] = 0x00u;
    file[len++] = 0x11u;
    file[len++] = 0x22u;
    file[len++] = 0x33u;
    file[len++] = 0x44u;
    len = add_frame(file, len, 7);
    len = add_frame(file, len, 0);
    ok(nj_amr_frames(file, len) == 3u,
       "a six-byte SID frame is one frame, not six headers");
    memset(pcm, 0xAA, sizeof(pcm));
    got = nj_amr_decode(file, len, pcm);
    ok(got == 3u, "the speech frames behind a SID frame still decode");
    ok(!all_zero(pcm, SAMPLES_PER_FRAME), "SID_FIRST initializes comfort noise");
    ok(sumsq_of(pcm + BYTES_PER_FRAME, 2 * SAMPLES_PER_FRAME) > 0L,
       "the frames after the SID frame are not silence");

    /* NO_DATA is the header alone: one byte, no payload, and it still costs a
     * frame's worth of output. */
    memcpy(file, "#!AMR\n", 6u);
    len = 6u;
    file[len++] = (nj_u1)((15u << 3) | 0x04u);
    len = add_frame(file, len, 7);
    ok(nj_amr_frames(file, len) == 2u, "a one-byte NO_DATA frame counts");
    memset(pcm, 0xAA, sizeof(pcm));
    got = nj_amr_decode(file, len, pcm);
    ok(got == 2u && all_zero(pcm, SAMPLES_PER_FRAME),
       "NO_DATA decodes to silence and the frame after it decodes");

    /* 9 through 14 are reserved: the stream stops being a stream there, and the
     * walk stops rather than reading the rest of the file as headers. */
    len = build(file, mix_modes, 1);
    file[len++] = (nj_u1)((9u << 3) | 0x04u);
    len = add_frame(file, len, 7);
    ok(nj_amr_frames(file, len) == 1u,
       "a reserved frame type ends the walk, it is not skipped");

    /* ---- short and truncated files ---- */

    len = build(file, mix_modes, 2);
    ok(nj_amr_frames(file, len - 1u) == 1u,
       "a frame whose payload is cut short is not counted");
    ok(nj_amr_frames(file, len) == 2u, "the same file intact counts two");
    /* Bytes after the last speech frame are read as frame headers, and 0xFF is
     * a legal one: mode 15, NO_DATA, one byte, no payload.  So trailing 0xFF
     * costs a frame of silence rather than being called garbage - which is what
     * a stream that pads with them expects. */
    file[len] = 0xFFu;
    file[len + 1] = 0xFFu;
    ok(nj_amr_frames(file, len + 2u) == 4u,
       "trailing NO_DATA headers are frames, not garbage");
    ok(nj_amr_decode(file, len + 2u, NULL) == 0u,
       "a null output buffer decodes nothing");
    /* A reserved type is the other half of that: there the stream has stopped
     * being a stream, and the walk stops with it. */
    file[len] = (nj_u1)((12u << 3) | 0x04u);
    ok(nj_amr_frames(file, len + 1u) == 2u,
       "a trailing reserved header is not counted");

#if NJ_ENABLE_AMR
    test_numeric_recovery();
    test_sid_and_bad_frames();
    test_provider_matches_direct_decode();
#endif
    printf("\n%d checks, %d failures\n", checks, fails);
    printf("%s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
