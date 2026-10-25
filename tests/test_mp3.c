/*
 * nano_j2me - tests/test_mp3.c
 *
 * Checks for the built-in MPEG audio (Layer III) provider.
 *
 * The streams below are synthesised in the test instead of being embedded as
 * opaque blobs: a Layer III frame with an all-zero side information block has
 * part2_3_length == 0, so every granule decodes to an all-zero spectrum and the
 * decoder must emit digital silence.  That makes a hand-built stream a precise
 * oracle for the container side of the decoder (sync, header fields, side
 * information size, reservoir bookkeeping, PCM frame accounting, streaming and
 * seeking) without needing a binary fixture.
 *
 * Bit-exact audio is NOT asserted here: no reference PCM is embedded, so this
 * test only proves that the pipeline runs and that a silent stream stays
 * silent.  See the report that came with this file for what was measured
 * against ffmpeg's decoder outside the repository.
 *
 * Usage: test_mp3
 */
#include "nj2me/nj.h"
#include "nj2me/nj_avcodec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const nj_av_audio_ops nj_av_audio_mp3;

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

static void okf(int cond, const char *fmt, long a, long b)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL ");
        printf(fmt, a, b);
        printf("\n");
    }
}

static void *t_alloc(void *ud, nj_u4 len)
{
    (void)ud;
    return malloc(len);
}

static void t_free(void *ud, void *ptr)
{
    (void)ud;
    free(ptr);
}

/* ------------------------------------------------------------------ */
/* synthetic Layer III streams                                         */
/* ------------------------------------------------------------------ */

#define HDR_MPEG1   3
#define HDR_MPEG2   2
#define HDR_MPEG25  0

/*
 * Write a four byte frame header.  `version` is HDR_*, `bitrate_index` and
 * `rate_index` are the raw header fields (the sampling rate field is only two
 * bits wide, so it is 0..2 for every MPEG version), `mode` 0..3.
 */
static void put_header(nj_u1 *p, int version, int bitrate_index, int rate_index,
                       int mode, int mode_ext)
{
    p[0] = 0xFF;
    p[1] = (nj_u1)(0xE0 | (version << 3) | (1 << 1) | 1);
    p[2] = (nj_u1)((bitrate_index << 4) | (rate_index << 2));
    p[3] = (nj_u1)((mode << 6) | (mode_ext << 4) | 0x04);
}

/*
 * Build `frames` frames of MPEG audio Layer III silence.  Returns the number of
 * bytes written, or 0 when the buffer is too small.
 */
static nj_u4 build_silence(nj_u1 *dst, nj_u4 cap, int version, int bitrate_index,
                           int rate_index, int mode, int frames)
{
    static const int frame_len_m1[16] = {
        0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
    };
    static const int frame_len_lsf[16] = {
        0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
    };
    static const int rate_tab[9] = {
        44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000
    };
    int kbps, rate, side, per_frame, i, done = 0;
    int row;
    nj_u4 pos = 0;

    kbps = (version == HDR_MPEG1) ? frame_len_m1[bitrate_index]
                                  : frame_len_lsf[bitrate_index];
    row = (version == HDR_MPEG1) ? rate_index
        : (version == HDR_MPEG2) ? 3 + rate_index
        : 6 + rate_index;
    rate = rate_tab[row];
    side = (version == HDR_MPEG1) ? ((mode == 3) ? 17 : 32)
                                  : ((mode == 3) ? 9 : 17);
    per_frame = (version == HDR_MPEG1) ? (144 * kbps * 1000 / rate)
                                       : (72 * kbps * 1000 / rate);
    if (per_frame <= 4 + side) {
        return 0;
    }
    for (i = 0; i < frames; i++) {
        if (pos + (nj_u4)per_frame > cap) {
            return 0;
        }
        put_header(dst + pos, version, bitrate_index, rate_index, mode, 0);
        memset(dst + pos + 4, 0, (size_t)per_frame - 4u);
        pos += (nj_u4)per_frame;
        done++;
    }
    return (done == frames) ? pos : 0;
}

