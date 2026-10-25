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
 * this test anchors to the one the code was written from.)
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
     * decoder has no comfort noise generator, so it answers with silence - but
     * it must still walk the stream by the right six bytes, or the speech frame
     * behind it is read as the middle of a header.
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
    ok(all_zero(pcm, SAMPLES_PER_FRAME), "a SID frame decodes to silence");
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
    test_provider_matches_direct_decode();
#endif
    printf("\n%d checks, %d failures\n", checks, fails);
    printf("%s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
