/* SPDX-License-Identifier: MIT
 * nano_j2me - nj_amr.c
 *
 * AMR narrowband speech: the decoder behind Manager.createPlayer() for the
 * voice clips in the collection.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------------
 * 182 assets in tests/games/, across 22 titles, are AMR: a "#!AMR\n" header
 * and a stream of 20 ms frames.  Sixty-three of them are not even named
 * ".amr" - The Sims 3 ships fourteen of them as "ck" through "cy" - so they
 * announce themselves only by their first six bytes, and the runtime's
 * content sniffer does find them.  What it then did was answer "this runtime
 * has no AMR decoder", which is what a sound loader reads as a failed
 * createPlayer().  A loader that checks the return value plays on in silence;
 * one that does not - and The Sims 3 is one that does not - dereferences the
 * null Player and throws a NullPointerException, once per sound effect, for
 * as long as the game runs.  Neither outcome is the title's fault.
 *
 * ---------------------------------------------------------------------------
 * WHAT IT IS
 * ---------------------------------------------------------------------------
 * An AMR-NB decoder: all eight speech modes (4.75 to 12.2 kbit/s),
 * the RFC 4867 frame header, the five split-vector LSF quantisers and the
 * three-way one, LSF interpolation and the LSP to LPC conversion, the
 * adaptive and algebraic codebooks with their per-mode track maps, gain
 * dequantisation with the fixed gain smoothing and adaptive phase dispersion
 * of the standard, the formant postfilter, the tilt compensation and the
 * high-pass filter. The implementation uses floating point; it does not
 * claim bit-exact arithmetic with the fixed-point 3GPP TS 26.073 reference.
 *
 * Runtime implementation: MIT. The bounded authorship audit and independent
 * replacements based on TS 26.090 v17.0.0 equations are recorded in
 * docs/notes/2026-11-12-amr-independent-implementation.md, including retained
 * arithmetic, format facts, compatibility policies and validation limits.
 * Generated data is separately Apache-2.0 where marked; this SPDX identifier
 * applies to this runtime source, not to the combined decoder's data.
 *
 * Receive-side SID/DTX follows TS 26.092/26.093: SID spectrum and energy,
 * interpolation, history variation, sparse random excitation, filter-gain
 * normalization and missing-update muting. This floating implementation is
 * not fixed-point conformance: low-level noise rounding, spectral variation
 * control and speech/DTX transition state differ from the reference.
 * Q=0 payload is never decoded as good speech or used as a SID model.
 * Speech erasures use conservative muting, not the full TS 26.091 PLC.
 * There is no encoder and no AMR-WB implementation.
 *
 * ---------------------------------------------------------------------------
 * OUTPUT
 * ---------------------------------------------------------------------------
 * Mono, 16-bit signed, little-endian, NJ_AMR_RATE samples per second - the
 * shape nj_port.pcm_write() takes, and the shape a WAV asset already has.
 * The whole file is decoded up front, once, at createPlayer() time: after
 * that an AMR Player and a WAV Player are the same object, which is the
 * simplest integration there is.  Five minutes of speech is 4.8 MB, and
 * nothing in the collection is anywhere near that (the longest is 135
 * seconds), so the cap below is a bound on a malformed file, not on an asset.
 *
 * The samples go into a buffer the caller allocates (nj_amr.h): this file
 * allocates nothing at all, which is what lets the media layer size a Java
 * byte array once and decode straight into it.
 *
 * ---------------------------------------------------------------------------
 * TURNING IT OFF
 * ---------------------------------------------------------------------------
 * -DNJ_ENABLE_AMR=0 compiles the decoder out and createPlayer() answers an AMR
 * stream the way it did before this file existed.  Most of what it costs is
 * nj_amr_tables.h - a hundred kilobytes of the standard's codebooks - so a
 * build with no voice clips in it has something real to save.
 *
 * ---------------------------------------------------------------------------
 * THE TABLES
 * ---------------------------------------------------------------------------
 * nj_amr_tables.h is rebuilt by tools/gen_amr_tables.py from hash-pinned
 * Apache-2.0 OpenCORE numeric data and original formula output. See
 * docs/AMR_DATA_LICENSES.md, docs/licenses/AMR-Apache-2.0.txt, and
 * docs/licenses/AMR-OpenCORE-NOTICE.txt for provenance and retained notices.
 * No OpenCORE or FFmpeg decoder implementation is incorporated in this source.
 *
 * There is no file-scope mutable state here, deliberately: two VMs in one
 * process must not be able to see each other.
 */

#include "nj_amr.h"
#include "nj_amr_mp4.h"

#include <stddef.h>   /* offsetof: the bit allocation tables are byte offsets */

#if NJ_USE_LIBC
#  include <string.h>
#endif

#if NJ_ENABLE_AMR

/* ------------------------------------------------------------------ */
/* The unpacked frame                                                  */
/* ------------------------------------------------------------------ */

/*
 * The local field layout is retained for the generated parameter maps.
 * NJ_AMR_OFF macros bind map offsets to these structures at compile time;
 * fields have 16-bit storage and each subframe has ten pulse-index slots.
 * This layout originated with the historical decoder; its simple parameter
 * grouping is retained as an internal data interface, not upstream code.
 */
typedef struct nj_amr_subframe {
    nj_u2 p_lag;        /* pitch delay index */
    nj_u2 p_gain;       /* pitch gain index */
    nj_u2 fixed_gain;   /* codebook gain index */
    nj_u2 pulses[10];   /* algebraic codebook indices */
} nj_amr_subframe;

typedef struct nj_amr_frame {
    nj_u2 lsf[5];       /* LSF split vector indices */
    nj_amr_subframe subframe[4];
} nj_amr_frame;

#define NJ_AMR_OFF(field)     ((nj_u1)offsetof(nj_amr_frame, field))
#define NJ_AMR_OFF_SUB(n, f)  ((nj_u1)offsetof(nj_amr_frame, subframe[n].f))

#include "nj_amr_tables.h"
#include "nj_amr_dtx.h"

/* ------------------------------------------------------------------ */
/* Constants (3GPP TS 26.090, TS 26.073)                               */
/* ------------------------------------------------------------------ */

/* Frame modes as they appear in the frame header's four bit frame type. */
enum {
    NJ_AMR_MODE_4K75    = 0,
    NJ_AMR_MODE_5K15    = 1,
    NJ_AMR_MODE_5K9     = 2,
    NJ_AMR_MODE_6K7     = 3,
    NJ_AMR_MODE_7K4     = 4,
    NJ_AMR_MODE_7K95    = 5,
    NJ_AMR_MODE_10K2    = 6,
    NJ_AMR_MODE_12K2    = 7,
    NJ_AMR_MODE_SID     = 8,    /* comfort noise: STI and Q classify the SID */
    NJ_AMR_MODE_NO_DATA = 15    /* keep CN active, or mute a speech erasure */
};

#define NJ_AMR_MAGIC_LEN 6u     /* "#!AMR\n" */

/* The frame count a file may claim.  A five minute clip at 20 ms a frame. */
#define NJ_AMR_MAX_FRAMES 15000u

#define NJ_AMR_PITCH_DELAY_MAX 143
#define NJ_AMR_PITCH_DELAY_MIN 20

/* Lower bound on the decoded lag in 12.2 kbit/s mode, TS 26.090 5.7. */
#define NJ_AMR_PITCH_LAG_MIN_12K2 18

/* LSF prediction: the 12.2 mode predicts from the last residual with a fixed
 * factor, every other mode through nj_amr_pred_fac. */
#define NJ_AMR_PRED_FAC_12K2 0.65f
#define NJ_AMR_LSF_R_FAC     (8000.0f / 32768.0f)
#define NJ_AMR_MIN_LSF_SPACING (50.0488f / 8000.0f)

#define NJ_AMR_MIN_ENERGY  (-14.0f)

/* Pitch sharpening is capped; above this the excitation rings. */
#define NJ_AMR_SHARP_MAX 0.79449462890625f

/* Postfilter and high-pass geometry. */
#define NJ_AMR_TILT_RESPONSE 22
#define NJ_AMR_TILT_GAMMA_T  0.8f
#define NJ_AMR_AGC_ALPHA     0.9f

#define NJ_AMR_SAMPLE_BOUND 32768.0f
#define NJ_AMR_SAMPLE_SCALE (2.0f / 32768.0f)

/*
 * The per-mode bit allocation tables, as pointers, because the mode is a
 * number and the decoder wants a table.  Every mode that can be unpacked has
 * one; a SID or no-data frame has no payload to unpack and never gets here.
 */
static const nj_u1 *const nj_amr_order_by_mode[NJ_AMR_MODES] = {
    nj_amr_order_4k75, nj_amr_order_5k15, nj_amr_order_5k9, nj_amr_order_6k7,
    nj_amr_order_7k4,  nj_amr_order_7k95, nj_amr_order_10k2, nj_amr_order_12k2,
    NULL
};

/* The three phase dispersion filters, and the pair each mode may choose from. */
static const float *const nj_amr_ir_lookup[2] = {
    nj_amr_ir_filter_strong, nj_amr_ir_filter_medium
};
static const float *const nj_amr_ir_lookup_7k95[2] = {
    nj_amr_ir_filter_strong_7k95, nj_amr_ir_filter_medium
};

/* ------------------------------------------------------------------ */
/* The four transcendentals                                            */
/* ------------------------------------------------------------------ */
/*
 * The reference decoder leans on libm for sqrt, exp10, log10 and cos.  This
 * one cannot: NJ_ENABLE_FLOAT=0 drops every libm transcendental from the
 * runtime (include/nj2me/nj_libc.h), and an AMR asset should not stop
 * decoding because a build configured itself for a target without them.
 *
 * Each is a handful of lines at double precision, which is more accuracy than
 * the decoder can use - the reference stores LSPs as double and everything
 * else as float, so an error of 1e-15 disappears in the first rounding.
 */

/* Square root by Newton's method after scaling into [1,4), where the seed
 * below is within 15% of the root: each step doubles the digits, so five of
 * them leave nothing for a double to hold. */