/* ------------------------------------------------------------------ */
/* decode driver                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    nj_u4 frames;
    nj_u4 bytes;
    nj_u4 calls;
    nj_u4 again;
    int   last_rc;
    nj_s8 last_time;
    int   silent;
    int   time_monotonic;
} run_stat;

static void run_decode(const nj_av_audio_ops *ops, const nj_u1 *data, nj_u4 len,
                       nj_u4 chunk, run_stat *st)
{
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    nj_u1 *pcm;
    nj_u4 off = 0;
    nj_s8 prev_time = -1;

    memset(st, 0, sizeof(*st));
    st->silent = 1;
    st->time_monotonic = 1;
    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    pcm = (nj_u1 *)malloc(16384);
    dec = ops->open(data, len, &mem, &info, &err);
    if (dec == NULL || pcm == NULL) {
        printf("  open failed: %s\n", err != NULL ? err : "?");
        free(pcm);
        return;
    }
    for (;;) {
        nj_av_step step;
        nj_u4 avail = len - off;
        int rc;

        if (avail > chunk) {
            avail = chunk;
        }
        memset(&step, 0, sizeof(step));
        step.in = data + off;
        step.in_len = avail;
        step.out = pcm;
        step.out_cap = 16384u;
        rc = ops->decode(dec, &step);
        st->calls++;
        off += step.in_used;
        st->frames += step.frames;
        st->bytes += step.out_used;
        if (rc == NJ_AV_ERR_AGAIN) {
            st->again++;
        }
        if (step.time_us >= 0) {
            if (prev_time >= 0 && step.time_us < prev_time) {
                st->time_monotonic = 0;
            }
            prev_time = step.time_us;
            st->last_time = step.time_us;
        }
        if (step.out_used > 0) {
            nj_u4 i;

            for (i = 0; i < step.out_used; i++) {
                if (pcm[i] != 0u) {
                    st->silent = 0;
                    break;
                }
            }
        }
        st->last_rc = rc;
        if (rc == NJ_AV_ERR_END) {
            break;
        }
        if (rc < 0 && rc != NJ_AV_ERR_AGAIN) {
            break;
        }
        if (rc == NJ_AV_ERR_AGAIN && step.in_used == 0u && avail == 0u) {
            break;      /* no progress and no more input: give up */
        }
    }
    ops->close(dec);
    free(pcm);
}

/* ------------------------------------------------------------------ */
/* tests                                                               */
/* ------------------------------------------------------------------ */

static void test_ops(void)
{
    ok(nj_av_audio_mp3.name != NULL, "ops.name");
    ok(nj_av_audio_mp3.format == NJ_AV_AUDIO_MP3, "ops.format");
    ok(nj_av_audio_mp3.accepts != NULL, "ops.accepts");
    ok(nj_av_audio_mp3.open != NULL, "ops.open");
    ok(nj_av_audio_mp3.decode != NULL, "ops.decode");
    ok(nj_av_audio_mp3.close != NULL, "ops.close");
    ok(nj_av_audio_mp3.pcm_frame_bytes == 4u, "ops.pcm_frame_bytes == 4");
#if NJ_MP3_ENABLE_SEEK
    ok(nj_av_audio_mp3.seek != NULL, "ops.seek present when seeking enabled");
#else
    ok(nj_av_audio_mp3.seek == NULL, "ops.seek NULL when seeking trimmed out");
#endif
}