static double nj_amr_sqrt(double x)
{
    double r;
    int    e = 0;

    if (x <= 0.0)
        return 0.0;

    while (x >= 4.0 && e < 40) { x *= 0.25; e++; }
    while (x < 1.0 && e > -40) { x *= 4.0;  e--; }

    r = 0.5 * (x + 1.0);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);
    r = 0.5 * (r + x / r);

    while (e > 0) { r *= 2.0; e--; }
    while (e < 0) { r *= 0.5; e++; }
    return r;
}

/* 10^x = 2^(x*log2 10), with the fractional part through a Taylor series on
 * |r| <= log(2)/2 (fifteen terms is 1e-16 relative) and the integer part by
 * scaling - the exponent here never leaves [-20, 20]. */
static double nj_amr_exp10(double x)
{
    const double log2_10 = 3.32192809488736234787;
    const double ln2     = 0.69314718055994530942;
    double t = x * log2_10;
    double r, s = 1.0, term = 1.0;
    int    k = (int)(t >= 0.0 ? t + 0.5 : t - 0.5);
    int    i;

    r = (t - (double)k) * ln2;

    for (i = 1; i <= 15; i++) {
        term *= r / (double)i;
        s += term;
    }

    while (k > 0) { s *= 2.0; k--; }
    while (k < 0) { s *= 0.5; k++; }
    return s;
}

/* ln(x) with the exponent pulled out (x = m * 2^e, m in [1,2)) and the
 * mantissa through atanh: ln(m) = 2*atanh(z), z = (m-1)/(m+1) <= 1/3, where
 * thirteen terms are 1e-15 relative. */
static double nj_amr_log10(double x)
{
    const double ln2      = 0.69314718055994530942;
    const double inv_ln10 = 0.43429448190325182765;
    double z, z2, s;
    int    e = 0, i;

    if (x <= 0.0)
        return -60.0;   /* the reference would say -inf; a zero fixed gain
                           factor is silence, and -60 dB is silence */

    while (x >= 2.0 && e < 40) { x *= 0.5; e++; }
    while (x < 1.0 && e > -40) { x *= 2.0; e--; }

    z  = (x - 1.0) / (x + 1.0);
    z2 = z * z;

    s = 1.0 / 25.0;                          /* the series, Horner, from the
                                                smallest term upwards */
    for (i = 23; i >= 1; i -= 2)
        s = s * z2 + 1.0 / (double)i;
    s *= z;                                  /* sum z^(2j+1)/(2j+1) */

    return ((double)e * ln2 + 2.0 * s) * inv_ln10;
}

/* cos(x) by quadrant reduction and Taylor series on |r| <= pi/4, where the
 * two polynomials below are a few 1e-16 short of exact. */
static double nj_amr_cos(double x)
{
    const double half_pi    = 1.57079632679489661923;
    const double two_over_pi = 0.63661977236758134308;
    double r, r2, c, s, t;
    int    q;

    /* Nearest quadrant, rounded towards minus infinity: (int) truncates
     * towards zero, which would put every negative angle one quadrant out. */
    t = x * two_over_pi + 0.5;
    q = (int)t;
    if (t < 0.0 && (double)q != t)
        q--;

    r  = x - (double)q * half_pi;
    r2 = r * r;

    c = 1.0 / 87178291200.0;
    c = c * r2 - 1.0 / 479001600.0;
    c = c * r2 + 1.0 / 3628800.0;
    c = c * r2 - 1.0 / 40320.0;
    c = c * r2 + 1.0 / 720.0;
    c = c * r2 - 1.0 / 24.0;
    c = c * r2 + 0.5;
    c = c * r2 - 1.0;
    c = -c;                                    /* cos(r) */

    s = 1.0 / 1307674368000.0;
    s = s * r2 - 1.0 / 6227020800.0;
    s = s * r2 + 1.0 / 39916800.0;
    s = s * r2 - 1.0 / 362880.0;
    s = s * r2 + 1.0 / 5040.0;
    s = s * r2 - 1.0 / 120.0;
    s = s * r2 + 1.0 / 6.0;
    s = s * r2 - 1.0;
    s = -s * r;                                /* sin(r) */

    switch (((q % 4) + 4) % 4) {
    case 0:  return  c;
    case 1:  return -s;
    case 2:  return -c;
    default: return  s;
    }
}

/* Round toward zero without an out-of-range float-to-int conversion.
 * Every binary32 value at or above 2^23 in magnitude is already integral;
 * damaged payloads can drive the excitation beyond the signed-int range. */
static float nj_amr_truncf(float x)
{
    if (x >= 8388608.0f || x <= -8388608.0f)
        return x;
    return (float)(int)x;
}

/* ------------------------------------------------------------------ */
/* Elementary arithmetic retained after bounded authorship review      */
/* ------------------------------------------------------------------ */

static float nj_amr_dot(const float *a, const float *b, int n)
{
    float sum = 0.0f;
    int   i;

    for (i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

static void nj_amr_weighted_sum(float *out, const float *a, const float *b,
                                float wa, float wb, int n)
{
    int i;

    for (i = 0; i < n; i++)
        out[i] = wa * a[i] + wb * b[i];
}

/* TS 26.090 5.6.1 (40): two ten-term sides of b60 at sixth-sample
 * spacing. Keep the pairwise accumulation order and permit output to extend
 * the input history, as required when a pitch period is below 40 samples. */
static void nj_amr_interpolate_pitch(float *out, const float *origin, int phase)
{
    int sample, tap;
    for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample) {
        float value = 0.0f;
        for (tap = 0; tap < 10; ++tap) {
            value += origin[sample + tap] * nj_amr_b60_sinc[6 * tap + phase];
            value += origin[sample - tap - 1] * nj_amr_b60_sinc[6 * (tap + 1) - phase];
        }
        out[sample] = value;
    }
}

/* Second order transfer function: the high-pass at the end of a frame. */
static void nj_amr_order_2(float *out, const float *in,
                           const float zero_coeffs[2],
                           const float pole_coeffs[2],
                           float gain, float mem[2], int n)
{
    int   i;
    float tmp;

    for (i = 0; i < n; i++) {
        tmp = gain * in[i] - pole_coeffs[0] * mem[0] - pole_coeffs[1] * mem[1];
        out[i] = tmp + zero_coeffs[0] * mem[0] + zero_coeffs[1] * mem[1];

        mem[1] = mem[0];
        mem[0] = tmp;
    }
}

/* TS 26.090 (79): y[n] = x[n] - sum a[k] y[n-k]. The caller provides
 * ten history samples before out. This elementary recurrence is retained;
 * the left-to-right binary32 subtraction order is part of PCM compatibility. */
static void nj_amr_lp_synthesis(float *out, const float *coeffs,
                                const float *in, int length, int order)
{
    int i, n;

    for (n = 0; n < length; n++) {
        out[n] = in[n];
        for (i = 1; i <= order; i++)
            out[n] -= coeffs[i - 1] * out[n - i];
    }
}

/* ------------------------------------------------------------------ */
/* The sparse fixed vector and the decoded state                       */
/* ------------------------------------------------------------------ */

/*
 * The algebraic codebook vector before it becomes a signal: up to ten pulses,
 * each a position and a sign, plus the pitch sharpening that repeats them
 * down the subframe. Every supported pulse participates in pitch sharpening.
 */
typedef struct nj_amr_fixed {
    int    n;
    nj_u1  x[10];
    float  y[10];
    int    pitch_lag;
    float  pitch_fac;
} nj_amr_fixed;

/*
 * Decoder state.  One of these per file, on the stack: nothing here outlives
 * a decode, and nothing here is shared, which is what lets two VMs decode two
 * clips at once.
 *
 * excitation_buf carries 143 samples of history, then the ten the synthesis
 * filter looks back through, then the current subframe: a pitch lag can reach
 * 143 and the interpolation reads ten samples either side of it, so the
 * history is exactly as long as it has to be and no longer.
 */
typedef struct nj_amr_state {
    nj_amr_frame frame;
    int          mode;

    float  prev_lsf_r[NJ_AMR_LPC_ORDER];
    double lsp[4][NJ_AMR_LPC_ORDER];
    double prev_lsp_sub4[NJ_AMR_LPC_ORDER];
    float  lsf_q[4][NJ_AMR_LPC_ORDER];
    float  lsf_avg[NJ_AMR_LPC_ORDER];
    float  lpc[4][NJ_AMR_LPC_ORDER];

    int    pitch_lag_int;
    float  excitation_buf[NJ_AMR_PITCH_DELAY_MAX + NJ_AMR_LPC_ORDER + 1 +
                          NJ_AMR_SUBFRAME];
    float *excitation;
    float  pitch_vector[NJ_AMR_SUBFRAME];
    float  fixed_vector[NJ_AMR_SUBFRAME];

    float  prediction_error[4];
    float  pitch_gain[5];
    float  fixed_gain[5];
    float  beta;
    int    diff_count;
    int    hang_count;
    float  prev_sparse_fixed_gain;
    int    prev_ir_filter_nr;
    int    ir_filter_onset;

    float  postfilter_mem[NJ_AMR_LPC_ORDER];
    float  tilt_mem;
    float  postfilter_agc;
    float  high_pass_mem[2];
    float  samples_in[NJ_AMR_LPC_ORDER + NJ_AMR_SUBFRAME];

    /* TS 26.092/26.093 receive-side comfort noise. These histories never
     * participate in an uninterrupted good-speech path. Log energy is log2
     * RMS in the synthesis signal's integer PCM scale, not dB or log power. */
    int cn_active, cn_valid, cn_since_sid, cn_period, cn_hist_pos;
    int cn_speech_frames, cn_mode;
    nj_u4 cn_seed;
    double cn_log_rms, cn_old_log_rms, cn_adjust;
    double cn_lsp[10], cn_old_lsp[10];
    float cn_lsf_hist[8][10], cn_log_hist[8], cn_variation[8][10];
} nj_amr_state;

static void nj_amr_state_init(nj_amr_state *p)
{
    int i;

    memset(p, 0, sizeof(*p));

    /* The pitch interpolation reads backwards from here; this is the one
     * place the pointer is set, so it cannot drift. */
    p->excitation = &p->excitation_buf[NJ_AMR_PITCH_DELAY_MAX +
                                       NJ_AMR_LPC_ORDER + 1];

    for (i = 0; i < NJ_AMR_LPC_ORDER; i++) {
        p->prev_lsp_sub4[i] = (double)(nj_amr_lsp_sub4_init[i] * 1000) / 32768.0;
        p->lsf_avg[i] = p->lsf_q[3][i] =
            (float)((double)nj_amr_lsp_avg_init[i] / 32768.0);
    }

    for (i = 0; i < 4; i++)
        p->prediction_error[i] = NJ_AMR_MIN_ENERGY;

    p->cn_seed = 0x70816958u;
    p->cn_period = 8;
    p->cn_mode = NJ_AMR_MODE_12K2;
    p->cn_log_rms = p->cn_old_log_rms = 3500.0 / 2048.0;
    for (i = 0; i < 10; ++i) {
        int frame;
        double f = nj_amr_lsf_3_mean[i] / 8000.0;
        p->cn_lsp[i] = p->cn_old_lsp[i] =
            nj_amr_cos(6.2831853071795864769 * f);
        for (frame = 0; frame < 8; ++frame) {
            p->cn_lsf_hist[frame][i] = (float)f;
            p->cn_log_hist[frame] = (float)p->cn_log_rms;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The bitstream                                                       */
/* ------------------------------------------------------------------ */

/*
 * Reorder one frame's payload bits into the unpacked frame.
 *
 * The table (nj_amr_order_*) is a list of fields: a bit count, the byte
 * offset of the field in nj_amr_frame, then one byte per bit giving where
 * that bit sits in the payload. A zero bit count ends the list. Offsets
 * are even because every field is a 16-bit word. `size` is used to clear
 * the destination, not to validate maps: only generated compile-time maps
 * are passed here, after the caller validates the whole payload length.
 */
static void nj_amr_bit_reorder(nj_u2 *out, int size, const nj_u1 *data,
                               const nj_u1 *ord)
{
    int field_size, field, field_offset, bit;

    memset(out, 0, (nj_size)size);

    while ((field_size = *ord++) != 0) {
        field        = 0;
        field_offset = *ord++;

        while (field_size--) {
            bit = *ord++;
            field <<= 1;
            field |= (data[bit >> 3] >> (bit & 7)) & 1;
        }

        out[field_offset >> 1] = (nj_u2)field;
    }
}

/*
 * Read one frame header: the four bit frame type, the quality bit, and the
 * payload that goes with the type.  Returns the mode, or NJ_AMR_MODE_NO_DATA
 * for a frame that is too short to hold what its own type says it does.
 *
 * The caller classifies Q/FT first. Only good speech reaches this unpacker;
 * unusable payloads never update the ordinary speech parameters.
 */
static int nj_amr_unpack(nj_amr_state *p, const nj_u1 *buf, nj_u4 size)
{
    int mode;

    if (size < 1u)
        return NJ_AMR_MODE_NO_DATA;

    mode = (buf[0] >> 3) & 0x0F;

    if (mode >= NJ_AMR_MODES) {
        /* Seven of the sixteen type values are not speech modes: 15 means
         * "no data", the rest are reserved.  Either way there is no payload
         * to unpack. */
        return NJ_AMR_MODE_NO_DATA;
    }

    if (size < (nj_u4)nj_amr_payload_bytes[mode] + 1u)
        return NJ_AMR_MODE_NO_DATA;

    if (mode < NJ_AMR_MODE_SID)
        nj_amr_bit_reorder((nj_u2 *)&p->frame, (int)sizeof(p->frame), buf + 1,
                           nj_amr_order_by_mode[mode]);

    return mode;
}

/* Bytes one frame occupies, header included.  A speech frame is its header plus
 * the mode's payload; a SID frame is six bytes (39 bits of comfort noise plus
 * the header, RFC 4867 Table 1a), and a no-data frame is the header alone. */
static nj_u4 nj_amr_frame_len(int mode)
{
    if (mode < NJ_AMR_MODES)
        return (nj_u4)nj_amr_payload_bytes[mode] + 1u;
    return 1u;   /* no-data, or reserved */
}

/* ------------------------------------------------------------------ */
/* LSF and LSP                                                         */
/* ------------------------------------------------------------------ */

/* Spectral reconstruction, TS 26.090 5.2.5-5.2.6, equations (20)-(28).
 * Work by frequency band: fetch residuals, add the MA predictor, enforce the
 * minimum distance, then interpolate. Frequencies here are cycles/sample.
 * Separate binary32 predictor/mean operations preserve existing PCM rounding. */
static void nj_amr_decode_spectrum(nj_amr_state *p)
{
    const nj_u2 *index = p->frame.lsf;
    const nj_s2 *book[5];
    float frequency[2][NJ_AMR_LPC_ORDER];
    int dual = p->mode == NJ_AMR_MODE_12K2;
    int band, set, sf;

    if (dual) {
        book[0] = nj_amr_lsf_5_1[index[0]];
        book[1] = nj_amr_lsf_5_2[index[1]];
        book[2] = nj_amr_lsf_5_3[index[2] / 2];
        book[3] = nj_amr_lsf_5_4[index[3]];
        book[4] = nj_amr_lsf_5_5[index[4]];
    } else {
        int reduced = p->mode <= NJ_AMR_MODE_5K15;
        book[0] = p->mode == NJ_AMR_MODE_7K95
                ? nj_amr_lsf_3_1_7k95[index[0]] : nj_amr_lsf_3_1[index[0]];
        book[1] = nj_amr_lsf_3_2[index[1] * (reduced ? 2 : 1)];
        book[2] = reduced ? nj_amr_lsf_3_3_5k15[index[2]]
                          : nj_amr_lsf_3_3[index[2]];
    }
    for (band = 0; band < NJ_AMR_LPC_ORDER; ++band) {
        float past = p->prev_lsf_r[band];
        for (set = 0; set <= dual; ++set) {
            int residual;
            float decoded;
            if (dual) {
                float predicted = past * NJ_AMR_LSF_R_FAC *
                                  NJ_AMR_PRED_FAC_12K2 + nj_amr_lsf_5_mean[band];
                residual = book[band / 2][2 * set + band % 2];
                if (band / 2 == 2 && (index[2] & 1u)) residual = -residual;
                decoded = (float)residual * (NJ_AMR_LSF_R_FAC / 8000.0f) +
                          predicted * (1.0f / 8000.0f);
            } else {
                int split = band < 3 ? 0 : (band < 6 ? 1 : 2);
                residual = book[split][band - 3 * split];
                decoded = ((float)residual + past * nj_amr_pred_fac[band]) *
                          (NJ_AMR_LSF_R_FAC / 8000.0f) +
                          nj_amr_lsf_3_mean[band] * (1.0f / 8000.0f);
            }
            frequency[set][band] = decoded;
            if (set == dual) p->prev_lsf_r[band] = (float)residual;
        }
    }
    for (set = 0; set <= dual; ++set) {
        float lower = NJ_AMR_MIN_LSF_SPACING;
        for (band = 0; band < NJ_AMR_LPC_ORDER; ++band) {
            float f = frequency[set][band];
            if (f < lower) f = lower;
            frequency[set][band] = f;
            p->lsp[dual && set == 0 ? 1 : 3][band] =
                nj_amr_cos(2.0 * 3.14159265358979323846 * (double)f);
            lower = f + NJ_AMR_MIN_LSF_SPACING;
        }
    }
    for (band = 0; band < NJ_AMR_LPC_ORDER; ++band) {
        float old = p->lsf_q[3][band];
        double previous = p->prev_lsp_sub4[band];
        for (sf = 0; sf < 4; ++sf) {
            p->lsf_q[sf][band] = 0.25f * (float)(3 - sf) * old +
                0.25f * (float)(sf + 1) * frequency[dual][band];
            if (dual) {
                if (!(sf & 1))
                    p->lsp[sf][band] = 0.5 * (sf == 0 ? previous : p->lsp[1][band]) +
                                      0.5 * p->lsp[sf + 1][band];
            } else if (sf < 3) {
                p->lsp[sf][band] = previous +
                    (p->lsp[3][band] - previous) * 0.25 * (double)(sf + 1);
            }
        }
    }
}

/* TS 26.090 5.2.4: expand the even and odd root products together using
 * its symmetric half-polynomial recurrence, then apply (18)-(19).
 * The recurrence is mathematical specification, including its factor of 2
 * at the moving symmetric midpoint, rather than an imported DSP helper. */
static void nj_amr_lspd2lpc(const double *lsp, float *lpc)
{
    double f[2][6] = {{1.0}, {1.0}};
    int root, parity, degree;
    for (root = 0; root < 5; ++root) {
        for (parity = 0; parity < 2; ++parity) {
            double factor = -2.0 * lsp[2 * root + parity];
            if (root == 0) {
                f[parity][1] = factor;
            } else {
                f[parity][root + 1] = factor * f[parity][root] +
                                      2.0 * f[parity][root - 1];
                for (degree = root; degree >= 2; --degree)
                    f[parity][degree] += f[parity][degree - 1] * factor +
                                          f[parity][degree - 2];
                f[parity][1] += factor;
            }
        }
    }
    for (degree = 1; degree <= 5; ++degree) {
        double symmetric = f[0][degree] + f[0][degree - 1];
        double antisymmetric = f[1][degree] - f[1][degree - 1];
        lpc[degree - 1] = (float)(0.5 * (symmetric + antisymmetric));
        lpc[10 - degree] = (float)(0.5 * (symmetric - antisymmetric));
    }
}

/* ------------------------------------------------------------------ */
/* Pitch delay                                                         */
/* ------------------------------------------------------------------ */

/* Pitch is represented as an integer coordinate in sixths or thirds of a
 * sample. TS 26.090 5.6.1 gives the absolute and relative search intervals.
 * The index numbering and boundary codewords below are format facts retained
 * for interoperability; integer division replaces reciprocal-multiply tricks.
 * Four bits describes an index width, never quarter-sample resolution. */
static int nj_amr_pitch_window(int previous, int below, int width, int minimum)
{
    int start = previous - below;
    if (start < minimum) start = minimum;
    if (start + width > NJ_AMR_PITCH_DELAY_MAX)
        start = NJ_AMR_PITCH_DELAY_MAX - width;
    return start;
}

static void nj_amr_pitch_coordinates(int mode, int subframe, int index,
                                      int previous, int *lag, int *fraction)
{
    int absolute = subframe == 0 || (subframe == 2 && mode > NJ_AMR_MODE_5K15);
    int units, centered;
    if (mode == NJ_AMR_MODE_12K2) {
        if (absolute && index >= 463) {
            *lag = 95 + index - 463;
            *fraction = 0;
            return;
        }
        if (absolute) {
            units = 105 + index;
        } else {
            int start = nj_amr_pitch_window(previous, 5, 9, 18);
            units = 6 * start - 3 + index;
        }
        /* Nearest integer with ties represented by +3/6. */
        centered = units + 2;
        *lag = centered / 6;
        *fraction = units - 6 * *lag;
        return;
    }
    if (absolute) {
        units = index < 197 ? 58 + index : 3 * (85 + index - 197);
    } else {
        int wide = mode == NJ_AMR_MODE_7K95;
        int start = nj_amr_pitch_window(previous, wide ? 10 : 5,
                                        wide ? 19 : 9, 20);
        if (mode <= NJ_AMR_MODE_6K7) {
            /* Four integer slots, eight fractional slots, four integer slots. */
            if (index < 4) units = 3 * (start + index);
            else if (index < 12) units = 3 * start + index + 6;
            else units = 3 * (start + index - 6);
        } else {
            units = 3 * start + index - 2;
        }
    }
    *lag = (units + 1) / 3;
    *fraction = units - 3 * *lag;
}

/*
 * The pitch vector: the past excitation interpolated at the decoded lag with
 * the standard's windowed sinc, at sixth sample resolution.
 */
static void nj_amr_decode_pitch_vector(nj_amr_state *p,
                                       const nj_amr_subframe *subframe,
                                       int subframe_no)
{
    int pitch_lag_int, pitch_lag_frac;

    nj_amr_pitch_coordinates(p->mode, subframe_no, (int)subframe->p_lag,
                              p->pitch_lag_int, &pitch_lag_int, &pitch_lag_frac);
    if (p->mode != NJ_AMR_MODE_12K2) pitch_lag_frac *= 2;

    p->pitch_lag_int = pitch_lag_int;    /* the previous lag, for the next one */

    pitch_lag_int += pitch_lag_frac > 0;

    nj_amr_interpolate_pitch(p->excitation, p->excitation + 1 - pitch_lag_int,
                             pitch_lag_frac + (pitch_lag_frac > 0 ? 0 : 6));

    memcpy(p->pitch_vector, p->excitation,
           NJ_AMR_SUBFRAME * sizeof(float));
}

/* ------------------------------------------------------------------ */
/* The algebraic codebook                                              */
/* ------------------------------------------------------------------ */

/* TS 26.090 5.7.1/Table 4: two pulses on each four-sample track.
 * The compressed codeword permutation is a format fact: two ten-bit words
 * carry three coordinates each, and seven bits carry the last pair. */
static void nj_amr_8_pulses_31bits(const nj_u2 *word, nj_amr_fixed *out)
{
    int coordinate[8], group, digit, pulse;
    static const nj_u1 lane[2][3] = {{0, 4, 1}, {2, 6, 5}};
    for (group = 0; group < 2; ++group) {
        unsigned code = word[4 + group];
        const nj_u1 *base5 = nj_amr_base_five_table[code / 8];
        for (digit = 0; digit < 3; ++digit)
            coordinate[lane[group][digit]] =
                2 * base5[2 - digit] + ((code >> digit) & 1u);
    }
    {
        unsigned pair = ((word[6] / 4) * 25u + 12u) / 32u;
        unsigned row = pair / 5, column = pair % 5;
        if (row & 1u) column = 4 - column;
        coordinate[3] = 2 * column + (word[6] & 1u);
        coordinate[7] = 2 * row + ((word[6] >> 1) & 1u);
    }
    out->n = 8;
    for (pulse = 0; pulse < 8; ++pulse) {
        int track = pulse % 4;
        float amplitude = word[track] ? -1.0f : 1.0f;
        if (pulse >= 4 && coordinate[pulse] < coordinate[track])
            amplitude = -amplitude;
        out->x[pulse] = (nj_u1)(4 * coordinate[pulse] + track);
        out->y[pulse] = amplitude;
    }
}

/* TS 26.090 v17.0.0, 5.7.1/Table 3: five interleaved tracks, two
 * positions per track, with the second sign derived from relative position.
 * The generated position map is separately attributed Apache-2.0 data;
 * the standard's prose alone does not fix that Gray permutation.
 * The local unpacker supplies the sign-bearing word after the second-position
 * word in each pair. Keep that layout and accumulation order for PCM. */
static void nj_amr_10_pulses_35bits(const nj_u2 *fixed_index,
                                    nj_amr_fixed *fixed_sparse)
{
    int pulse;
    fixed_sparse->n = 10;
    for (pulse = 0; pulse < 10; pulse++) {
        unsigned position = nj_amr_gray_decode[fixed_index[pulse] & 7u];
        unsigned first_position = nj_amr_gray_decode[fixed_index[pulse | 1] & 7u];
        float amplitude = (fixed_index[pulse | 1] & 8u) ? -1.0f : 1.0f;
        if (!(pulse & 1) && position < first_position)
            amplitude = -amplitude;
        fixed_sparse->x[pulse] = (nj_u1)(pulse / 2 + position);
        fixed_sparse->y[pulse] = amplitude;
    }
}

/*
 * Turn the codebook indices into pulse positions and signs.  The number of
 * pulses, their tracks and their signs all depend on the mode, and so does
 * how many bits each index has - the bit allocation table is what says.
 */
static void nj_amr_decode_fixed_sparse(nj_amr_fixed *fixed_sparse,
                                       const nj_u2 *pulses, int mode,
                                       int subframe)
{
    if (mode == NJ_AMR_MODE_12K2) {
        nj_amr_10_pulses_35bits(pulses, fixed_sparse);
        return;
    }

    if (mode == NJ_AMR_MODE_10K2) {
        nj_amr_8_pulses_31bits(pulses, fixed_sparse);
        return;
    }

    /* Tables 5-8 specify the track sets. Bit positions within these words
     * follow the existing unpacked wire layout; positions are not code text. */
    {
        unsigned code = pulses[0];
        int count = mode <= NJ_AMR_MODE_5K9 ? 2 :
                    mode == NJ_AMR_MODE_6K7 ? 3 : 4;
        int pulse;
        fixed_sparse->n = count;
        for (pulse = 0; pulse < count; ++pulse) {
            unsigned position, track, shift;
            if (mode <= NJ_AMR_MODE_5K15) {
                unsigned subset = ((code >> 6) & 1u) * 8 + subframe * 2;
                track = nj_amr_track_position[subset + pulse];
                position = 5 * ((code >> (3 * pulse)) & 7u) + track;
            } else if (mode == NJ_AMR_MODE_5K9) {
                track = pulse == 0 ? 1 + 2 * (code & 1u) : (code >> 4) & 3u;
                if (pulse == 1 && track == 3) track = 4;
                shift = pulse == 0 ? 1 : 6;
                position = 5 * ((code >> shift) & 7u) + track;
            } else if (mode == NJ_AMR_MODE_6K7) {
                track = (unsigned)pulse;
                if (pulse) track += 2 * ((code >> (4 * pulse - 1)) & 1u);
                position = 5 * ((code >> (4 * pulse)) & 7u) + track;
            } else {
                shift = pulse == 3 ? 10 : 3 * pulse;
                track = (unsigned)pulse + (pulse == 3 ? (code >> 9) & 1u : 0);
                position = nj_amr_gray_decode[(code >> shift) & 7u] + track;
            }
            fixed_sparse->x[pulse] = (nj_u1)position;
            fixed_sparse->y[pulse] = (pulses[1] & (1u << pulse)) ? 1.0f : -1.0f;
        }
        /* Retain the existing 5.9 collision policy: one pulse, first sign. */
        if (mode == NJ_AMR_MODE_5K9 && fixed_sparse->x[0] == fixed_sparse->x[1])
            fixed_sparse->n = 1;
    }
}

/* Finite geometric pulse train c[n] + beta*c[n-T], TS 26.090 5.7.
 * Visit pulse bases in wire order, retaining rounding for coincident pulses.
 * The caller clears the complete 40-sample work vector between subframes. */
static void nj_amr_set_fixed_vector(float *out, const nj_amr_fixed *in,
                                    float scale, int size)
{
    int pulse;
    if (in->pitch_lag <= 0) return;
    for (pulse = 0; pulse < in->n; ++pulse) {
        int position;
        float amplitude = scale * in->y[pulse];
        for (position = in->x[pulse]; position < size; position += in->pitch_lag) {
            out[position] += amplitude;
            amplitude *= in->pitch_fac;
        }
    }
}

/* Pitch sharpening: the fixed vector is repeated at the pitch lag with a
 * factor that follows the pitch gain. */
static void nj_amr_pitch_sharpening(nj_amr_state *p, int subframe,
                                    nj_amr_fixed *fixed_sparse)
{
    /* The standard suggests the current pitch gain always, but outside the
     * 12.2 kbit/s mode the pitch and codebook gains are quantised together,
     * so the codebook gain cannot depend on a pitch gain not yet decoded. */
    if (p->mode == NJ_AMR_MODE_12K2) {
        p->beta = p->pitch_gain[4] < 1.0f ? p->pitch_gain[4] : 1.0f;
    }

    fixed_sparse->pitch_lag = p->pitch_lag_int;
    fixed_sparse->pitch_fac = p->beta;

    /* 4.75 kbit/s quantises the gains of two subframes together, so its
     * sharpening factor is only updated on the second and fourth. */
    if (p->mode != NJ_AMR_MODE_4K75 || (subframe & 1)) {
        p->beta = p->pitch_gain[4] < 0.0f ? 0.0f :
                 (p->pitch_gain[4] > NJ_AMR_SHARP_MAX ? NJ_AMR_SHARP_MAX
                                                      : p->pitch_gain[4]);
    }
}

/* ------------------------------------------------------------------ */
/* Gains                                                               */
/* ------------------------------------------------------------------ */

/*
 * The codebook gain: the quantised correction factor times the predicted
 * energy, in dB, less the energy of the vector it will scale - which is why
 * the vector has to exist before the gain does.  The prediction error history
 * rolls on regardless.
 */
static float nj_amr_set_fixed_gain(float fixed_gain_factor,
                                   float fixed_mean_energy,
                                   float *prediction_error,
                                   float energy_mean,
                                   const float *pred_table)
{
    /* TS 26.090 (66)-(69): correction * predicted amplitude / RMS(code).
     * Keep the existing four-error chronological history, oldest first. */
    float predicted_db = 0.0f, amplitude, rms;
    int age;
    for (age = 0; age < 4; ++age)
        predicted_db += pred_table[age] * prediction_error[age];
    amplitude = (float)nj_amr_exp10(0.05 * ((double)predicted_db + (double)energy_mean));
    rms = (float)nj_amr_sqrt(fixed_mean_energy == 0.0f ? 1.0 : (double)fixed_mean_energy);
    for (age = 0; age < 3; ++age) prediction_error[age] = prediction_error[age + 1];
    prediction_error[3] = 20.0f * (float)nj_amr_log10((double)fixed_gain_factor);
    return fixed_gain_factor * amplitude / rms;
}

static void nj_amr_decode_gains(nj_amr_state *p,
                                const nj_amr_subframe *subframe,
                                int subframe_no, float *fixed_gain_factor)
{
    /* 5.8.2: scalar or joint gain quantization. The table scales are
     * format units: Q14 pitch, Q11 scalar correction, Q12 joint correction. */
    unsigned pitch, correction;
    float correction_unit = 1.0f / 4096.0f;
    const nj_u2 *joint = NULL;
    switch (p->mode) {
    case NJ_AMR_MODE_4K75:
        joint = nj_amr_gains_4k75[2 * p->frame.subframe[subframe_no / 2 * 2].p_gain +
                                   subframe_no % 2];
        break;
    case NJ_AMR_MODE_5K15:
    case NJ_AMR_MODE_5K9:
        joint = nj_amr_gains_low[subframe->p_gain];
        break;
    case NJ_AMR_MODE_6K7:
    case NJ_AMR_MODE_7K4:
    case NJ_AMR_MODE_10K2:
        joint = nj_amr_gains_high[subframe->p_gain];
        break;
    default: /* 7.95 and 12.2 use separately coded gains. */
        break;
    }
    if (joint) {
        pitch = joint[0];
        correction = joint[1];
    } else {
        pitch = nj_amr_qua_gain_pit[subframe->p_gain];
        correction = nj_amr_qua_gain_code[subframe->fixed_gain];
        correction_unit = 1.0f / 2048.0f;
    }
    p->pitch_gain[4] = (float)pitch * (1.0f / 16384.0f);
    *fixed_gain_factor = (float)correction * correction_unit;
}

/* TS 26.090 v17.0.0, 6.1 step 4, equations (71)-(74): relative spectral
 * distance, five-gain mean and a clamped blend. Equation (70)'s 0.84/0.16
 * spectral mean is updated once per frame by the caller. The elapsed-counter
 * convention and binary32 expression 4*distance-1.6 retain this decoder's PCM
 * contract; they are not a claim of bit-exact 3GPP reference conformance. */
static float nj_amr_fixed_gain_smooth(nj_amr_state *p, const float *lsf,
                                      const float *lsf_avg)
{
    float distance = 0.0f, gain_mean = 0.0f, blend;
    int band, elapsed;
    for (band = 0; band < NJ_AMR_LPC_ORDER; band++) {
        float relative = (lsf_avg[band] - lsf[band]) / lsf_avg[band];
        distance += relative < 0.0f ? -relative : relative;
    }
    p->diff_count = distance > 0.65f ? p->diff_count + 1 : 0;
    if (p->diff_count > 10) {
        p->diff_count = 10;
        p->hang_count = 0;
    }
    elapsed = p->hang_count;
    if (elapsed < 40) p->hang_count = elapsed + 1;
    if (elapsed < 40 || p->mode == NJ_AMR_MODE_7K4 ||
        p->mode == NJ_AMR_MODE_7K95 || p->mode == NJ_AMR_MODE_12K2)
        return p->fixed_gain[4];
    for (band = 0; band < 5; band++) gain_mean += p->fixed_gain[band];
    gain_mean *= 0.2f;
    blend = 4.0f * distance - 1.6f;
    if (blend < 0.0f) blend = 0.0f;
    if (blend > 1.0f) blend = 1.0f;
    return blend * p->fixed_gain[4] + (1.0f - blend) * gain_mean;
}

/* ------------------------------------------------------------------ */
/* Adaptive phase dispersion                                           */
/* ------------------------------------------------------------------ */

/*
 * TS 26.090 6.1 step 5 specifies a 40-sample circular convolution.  Express
 * the sharpened algebraic vector as a sum of pulse bases.  A pulse at x has
 * copies at x+r*lag < 40, with amplitude multiplied by pitch_fac each time.
 * Its contribution at output n is therefore
 *   y * sum_r pitch_fac^r * h[(n-x-r*lag) mod 40].
 * Evaluate each finite polynomial from its last term, then add pulse bases
 * in the unpacker's order to preserve the binary32 PCM contract.  This dense
 * output walk needs neither shifted-filter buffers nor a circular-add helper.
 * The h[] coefficients are separately attributed Apache-2.0 numeric data;
 * see docs/AMR_DATA_LICENSES.md. Their license is not changed by this code.
 */
static void nj_amr_apply_ir_filter(float *out, const nj_amr_fixed *in,
                                   const float *filter)
{
    int sample, pulse;
    for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample) {
        float value = 0.0f;
        for (pulse = 0; pulse < in->n && pulse < 10; ++pulse) {
            int position = in->x[pulse];
            int copies, index;
            float basis;
            if (position >= NJ_AMR_SUBFRAME) continue;
            copies = in->pitch_lag > 0
                   ? (NJ_AMR_SUBFRAME - 1 - position) / in->pitch_lag : 0;
            index = sample - position - copies * in->pitch_lag;
            if (index < 0) index += NJ_AMR_SUBFRAME;
            basis = filter[index];
            while (copies-- > 0) {
                index += in->pitch_lag;
                if (index >= NJ_AMR_SUBFRAME) index -= NJ_AMR_SUBFRAME;
                basis = filter[index] + in->pitch_fac * basis;
            }
            value += in->y[pulse] * basis;
        }
        out[sample] = value;
    }
}

/*
 * Phase selection from TS 26.090 6.1 step 5: gain thresholds, onset, median
 * history and one-step release.  The two-subframe onset hold, quiet gain < 5
 * bypass, and applying the median rule to every non-onset level are retained
 * interoperability policies; the cited prose does not fully specify them.
 * The quiet threshold also occurs as Q1 gain < 10 in pre-FFmpeg OpenCORE.
 * No OpenCORE implementation is incorporated here (its license is Apache-2.0).
 */
static int nj_amr_phase_level(nj_amr_state *p, float gain)
{
    float ordered[5];
    int level, i, j;
    int onset = gain > 2.0f * p->prev_sparse_fixed_gain
              ? 2 : (p->ir_filter_onset > 0 ? p->ir_filter_onset - 1 : 0);

    level = p->pitch_gain[4] < 0.6f ? 0
          : p->pitch_gain[4] < 0.9f ? 1 : 2;
    if (onset > 0) {
        if (level < 2) ++level;
    } else {
        /* Insertion sorting five finite gains computes the stated median. */
        for (i = 0; i < 5; ++i) {
            float v = p->pitch_gain[i];
            j = i;
            while (j > 0 && ordered[j - 1] > v) {
                ordered[j] = ordered[j - 1];
                --j;
            }
            ordered[j] = v;
        }
        if (ordered[2] < 0.6f) level = 0;
        if (level > p->prev_ir_filter_nr + 1)
            level = p->prev_ir_filter_nr + 1;
    }
    if (gain < 5.0f) level = 2;
    p->ir_filter_onset = onset;
    p->prev_ir_filter_nr = level;
    p->prev_sparse_fixed_gain = gain;
    return level;
}

static const float *nj_amr_anti_sparseness(nj_amr_state *p,
                                            nj_amr_fixed *fixed_sparse,
                                            const float *fixed_vector,
                                            float fixed_gain, float *out)
{
    int level = nj_amr_phase_level(p, fixed_gain);
    if (level < 2 && p->mode < NJ_AMR_MODE_10K2 && p->mode != NJ_AMR_MODE_7K4) {
        const float *kernel = p->mode == NJ_AMR_MODE_7K95
                            ? nj_amr_ir_lookup_7k95[level]
                            : nj_amr_ir_lookup[level];
        nj_amr_apply_ir_filter(out, fixed_sparse, kernel);
        return out;
    }
    return fixed_vector;
}

/* ------------------------------------------------------------------ */
/* Synthesis and postfilter                                            */
/* ------------------------------------------------------------------ */

/*
 * One subframe of speech: the excitation is the pitch and codebook
 * contributions summed, with the pitch contribution emphasised when the pitch
 * gain is high, and the short term filter turns it into samples.  Returns
 * whether the result overflowed 16 bits, in which case the caller runs it
 * again with the pitch vector scaled down.
 */
static int nj_amr_synthesis(nj_amr_state *p, float *lpc, float fixed_gain,
                            const float *fixed_vector, float *samples,
                            int overflow)
{
    /* TS 26.090 6.1 (75)-(79): mixture, optional emphasis, energy
     * normalization, then A(z)^-1. Retain the legacy retry policy, which
     * quarters this pitch vector rather than the entire excitation history. */
    float excitation[NJ_AMR_SUBFRAME];
    float energy = 0.0f;
    int sample, exceeded = 0;
    for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample) {
        if (overflow) p->pitch_vector[sample] *= 0.25f;
        excitation[sample] = p->pitch_gain[4] * p->pitch_vector[sample] +
                             fixed_gain * fixed_vector[sample];
        energy += excitation[sample] * excitation[sample];
    }
    if (!overflow && p->pitch_gain[4] > 0.5f) {
        float ceiling = p->mode == NJ_AMR_MODE_12K2 ? 1.0f : NJ_AMR_SHARP_MAX;
        float beta = p->pitch_gain[4] < ceiling ? p->pitch_gain[4] : ceiling;
        float emphasis = p->pitch_gain[4] *
                         ((p->mode == NJ_AMR_MODE_12K2 ? 0.25f : 0.5f) * beta);
        float emphasized_energy = 0.0f, normalization;
        for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample) {
            excitation[sample] += emphasis * p->pitch_vector[sample];
            emphasized_energy += excitation[sample] * excitation[sample];
        }
        normalization = emphasized_energy == 0.0f ? 0.0f :
            (float)nj_amr_sqrt((double)energy / (double)emphasized_energy);
        for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample)
            excitation[sample] *= normalization;
    }
    nj_amr_lp_synthesis(samples, lpc, excitation, NJ_AMR_SUBFRAME, NJ_AMR_LPC_ORDER);
    for (sample = 0; sample < NJ_AMR_SUBFRAME; ++sample)
        if (samples[sample] > NJ_AMR_SAMPLE_BOUND || samples[sample] < -NJ_AMR_SAMPLE_BOUND)
            exceeded = 1;
    return exceeded;
}