static void test_accepts(void)
{
    nj_u1 h[4];
    nj_u1 buf[2048];

    memset(buf, 0, sizeof(buf));
    put_header(h, HDR_MPEG1, 9, 0, 1, 2);
    ok(nj_av_audio_mp3.accepts(h, 4) == 1, "accepts MPEG-1 Layer III");

    put_header(h, HDR_MPEG2, 8, 0, 1, 0);
    ok(nj_av_audio_mp3.accepts(h, 4) == 1, "accepts MPEG-2 Layer III");

    put_header(h, HDR_MPEG25, 1, 0, 3, 0);
    ok(nj_av_audio_mp3.accepts(h, 4) == 1, "accepts MPEG-2.5 Layer III");

    /* Layer I and II are not implemented, whatever LAYER3_ONLY says. */
    put_header(h, HDR_MPEG1, 9, 0, 1, 0);
    h[1] = (nj_u1)(h[1] & ~0x06u);          /* layer I */
    ok(nj_av_audio_mp3.accepts(h, 4) == 0, "rejects Layer I");
    h[1] = (nj_u1)((h[1] & ~0x06u) | 0x04u);/* layer II */
    ok(nj_av_audio_mp3.accepts(h, 4) == 0, "rejects Layer II");

    put_header(h, HDR_MPEG1, 15, 0, 1, 0);  /* bitrate index 15 is invalid */
    ok(nj_av_audio_mp3.accepts(h, 4) == 0, "rejects invalid bitrate index");

    put_header(h, HDR_MPEG1, 9, 3, 1, 0);   /* sampling rate index 3 invalid */
    ok(nj_av_audio_mp3.accepts(h, 4) == 0, "rejects invalid samplerate index");

    put_header(h, HDR_MPEG1, 0, 0, 1, 0);   /* free format */
    ok(nj_av_audio_mp3.accepts(h, 4) == 0, "rejects free-format bitrate");

    ok(nj_av_audio_mp3.accepts(buf, 3) == 0, "rejects short buffer");
    ok(nj_av_audio_mp3.accepts(buf, 0) == 0, "rejects empty buffer");

    /* A short valid stream followed by junk must still be accepted. */
    {
        nj_u4 n = build_silence(buf, sizeof(buf), HDR_MPEG1, 9, 0, 1, 3);

        ok(n != 0, "synthetic MPEG-1 frames built");
        memset(buf + n, 0, 32);
        ok(nj_av_audio_mp3.accepts(buf, n + 32) == 1,
           "accepts stream with trailing garbage");
    }
}

static void test_mpeg1_open_and_decode(void)
{
    enum { FRAMES = 8 };
    nj_u1 *buf;
    nj_u4 len;
    run_stat st;
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;

    buf = (nj_u1 *)calloc(FRAMES * 418u + 64u, 1);
    len = build_silence(buf, FRAMES * 418u, HDR_MPEG1, 9, 0, 1, FRAMES);
    ok(len == FRAMES * 417u, "MPEG-1 frame size is 417 bytes");

    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    dec = nj_av_audio_mp3.open(buf, len, &mem, &info, &err);
    ok(dec != NULL, "open MPEG-1 stereo silence");
    if (dec != NULL) {
        ok(info.format == NJ_AV_AUDIO_MP3, "info.format");
        ok(info.rate == 44100u, "info.rate");
        ok(info.channels == 2u, "info.channels");
        okf(info.frames == FRAMES * 1152u, "info.frames %ld (want %ld)",
            (long)info.frames, (long)(FRAMES * 1152u));
        okf(info.duration_ms == (FRAMES * 1152u * 1000u) / 44100u,
            "info.duration_ms %ld (want %ld)", (long)info.duration_ms,
            (long)((FRAMES * 1152u * 1000u) / 44100u));
        ok(info.bitrate == 128u, "info.bitrate");
        nj_av_audio_mp3.close(dec);
    }

    run_decode(&nj_av_audio_mp3, buf, len, 100u, &st);
    okf(st.frames == FRAMES * 1152u, "streamed PCM frames %ld (want %ld)",
        (long)st.frames, (long)(FRAMES * 1152u));
    ok(st.bytes == st.frames * 4u, "PCM bytes == frames * 4");
    ok(st.last_rc == NJ_AV_ERR_END, "decode ends with NJ_AV_ERR_END");
    ok(st.again > 0u, "small input chunks produce NJ_AV_ERR_AGAIN");
    ok(st.silent, "all-zero side information decodes to silence");
    ok(st.time_monotonic, "time_us is monotonic");

    /* One large step must yield the same amount of PCM. */
    run_decode(&nj_av_audio_mp3, buf, len, len, &st);
    okf(st.frames == FRAMES * 1152u, "single-step PCM frames %ld (want %ld)",
        (long)st.frames, (long)(FRAMES * 1152u));
    ok(st.silent, "single-step decode is silent too");

    free(buf);
}