/* End of subframe: the histories shift by one subframe, the fourth LSP set
 * becomes the previous frame's. */
static void nj_amr_update_state(nj_amr_state *p)
{
    int i;

    for (i = 0; i < NJ_AMR_LPC_ORDER; i++)
        p->prev_lsp_sub4[i] = p->lsp[3][i];

    memmove(&p->excitation_buf[0], &p->excitation_buf[NJ_AMR_SUBFRAME],
            (nj_size)(NJ_AMR_PITCH_DELAY_MAX + NJ_AMR_LPC_ORDER + 1) *
            sizeof(float));

    for (i = 0; i < 4; i++) {
        p->pitch_gain[i] = p->pitch_gain[i + 1];
        p->fixed_gain[i] = p->fixed_gain[i + 1];
    }

    memmove(&p->samples_in[0], &p->samples_in[NJ_AMR_SUBFRAME],
            (nj_size)NJ_AMR_LPC_ORDER * sizeof(float));
}

/* TS 26.090 6.2 (80)-(86). The formant transfer is B(z)/D(z).
 * Process denominator then numerator to preserve existing binary32 PCM;
 * they commute in real arithmetic. A second recurrence computes its first
 * 22 impulse samples for tilt, without a temporary synthesis invocation. */
static void nj_amr_postfilter(nj_amr_state *p, float *lpc, float *out)
{
    float numerator[10], denominator[10], history[50], impulse[22];
    const float *speech = p->samples_in + NJ_AMR_LPC_ORDER;
    int high = p->mode >= NJ_AMR_MODE_10K2;
    const float *gn = high ? nj_amr_pow_0_7 : nj_amr_pow_0_55;
    const float *gd = high ? nj_amr_pow_0_75 : nj_amr_pow_0_7;
    float r0 = 0.0f, r1 = 0.0f, tilt, filtered_energy = 0.0f;
    float input_energy = 0.0f, previous = p->tilt_mem, target, gain;
    int k, n;
    for (k = 0; k < 10; ++k) {
        numerator[k] = lpc[k] * gn[k];
        denominator[k] = lpc[k] * gd[k];
        history[k] = p->postfilter_mem[k];
    }
    for (n = 0; n < NJ_AMR_TILT_RESPONSE; ++n) {
        float h = n == 0 ? 1.0f : (n <= 10 ? numerator[n - 1] : 0.0f);
        for (k = 1; k <= 10 && k <= n; ++k)
            h -= denominator[k - 1] * impulse[n - k];
        impulse[n] = h;
        r0 += h * h;
        if (n) r1 += impulse[n - 1] * h;
    }
    /* Negative tilt is suppressed in all modes, a retained decoder policy. */
    tilt = r1 >= 0.0f ? r1 / r0 * NJ_AMR_TILT_GAMMA_T : 0.0f;
    for (n = 0; n < NJ_AMR_SUBFRAME; ++n) {
        float v, z;
        input_energy += speech[n] * speech[n];
        history[n + 10] = speech[n];
        for (k = 1; k <= 10; ++k)
            history[n + 10] -= denominator[k - 1] * history[n + 10 - k];
        z = history[n + 10];
        for (k = 1; k <= 10; ++k)
            z += numerator[k - 1] * history[n + 10 - k];
        v = z - tilt * previous;
        previous = z;
        out[n] = v;
        filtered_energy += v * v;
    }
    p->tilt_mem = previous;
    for (k = 0; k < 10; ++k) p->postfilter_mem[k] = history[40 + k];
    target = filtered_energy == 0.0f ? 1.0f :
        (float)nj_amr_sqrt((double)input_energy / (double)filtered_energy);
    target *= 1.0f - NJ_AMR_AGC_ALPHA;
    gain = p->postfilter_agc;
    for (n = 0; n < NJ_AMR_SUBFRAME; ++n) {
        gain = NJ_AMR_AGC_ALPHA * gain + target;
        out[n] *= gain;
    }
    p->postfilter_agc = gain;
}

/* ------------------------------------------------------------------ */
/* Receive-side SID and comfort noise (TS 26.092, TS 26.093)              */
/* ------------------------------------------------------------------ */

/* Storage-format SID bits are already in parameter order (MSB first).
 * The 35 codec bits are followed by STI and the three-bit speech mode. */
static unsigned nj_amr_sid_bits(const nj_u1 *payload, int start, int count)
{
    unsigned value = 0;
    int bit;
    for (bit = start; bit < start + count; ++bit)
        value = (value << 1) | ((payload[bit / 8] >> (7 - bit % 8)) & 1u);
    return value;
}

/* x^31 + x^3 + 1, the AMR receive-side pseudo-noise sequence. All shifts
 * are unsigned; its seed and 31-bit register are local to the decoder. */
static unsigned nj_amr_cn_random(nj_amr_state *p, int count)
{
    unsigned value = 0;
    while (count--) {
        unsigned feedback = (p->cn_seed ^ (p->cn_seed >> 28)) & 1u;
        value = (value << 1) | (p->cn_seed & 1u);
        p->cn_seed = (p->cn_seed >> 1) | (feedback << 30);
    }
    return value;
}

static double nj_amr_cn_mode_adjust(int mode)
{
    /* Mode-dependent synthesis log2 RMS correction, Q11 in the spec. */
    static const int correction[8] = {-1023,-878,-732,-586,-440,-294,-148,0};
    return (double)correction[mode] / 2048.0;
}