static void test_mpeg2_and_mono(void)
{
    enum { FRAMES = 4 };
    nj_u1 *buf;
    nj_u4 len;
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    run_stat st;

    buf = (nj_u1 *)calloc(FRAMES * 512u + 64u, 1);

    /* MPEG-2, 64 kbit/s, 22.05 kHz, stereo */
    len = build_silence(buf, FRAMES * 512u, HDR_MPEG2, 8, 0, 1, FRAMES);
    ok(len == FRAMES * 208u, "MPEG-2 frame size is 208 bytes");
    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    dec = nj_av_audio_mp3.open(buf, len, &mem, &info, &err);
    ok(dec != NULL, "open MPEG-2 stereo silence");
    if (dec != NULL) {
        ok(info.rate == 22050u, "MPEG-2 info.rate");
        ok(info.channels == 2u, "MPEG-2 info.channels");
        okf(info.frames == FRAMES * 576u, "MPEG-2 frames %ld (want %ld)",
            (long)info.frames, (long)(FRAMES * 576u));
        nj_av_audio_mp3.close(dec);
    }
    run_decode(&nj_av_audio_mp3, buf, len, 64u, &st);
    okf(st.frames == FRAMES * 576u, "MPEG-2 streamed frames %ld (want %ld)",
        (long)st.frames, (long)(FRAMES * 576u));
    ok(st.silent, "MPEG-2 silence stays silent");

    /* MPEG-1 mono, 64 kbit/s, 44.1 kHz: 17 byte side information */
    len = build_silence(buf, FRAMES * 512u, HDR_MPEG1, 5, 0, 3, FRAMES);
    ok(len == FRAMES * 208u, "MPEG-1 mono frame size is 208 bytes");
    dec = nj_av_audio_mp3.open(buf, len, &mem, &info, &err);
    ok(dec != NULL, "open MPEG-1 mono silence");
    if (dec != NULL) {
        ok(info.channels == 1u, "mono info.channels");
        ok(nj_av_audio_mp3.pcm_frame_bytes == 4u, "pcm_frame_bytes unchanged");
        nj_av_audio_mp3.close(dec);
    }
    run_decode(&nj_av_audio_mp3, buf, len, 4096u, &st);
    okf(st.frames == FRAMES * 1152u, "mono streamed frames %ld (want %ld)",
        (long)st.frames, (long)(FRAMES * 1152u));
    ok(st.silent, "mono silence stays silent");

    free(buf);
}

static void test_id3(void)
{
    enum { FRAMES = 3 };
    nj_u1 *buf;
    nj_u1 *tagged;
    nj_u4 len, tlen;
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;
    run_stat st;

    buf = (nj_u1 *)calloc(FRAMES * 418u + 64u, 1);
    len = build_silence(buf, FRAMES * 418u, HDR_MPEG1, 9, 0, 1, FRAMES);
    ok(len != 0u, "ID3 base stream built");

    /* 20 byte ID3v2 tag (10 byte header + 10 byte body) in front of the audio,
     * and a 128 byte ID3v1 trailer behind it. */
    tlen = 20u + len + 128u;
    tagged = (nj_u1 *)calloc(tlen, 1);
    memcpy(tagged, "ID3\x03\x00\x00", 6);
    tagged[6] = 0;
    tagged[7] = 0;
    tagged[8] = 0;
    tagged[9] = 10;             /* syncsafe size: 10 bytes follow */
    memcpy(tagged + 20, buf, len);
    memcpy(tagged + 20 + len, "TAG", 3);

    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    dec = nj_av_audio_mp3.open(tagged, tlen, &mem, &info, &err);
#if NJ_MP3_ENABLE_ID3
    ok(dec != NULL, "open stream with ID3v2 tag and ID3v1 trailer");
    if (dec != NULL) {
        okf(info.frames == FRAMES * 1152u,
            "ID3 frames %ld (want %ld)", (long)info.frames,
            (long)(FRAMES * 1152u));
        nj_av_audio_mp3.close(dec);
    }
#else
    /* Without tag parsing the decoder resyncs on the first valid frame, so the
     * stream is still found - only the tag bytes are skipped by the scanner. */
    ok(dec != NULL, "open tagged stream by resync (ID3 parsing trimmed out)");
    if (dec != NULL) {
        nj_av_audio_mp3.close(dec);
    }
#endif
    if (dec != NULL) {
        run_decode(&nj_av_audio_mp3, tagged, tlen, 256u, &st);
        okf(st.frames == FRAMES * 1152u,
            "tagged streamed frames %ld (want %ld)", (long)st.frames,
            (long)(FRAMES * 1152u));
        ok(st.silent, "tagged stream still silent");
    }
    free(tagged);
    free(buf);
}

static void test_seek(void)
{
    enum { FRAMES = 40 };
    nj_u1 *buf;
    nj_u4 len;
    nj_av_mem mem;
    nj_av_info info;
    nj_av_dec *dec;
    const char *err = NULL;
    nj_u1 *pcm;

    buf = (nj_u1 *)calloc(FRAMES * 418u + 64u, 1);
    len = build_silence(buf, FRAMES * 418u, HDR_MPEG1, 9, 0, 1, FRAMES);
    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    dec = nj_av_audio_mp3.open(buf, len, &mem, &info, &err);
    ok(dec != NULL, "open 40 frame stream for seeking");
    pcm = (nj_u1 *)malloc(16384);
    if (dec != NULL && pcm != NULL) {
#if NJ_MP3_ENABLE_SEEK
        int rc = nj_av_audio_mp3.seek(dec, 500000);    /* 0.5 s */

        ok(rc == NJ_AV_OK, "seek to 500 ms returns NJ_AV_OK");
        {
            int got = 0;
            while (got < 4) {
                nj_av_step step;

                memset(&step, 0, sizeof(step));
                step.in = NULL;
                step.in_len = 0;
                step.out = pcm;
                step.out_cap = 16384u;
                if (nj_av_audio_mp3.decode(dec, &step) == NJ_AV_ERR_END) {
                    break;
                }
                if (step.frames == 0u) {
                    break;
                }
                if (got == 0) {
                    okf(step.time_us >= 500000 && step.time_us < 560000,
                        "time_us after seek %ld (want ~500000)", (long)step.time_us,
                        (long)500000);
                }
                got++;
            }
            ok(got > 0, "decoding continues after seek");
        }
        ok(nj_av_audio_mp3.seek(dec, -5) == NJ_AV_OK, "seek clamps negative time");
        ok(nj_av_audio_mp3.seek(dec, 900000000) == NJ_AV_OK,
           "seek beyond the end is clamped");
#else
        ok(nj_av_audio_mp3.seek == NULL, "no seek op when trimmed out");
#endif
    }
    free(pcm);
    if (dec != NULL) {
        nj_av_audio_mp3.close(dec);
    }
    free(buf);
}

static void test_rejects_bad_stream(void)
{
    nj_u1 junk[512];
    nj_av_mem mem;
    nj_av_info info;
    const char *err = NULL;
    nj_av_dec *dec;

    memset(junk, 0, sizeof(junk));
    mem.ud = NULL;
    mem.alloc = t_alloc;
    mem.free = t_free;
    ok(nj_av_audio_mp3.accepts(junk, sizeof(junk)) == 0,
       "accepts rejects a buffer of zeros");
    dec = nj_av_audio_mp3.open(junk, sizeof(junk), &mem, &info, &err);
    ok(dec == NULL, "open fails on a buffer without a frame");
    ok(err != NULL, "open reports an error string");
    ok(nj_av_audio_mp3.open(NULL, 0, &mem, &info, &err) == NULL,
       "open(NULL, 0) fails");
}

int main(void)
{
    printf("test_mp3: MPEG audio Layer III provider\n");
    test_ops();
    test_accepts();
    test_mpeg1_open_and_decode();
    test_mpeg2_and_mono();
    test_id3();
    test_seek();
    test_rejects_bad_stream();

    printf("  %d checks, %d failure(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