/* Inverse cosine on [0,pi] by monotonic bisection. No host math library,
 * lookup approximation, or mutable shared table is required. */
static double nj_amr_cn_frequency(double lsp)
{
    double lo = 0.0, hi = 0.5;
    int i;
    for (i = 0; i < 26; ++i) {
        double mid = 0.5 * (lo + hi);
        if (nj_amr_cos(6.2831853071795864769 * mid) > lsp) lo = mid;
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

static void nj_amr_cn_space(float frequency[10])
{
    int i;
    float lower = NJ_AMR_MIN_LSF_SPACING;
    for (i = 0; i < 10; ++i) {
        float upper = 0.5f - (float)(10 - i) * NJ_AMR_MIN_LSF_SPACING;
        if (frequency[i] < lower) frequency[i] = lower;
        if (frequency[i] > upper) frequency[i] = upper;
        lower = frequency[i] + NJ_AMR_MIN_LSF_SPACING;
    }
}

/* Step down A_m(z) to its reflection coefficients. Prediction error
 * power is prod(1-k_m^2); white sparse excitation needs its square root
 * to cancel the all-pole synthesis filter's power gain (26.092 6.2). */
static double nj_amr_cn_residual(const float lpc[10])
{
    double a[10], previous[10], power = 1.0;
    int m, i;
    for (i = 0; i < 10; ++i) a[i] = lpc[i];
    for (m = 10; m > 0; --m) {
        double k = a[m - 1], factor = 1.0 - k * k;
        if (!(factor > 0.0 && factor <= 1.0)) return 0.0;
        power *= factor;
        for (i = 0; i < m - 1; ++i)
            previous[i] = (a[i] - k * a[m - 2 - i]) / factor;
        for (i = 0; i < m - 1; ++i) a[i] = previous[i];
    }
    return power;
}

/* Seven most recent speech frames plus a second copy of the newest form
 * the eight-frame hangover average. Retain mean-removed LSF histories for
 * the spectral variation required by 26.092 6.1. */
static void nj_amr_cn_hangover(nj_amr_state *p)
{
    float mean[10] = {0};
    double log_rms = 0.0;
    int h, k;
    for (h = 0; h < 8; ++h) {
        int slot = (p->cn_hist_pos + 7 - (h == 7 ? 0 : h)) % 8;
        log_rms += (double)p->cn_log_hist[slot] / 8.0;
        for (k = 0; k < 10; ++k) mean[k] += p->cn_lsf_hist[slot][k] * 0.125f;
    }
    nj_amr_cn_space(mean);
    for (h = 0; h < 8; ++h) {
        int slot = (p->cn_hist_pos + 7 - (h == 7 ? 0 : h)) % 8;
        for (k = 0; k < 10; ++k)
            p->cn_variation[h][k] = p->cn_lsf_hist[slot][k] - mean[k];
    }
    p->cn_log_rms = log_rms - nj_amr_cn_mode_adjust(p->cn_mode);
    for (k = 0; k < 10; ++k)
        p->cn_lsp[k] = nj_amr_cos(6.2831853071795864769 * (double)mean[k]);
}

/* SID_UPDATE alone carries usable new parameters. SID_FIRST and SID_BAD
 * enter CN using history; neither is allowed to overwrite the SID model. */
static void nj_amr_comfort_noise(nj_amr_state *p, const nj_u1 *payload,
                                  int update, float *block)
{
    float frequency[10], lpc[10], varied_lpc[10], excitation[40];
    double lsp[10], varied_lsp[10], fraction, rms, gain, log_rms;
    int entering = !p->cn_active, h, i, sf;
    if (entering) {
        if (p->cn_speech_frames >= 7) nj_amr_cn_hangover(p);
        p->cn_adjust = nj_amr_cn_mode_adjust(p->cn_mode);
        memcpy(p->cn_old_lsp, p->cn_lsp, sizeof(p->cn_lsp));
        p->cn_old_log_rms = p->cn_log_rms;
        p->cn_since_sid = 0;
        /* The adaptive codebook and ordinary LP/gain predictors must not
         * carry voiced pitch through an inactive period (26.092 6.2). */
        memset(p->excitation_buf, 0, sizeof(p->excitation_buf));
        memset(p->pitch_gain, 0, sizeof(p->pitch_gain));
        memset(p->fixed_gain, 0, sizeof(p->fixed_gain));
        memset(p->prev_lsf_r, 0, sizeof(p->prev_lsf_r));
        p->pitch_lag_int = 40;
        p->beta = 0.0f;
        p->cn_active = 1;
    }
    if (update) {
        unsigned reference = nj_amr_sid_bits(payload, 0, 3);
        unsigned one = nj_amr_sid_bits(payload, 3, 8);
        unsigned two = nj_amr_sid_bits(payload, 11, 9);
        unsigned three = nj_amr_sid_bits(payload, 20, 9);
        unsigned energy = nj_amr_sid_bits(payload, 29, 6);
        const nj_s2 *book[3];
        int period = p->cn_since_sid;
        book[0] = nj_amr_lsf_3_1[one];
        book[1] = nj_amr_lsf_3_2[two];
        book[2] = nj_amr_lsf_3_3[three];
        memcpy(p->cn_old_lsp, p->cn_lsp, sizeof(p->cn_lsp));
        p->cn_old_log_rms = p->cn_log_rms;
        p->cn_period = period < 2 ? 2 : (period > 32 ? 32 : period);
        for (i = 0; i < 10; ++i) {
            int split = i < 3 ? 0 : (i < 6 ? 1 : 2);
            frequency[i] = (float)(nj_amr_sid_reference[reference][i] +
                book[split][i - 3 * split]) / 32768.0f +
                nj_amr_lsf_3_mean[i] / 8000.0f;
        }
        nj_amr_cn_space(frequency);
        for (i = 0; i < 10; ++i)
            p->cn_lsp[i] = nj_amr_cos(6.2831853071795864769 * (double)frequency[i]);
        p->cn_log_rms = energy ? (double)energy * 0.25 - 2.5 : -16.0;
        if (entering || !p->cn_valid) {
            memcpy(p->cn_old_lsp, p->cn_lsp, sizeof(p->cn_lsp));
            p->cn_old_log_rms = p->cn_log_rms;
        }
        p->cn_since_sid = 0;
        p->cn_valid = 1;
    }
    if (p->cn_since_sid < 32767) ++p->cn_since_sid;
    if (p->cn_since_sid > 50) {
        /* Loss of SID updates: 26.093 timeout, gradual DTX muting. */
        p->cn_log_rms -= 0.125;
        if (p->cn_log_rms < -16.0) p->cn_log_rms = -16.0;
        p->cn_old_log_rms = p->cn_log_rms;
    }
    fraction = (double)p->cn_since_sid / (double)p->cn_period;
    if (fraction > 1.0) fraction = 1.0;
    for (i = 0; i < 10; ++i) {
        lsp[i] = p->cn_old_lsp[i] + fraction * (p->cn_lsp[i] - p->cn_old_lsp[i]);
        frequency[i] = (float)nj_amr_cn_frequency(lsp[i]);
    }
    nj_amr_lspd2lpc(lsp, lpc);
    log_rms = p->cn_old_log_rms + fraction * (p->cn_log_rms - p->cn_old_log_rms);
    p->cn_adjust = 0.9 * p->cn_adjust + 0.1 * nj_amr_cn_mode_adjust(p->cn_mode);
    rms = nj_amr_exp10(0.30102999566398119521 * (log_rms + p->cn_adjust));
    gain = 2.0 * rms * nj_amr_sqrt(nj_amr_cn_residual(lpc));
    h = (int)nj_amr_cn_random(p, 3);
    for (i = 0; i < 10; ++i) frequency[i] += p->cn_variation[h][i];
    nj_amr_cn_space(frequency);
    for (i = 0; i < 10; ++i)
        varied_lsp[i] = nj_amr_cos(6.2831853071795864769 * (double)frequency[i]);
    nj_amr_lspd2lpc(varied_lsp, varied_lpc);
    for (sf = 0; sf < 4; ++sf) {
        memset(excitation, 0, sizeof(excitation));
        for (i = 0; i < 10; ++i) {
            int position = i + 10 * (int)nj_amr_cn_random(p, 2);
            excitation[position] = (float)(nj_amr_cn_random(p, 1) ? gain : -gain);
        }
        nj_amr_lp_synthesis(p->samples_in + 10, varied_lpc, excitation, 40, 10);
        for (i = 0; i < 40; ++i) {
            float sample = p->samples_in[10 + i];
            if (!(sample >= -8388608.0f && sample <= 8388608.0f)) {
                /* A hostile SID must not poison synthesis or PCM conversion. */
                memset(p->samples_in, 0, sizeof(p->samples_in));
                memset(p->high_pass_mem, 0, sizeof(p->high_pass_mem));
                memset(block, 0, 160 * sizeof(float));
                return;
            }
        }
        memcpy(block + 40 * sf, p->samples_in + 10, 40 * sizeof(float));
        memmove(p->samples_in, p->samples_in + 40, 10 * sizeof(float));
    }
    nj_amr_order_2(block, block, nj_amr_highpass_zeros, nj_amr_highpass_poles,
                   nj_amr_highpass_gain * NJ_AMR_SAMPLE_SCALE, p->high_pass_mem, 160);
    /* Refresh the ordinary predictors before eventual speech resumes. */
    for (i = 0; i < 10; ++i) {
        p->prev_lsp_sub4[i] = lsp[i];
        p->lsf_q[3][i] = p->lsf_avg[i] = (float)nj_amr_cn_frequency(lsp[i]);
    }
    for (i = 0; i < 4; ++i) {
        double energy = log_rms - 8.7890625;
        if (energy < -14.09765625) energy = -14.09765625;
        if (energy > 0.0) energy = 0.0;
        p->prediction_error[i] = (float)(energy * 6.0205999132796239043);
    }
    p->cn_speech_frames = 0;
}

/* ------------------------------------------------------------------ */
/* One frame                                                           */
/* ------------------------------------------------------------------ */

/*
 * Decode one 20 ms frame into `block`, 160 floats in the reference decoder's
 * scale (the high-pass at the end of the chain applies AMR_SAMPLE_SCALE, so a
 * full scale sample is about 1.0).
 */
static void nj_amr_decode_frame(nj_amr_state *p, const nj_u1 *buf, nj_u4 size,
                                float *block)
{
    nj_amr_fixed fixed_sparse;
    float        fixed_gain_factor;
    float        spare_vector[NJ_AMR_SUBFRAME];
    float        synth_fixed_gain;
    const float *synth_fixed_vector;
    int          i, subframe;
    int          type = size ? (buf[0] >> 3) & 15 : NJ_AMR_MODE_NO_DATA;
    int          good = size && (buf[0] & 4u);
    double       speech_energy = 0.0;

    /* Classify before unpacking: Q=0 speech and SID payloads are unusable.
     * NO_DATA/SPEECH_BAD continue CN only after a SID has entered CN. */
    if (type == NJ_AMR_MODE_SID ||
        (p->cn_active && (type == NJ_AMR_MODE_NO_DATA || !good))) {
        int update = type == NJ_AMR_MODE_SID && good && size >= 6u &&
                     nj_amr_sid_bits(buf + 1, 35, 1);
        nj_amr_comfort_noise(p, update ? buf + 1 : NULL, update, block);
        return;
    }
    if (!good || type >= NJ_AMR_MODE_SID) {
        /* Conservative speech-erasure muting. This does not claim the
         * parameter-substitution PLC specified in TS 26.091. No bad payload
         * is read, and stale voiced excitation cannot cross the gap. */
        memset(block, 0, (nj_size)NJ_AMR_BLOCK * sizeof(float));
        memset(p->excitation_buf, 0, sizeof(p->excitation_buf));
        return;
    }
    p->cn_active = 0;
    p->mode = nj_amr_unpack(p, buf, size);
    if (p->mode >= NJ_AMR_MODE_SID) {
        memset(block, 0, (nj_size)NJ_AMR_BLOCK * sizeof(float));
        return;
    }
    p->cn_mode = p->mode;
    nj_amr_decode_spectrum(p);

    for (i = 0; i < 4; i++)
        nj_amr_lspd2lpc(p->lsp[i], p->lpc[i]);

    memset(&fixed_sparse, 0, sizeof(fixed_sparse));

    for (subframe = 0; subframe < 4; subframe++) {
        const nj_amr_subframe *amr_subframe = &p->frame.subframe[subframe];

        nj_amr_decode_pitch_vector(p, amr_subframe, subframe);

        nj_amr_decode_fixed_sparse(&fixed_sparse, amr_subframe->pulses,
                                   p->mode, subframe);

        /* The fixed gain depends on the fixed vector, and the fixed vector
         * depends on pitch sharpening, which depends on the pitch gain: so
         * the pitch gain is decoded first even though the standard lists the
         * codebook gain first. */
        nj_amr_decode_gains(p, amr_subframe, subframe, &fixed_gain_factor);

        nj_amr_pitch_sharpening(p, subframe, &fixed_sparse);

        /* No lag of zero: the standard forbids it, the reference refuses a
         * frame that has one, and the lag decoded above cannot be below 17. */

        nj_amr_set_fixed_vector(p->fixed_vector, &fixed_sparse, 1.0f,
                                NJ_AMR_SUBFRAME);

        p->fixed_gain[4] = nj_amr_set_fixed_gain(
            fixed_gain_factor,
            nj_amr_dot(p->fixed_vector, p->fixed_vector, NJ_AMR_SUBFRAME) /
            (float)NJ_AMR_SUBFRAME,
            p->prediction_error, nj_amr_energy_mean[p->mode],
            nj_amr_energy_pred_fac);

        /* The excitation that feeds the next subframe's pitch search is built
         * without any of the smoothing below: the reference does it this way
         * and the technical description does not say either way. */
        for (i = 0; i < NJ_AMR_SUBFRAME; i++)
            p->excitation[i] *= p->pitch_gain[4];
        nj_amr_set_fixed_vector(p->excitation, &fixed_sparse, p->fixed_gain[4],
                                NJ_AMR_SUBFRAME);

        /* No fractional bits in the excitation, as in the reference: the
         * encoder can emit long runs of a pitch factor above one, and a
         * non-zero excitation feedback turns those into a buzz. */
        for (i = 0; i < NJ_AMR_SUBFRAME; i++) {
            /* Corrupt but correctly sized payloads can feed gain > 1 back
             * indefinitely. Reject runaway excitation before squared energies
             * overflow; the negated comparisons also reject NaN. This is a
             * recovery bound, not a speech quantizer or a PLC algorithm. */
            float value = p->excitation[i];
            if (!(value >= -8388608.0f && value <= 8388608.0f)) {
                nj_amr_state_init(p);
                memset(block, 0, (nj_size)NJ_AMR_BLOCK * sizeof(float));
                return;
            }
            p->excitation[i] = nj_amr_truncf(value);
        }

        synth_fixed_gain = nj_amr_fixed_gain_smooth(p, p->lsf_q[subframe],
                                                    p->lsf_avg);

        synth_fixed_vector = nj_amr_anti_sparseness(p, &fixed_sparse,
                                                    p->fixed_vector,
                                                    synth_fixed_gain,
                                                    spare_vector);

        if (nj_amr_synthesis(p, p->lpc[subframe], synth_fixed_gain,
                             synth_fixed_vector,
                             &p->samples_in[NJ_AMR_LPC_ORDER], 0))
            nj_amr_synthesis(p, p->lpc[subframe], synth_fixed_gain,
                             synth_fixed_vector,
                             &p->samples_in[NJ_AMR_LPC_ORDER], 1);

        for (i = 0; i < NJ_AMR_SUBFRAME; ++i) {
            double sample = p->samples_in[NJ_AMR_LPC_ORDER + i];
            speech_energy += sample * sample;
        }
        nj_amr_postfilter(p, p->lpc[subframe],
                          block + subframe * NJ_AMR_SUBFRAME);

        memset(p->fixed_vector, 0, sizeof(p->fixed_vector));
        nj_amr_update_state(p);
    }

    nj_amr_order_2(block, block, nj_amr_highpass_zeros, nj_amr_highpass_poles,
                   nj_amr_highpass_gain * NJ_AMR_SAMPLE_SCALE,
                   p->high_pass_mem, NJ_AMR_BLOCK);

    /* The averaged LSF vector trails the current frame: the reference uses
     * qbar(n-1) where the technical description's equation 71 says qbar(n). */
    nj_amr_weighted_sum(p->lsf_avg, p->lsf_avg, p->lsf_q[3], 0.84f, 0.16f,
                        NJ_AMR_LPC_ORDER);
    memcpy(p->cn_lsf_hist[p->cn_hist_pos], p->lsf_q[3], sizeof(p->lsf_q[3]));
    p->cn_log_hist[p->cn_hist_pos] = speech_energy > 0.0 ?
        (float)(0.5 * nj_amr_log10(speech_energy / 160.0) /
                0.30102999566398119521) : -16.0f;
    p->cn_hist_pos = (p->cn_hist_pos + 1) % 8;
    if (p->cn_speech_frames < 32767) ++p->cn_speech_frames;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

/* 16-bit signed little-endian, rounded to nearest: the shape the port takes. */
static void nj_amr_put_s16(nj_u1 *dst, float v)
{
    float x = v * 32768.0f;
    int   s;

    if (x >= 32767.0f)
        s = 32767;
    else if (x <= -32768.0f)
        s = -32768;
    else
        s = (int)(x >= 0.0f ? x + 0.5f : x - 0.5f);

    dst[0] = (nj_u1)(s & 0xFF);
    dst[1] = (nj_u1)((s >> 8) & 0xFF);
}

/*
 * Walk the frames once and count them.
 *
 * A file has no frame count in it and every frame is a different length, so
 * this is the only way to size the output - and it is also the only way a
 * malformed file can be bounded: the walk stops at the first header that does
 * not fit, and what has been counted so far is what will be decoded.
 */
nj_u4 nj_amr_frames(const void *data, nj_u4 size)
{
    const nj_u1 *src = (const nj_u1 *)data;
    nj_u4 frames = 0u;
    nj_u4 off;

    if (src == NULL || size <= NJ_AMR_MAGIC_LEN)
        return 0u;

    /* "#!AMR\n" is AMR narrowband.  "#!AMR-WB\n" is a different codec with a
     * different sample rate and no modes here, and three of the collection's
     * assets are named ".amr" while holding an MP4 container - which is why
     * this looks at the bytes and not at the file name. */
    if (memcmp(src, "#!AMR\n", NJ_AMR_MAGIC_LEN) != 0)
        return 0u;

    off = NJ_AMR_MAGIC_LEN;
    while (off < size && frames < NJ_AMR_MAX_FRAMES) {
        int   mode = (src[off] >> 3) & 0x0F;
        nj_u4 len;

        /* 9 through 14 are reserved frame types: the stream is not a stream
         * any more, and the byte after this one is another format's idea of
         * data.  15 is legal - it says the frame is missing - and costs one
         * byte. */
        if (mode > NJ_AMR_MODE_SID && mode != NJ_AMR_MODE_NO_DATA)
            break;

        len = nj_amr_frame_len(mode);
        if (len > size - off)
            break;

        off += len;
        frames++;
    }

    return frames;
}

nj_u4 nj_amr_decode(const void *data, nj_u4 size, nj_u1 *pcm)
{
    const nj_u1 *src = (const nj_u1 *)data;
    nj_amr_state st;
    nj_u4 frames, off, n, i;

    if (pcm == NULL)
        return 0u;

    /* Same walk as nj_amr_frames(), re-run rather than cached: the caller owns
     * both the count and the buffer, so there is nothing to keep in step. */
    frames = nj_amr_frames(data, size);
    if (frames == 0u)
        return 0u;

    nj_amr_state_init(&st);

    off = NJ_AMR_MAGIC_LEN;
    for (n = 0u; n < frames; n++) {
        float block[NJ_AMR_BLOCK];
        int   mode = (src[off] >> 3) & 0x0F;
        nj_u4 len  = nj_amr_frame_len(mode);

        nj_amr_decode_frame(&st, src + off, size - off, block);

        for (i = 0u; i < NJ_AMR_BLOCK; i++)
            nj_amr_put_s16(pcm + (nj_size)n * (nj_size)NJ_AMR_BLOCK * 2u +
                           (nj_size)i * 2u, block[i]);

        off += len;
    }

    return frames;
}

/*
 * MP4/3GP: the same 20 ms frames, reached through a sample table instead of a
 * magic prefix.  Both functions answer so nj_lib_media.c can treat a container
 * exactly like a bare "#!AMR" file - ask for the count, size the byte array,
 * decode into it.
 *
 * Frames are counted by walking each sample's own bytes, not by trusting stsz
 * alone: the size table says how many bytes to hand over, and the mode byte at
 * the front of each frame says how far to advance.  The two have to agree with
 * damr's framesPerSample for the sample, or the track is refused.
 */
nj_u4 nj_amr_mp4_frames(const void *data, nj_u4 size)
{
    nj_amr_mp4 it;
    const nj_u1 *sample;
    nj_u4 bytes, total = 0u;

    if (!nj_amr_mp4_open(&it, data, size)) {
        return 0u;
    }
    for (;;) {
        int got = nj_amr_mp4_next(&it, &sample, &bytes);
        nj_u4 off = 0u, n = 0u;

        if (got < 0) {
            return 0u;
        }
        if (got == 0) {
            break;
        }
        while (off < bytes) {
            int mode = (sample[off] >> 3) & 0x0F;
            nj_u4 len;

            if (mode > NJ_AMR_MODE_SID && mode != NJ_AMR_MODE_NO_DATA) {
                return 0u;
            }
            len = nj_amr_frame_len(mode);
            if (len > bytes - off) {
                return 0u;
            }
            off += len;
            n++;
        }
        if (n != it.frames_per_sample) {
            return 0u;
        }
        total += n;
        if (total > NJ_AMR_MAX_FRAMES) {
            return 0u;
        }
    }
    return total;
}

nj_u4 nj_amr_mp4_decode(const void *data, nj_u4 size, nj_u1 *pcm)
{
    nj_amr_mp4 it;
    nj_amr_state st;
    const nj_u1 *sample;
    nj_u4 bytes, total = 0u;

    if (pcm == NULL || !nj_amr_mp4_open(&it, data, size)) {
        return 0u;
    }
    nj_amr_state_init(&st);
    for (;;) {
        int got = nj_amr_mp4_next(&it, &sample, &bytes);
        nj_u4 off = 0u, n = 0u;

        if (got < 0) {
            return 0u;
        }
        if (got == 0) {
            break;
        }
        while (off < bytes && n < it.frames_per_sample) {
            float block[NJ_AMR_BLOCK];
            int mode = (sample[off] >> 3) & 0x0F;
            nj_u4 len, i;

            if (mode > NJ_AMR_MODE_SID && mode != NJ_AMR_MODE_NO_DATA) {
                return 0u;
            }
            len = nj_amr_frame_len(mode);
            if (len > bytes - off) {
                return 0u;
            }
            /* Refuse the next frame before touching output beyond the cap. */
            if (total >= NJ_AMR_MAX_FRAMES) {
                return 0u;
            }
            nj_amr_decode_frame(&st, sample + off, bytes - off, block);
            for (i = 0u; i < NJ_AMR_BLOCK; i++) {
                nj_amr_put_s16(pcm + (nj_size)total * (nj_size)NJ_AMR_BLOCK * 2u +
                               (nj_size)i * 2u, block[i]);
            }
            off += len;
            n++;
            total++;
        }
        if (n != it.frames_per_sample || off != bytes) {
            return 0u;
        }
    }
    return total;
}

#else  /* !NJ_ENABLE_AMR */

/* ISO C forbids an empty translation unit, and a build with AMR off is
 * exactly that. */
typedef int nj_amr_disabled_;

#endif /* NJ_ENABLE_AMR */
