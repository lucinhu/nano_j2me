/* SPDX-License-Identifier: MIT
 * Independent AMR equation/format tests, TS 26.090 v17.0.0.
 * Build separately (implementation inclusion exposes private numerical stages):
 * cc -std=c99 -Iinclude -Isrc/midp tests/private/test_amr_equations.c \
 *    src/midp/nj_amr_mp4.c -lm -o /tmp/test_amr_equations
 * Add -fsanitize=address,undefined -fno-omit-frame-pointer for bounded fuzzing.
 * This tests equations and local API policies, not reference-decoder conformance.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/midp/nj_amr.c"

static uint32_t random_state = 26090;
static uint32_t next_random(void)
{
    random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return random_state;
}

static void elementary_math(void)
{
    int i;
    for (i = -120; i <= 120; ++i) {
        double x = i / 13.0, positive = pow(10.0, x);
        assert(fabs(nj_amr_sqrt(positive) / sqrt(positive) - 1.0) < 2e-14);
        assert(fabs(nj_amr_exp10(x) / positive - 1.0) < 2e-14);
        assert(fabs(nj_amr_log10(positive) - log10(positive)) < 2e-14);
        assert(fabs(nj_amr_cos(x) - cos(x)) < 3e-14);
    }
}

/* Expand full degree-ten root products, unlike the runtime's symmetric
 * half-polynomial recurrence. Equations (18)-(19) then give eleven terms. */
static void spectral_polynomials(void)
{
    int trial, parity, root, degree;
    for (trial = 0; trial < 300; ++trial) {
        double lsp[10], polynomial[2][11] = {{1}, {1}};
        float lpc[10];
        for (root = 0; root < 10; ++root)
            lsp[root] = cos((root + 1) * 3.14159265358979323846 / 11 +
                            (int)(next_random() % 100) * 0.00001);
        for (parity = 0; parity < 2; ++parity) {
            for (root = 0; root < 5; ++root) {
                double product[11] = {0};
                for (degree = 0; degree <= 2 * root; ++degree) {
                    double coefficient = polynomial[parity][degree];
                    product[degree] += coefficient;
                    product[degree + 1] -= 2 * lsp[2 * root + parity] * coefficient;
                    product[degree + 2] += coefficient;
                }
                memcpy(polynomial[parity], product, sizeof(product));
            }
        }
        nj_amr_lspd2lpc(lsp, lpc);
        for (degree = 1; degree <= 10; ++degree) {
            double expected = (polynomial[0][degree] + polynomial[0][degree - 1] +
                               polynomial[1][degree] - polynomial[1][degree - 1]) / 2;
            assert(fabs(lpc[degree - 1] - expected) < 1e-5);
        }
    }
}

static void pitch_format(void)
{
    int lag, fraction, mode, sf, previous, index, coordinate;
    /* Enumerate absolute search coordinates in physical order. */
    index = 0;
    for (lag = 17; lag <= 95; ++lag) for (fraction = -2; fraction <= 3; ++fraction) {
        int actual_lag, actual_fraction;
        coordinate = 6 * lag + fraction;
        if (coordinate < 105 || coordinate > 567) continue;
        nj_amr_pitch_coordinates(7, 0, index++, 0, &actual_lag, &actual_fraction);
        assert(actual_lag == lag && actual_fraction == fraction);
    }
    assert(index == 463);
    for (lag = 95; lag <= 143; ++lag) {
        int actual_lag, actual_fraction;
        nj_amr_pitch_coordinates(7, 2, index++, 0, &actual_lag, &actual_fraction);
        assert(actual_lag == lag && actual_fraction == 0);
    }
    assert(index == 512);
    index = 0;
    for (lag = 19; lag <= 85; ++lag) for (fraction = -1; fraction <= 1; ++fraction) {
        int actual_lag, actual_fraction;
        coordinate = 3 * lag + fraction;
        if (coordinate < 58 || coordinate > 254) continue;
        nj_amr_pitch_coordinates(4, 0, index++, 0, &actual_lag, &actual_fraction);
        assert(actual_lag == lag && actual_fraction == fraction);
    }
    assert(index == 197);
    for (lag = 85; lag <= 143; ++lag) {
        int actual_lag, actual_fraction;
        nj_amr_pitch_coordinates(4, 0, index++, 0, &actual_lag, &actual_fraction);
        assert(actual_lag == lag && actual_fraction == 0);
    }
    assert(index == 256);
    for (mode = 0; mode < 8; ++mode) for (sf = 0; sf < 4; ++sf)
        for (previous = 17; previous <= 143; ++previous) {
            int absolute = sf == 0 || (sf == 2 && mode >= 2);
            int bits = absolute ? (mode == 7 ? 9 : 8) :
                       mode == 7 || mode == 5 ? 6 : mode == 4 || mode == 6 ? 5 : 4;
            for (index = 0; index < (1 << bits); ++index) {
                nj_amr_pitch_coordinates(mode, sf, index, previous, &lag, &fraction);
                assert(lag >= 17 && lag <= 144);
                assert(abs(fraction) <= (mode == 7 ? 3 : 1));
            }
        }
}

static void pulse_formats(void)
{
    int mode, sf, code, pulse;
    for (mode = 0; mode < 8; ++mode) for (sf = 0; sf < 4; ++sf) {
        int limit = mode < 2 ? 128 : mode == 2 ? 512 : mode == 3 ? 2048 : 8192;
        if (mode >= 6) limit = 4096;
        for (code = 0; code < limit; ++code) {
            nj_u2 word[10] = {0};
            nj_amr_fixed sparse;
            if (mode < 6) { word[0] = (nj_u2)code; word[1] = (nj_u2)(code % 16); }
            else if (mode == 6) {
                for (pulse = 0; pulse < 4; ++pulse) word[pulse] = (code >> pulse) & 1;
                word[4] = code % 1024; word[5] = (code / 4) % 1024; word[6] = code % 128;
            } else {
                for (pulse = 0; pulse < 10; ++pulse)
                    word[pulse] = (nj_u2)((code >> (pulse % 4)) & (pulse % 2 ? 15 : 7));
            }
            memset(&sparse, 0, sizeof(sparse));
            nj_amr_decode_fixed_sparse(&sparse, word, mode, sf);
            assert(sparse.n >= 1 && sparse.n <= 10);
            for (pulse = 0; pulse < sparse.n; ++pulse) {
                assert(sparse.x[pulse] < 40);
                assert(fabsf(sparse.y[pulse]) == 1.0f);
            }
            if (mode == 6) for (pulse = 0; pulse < 8; ++pulse)
                assert(sparse.x[pulse] % 4 == pulse % 4);
            if (mode == 7) for (pulse = 0; pulse < 10; ++pulse)
                assert(sparse.x[pulse] % 5 == pulse / 2);
        }
    }
}

/* Direct long-double circular convolution of a dense sharpened pulse train
 * checks the sparse Horner evaluation against equation (step 5 of 6.1). */
static void phase_convolution(void)
{
    int trial, pulse, position, n, k;
    for (trial = 0; trial < 2000; ++trial) {
        nj_amr_fixed sparse;
        float output[40];
        long double train[40] = {0};
        const float *filter = trial & 1 ? nj_amr_ir_filter_medium : nj_amr_ir_filter_strong;
        sparse.n = 1 + next_random() % 10;
        sparse.pitch_lag = 17 + next_random() % 127;
        sparse.pitch_fac = (next_random() % 100) / 100.0f;
        for (pulse = 0; pulse < sparse.n; ++pulse) {
            long double amplitude;
            sparse.x[pulse] = next_random() % 40;
            sparse.y[pulse] = next_random() & 1 ? 1.0f : -1.0f;
            amplitude = sparse.y[pulse];
            for (position = sparse.x[pulse]; position < 40; position += sparse.pitch_lag) {
                train[position] += amplitude;
                amplitude *= sparse.pitch_fac;
            }
        }
        nj_amr_apply_ir_filter(output, &sparse, filter);
        for (n = 0; n < 40; ++n) {
            long double expected = 0;
            for (k = 0; k < 40; ++k) expected += train[k] * filter[(n - k + 40) % 40];
            assert(fabsl(output[n] - expected) < 2e-6L);
        }
    }
}

static void gain_and_filters(void)
{
    nj_amr_state state;
    float prediction[4] = {-14, -12, -10, -8};
    float predictor[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float gain, lpc[10] = {0}, vector[40], samples[50] = {0}, out[40];
    double expected = 1.5 * pow(10.0, 0.05 * (33 - 10)) / 2;
    int n;
    gain = nj_amr_set_fixed_gain(1.5f, 4.0f, prediction, 33, predictor);
    assert(fabs(gain / expected - 1) < 2e-6);
    assert(fabs(prediction[3] - 20 * log10(1.5)) < 1e-6);
    nj_amr_state_init(&state);
    state.mode = 0;
    for (n = 0; n < 40; ++n) { vector[n] = n % 3 - 1; state.pitch_vector[n] = 0; }
    assert(!nj_amr_synthesis(&state, lpc, 2.0f, vector, samples + 10, 0));
    for (n = 0; n < 40; ++n) assert(samples[n + 10] == 2 * vector[n]);
    memcpy(state.samples_in, samples, sizeof(samples));
    nj_amr_postfilter(&state, lpc, out);
    for (n = 0; n < 40; ++n) {
        double smoother = 1 - pow(0.9, n + 1);
        assert(fabs(out[n] - samples[n + 10] * smoother) < 2e-6);
    }
    state.pitch_gain[4] = 0.8f;
    for (n = 0; n < 40; ++n) state.pitch_vector[n] = n % 5 - 2;
    {
        float before = 0, after;
        for (n = 0; n < 40; ++n) {
            float v = 0.8f * state.pitch_vector[n] + 2 * vector[n];
            before += v * v;
        }
        assert(!nj_amr_synthesis(&state, lpc, 2.0f, vector, samples + 10, 0));
        after = nj_amr_dot(samples + 10, samples + 10, 40);
        assert(fabs(after / before - 1) < 1e-6);
    }
}

static void bounded_inputs(void)
{
    unsigned char input[1030], pcm[1024 * 320 + 32];
    int trial, mode, cut, n;
    assert(nj_amr_frames(NULL, 50) == 0);
    assert(nj_amr_decode(NULL, 50, pcm) == 0);
    assert(nj_amr_decode(input, 0, NULL) == 0);
    for (mode = 0; mode < 9; ++mode) {
        int length = 1 + nj_amr_payload_bytes[mode];
        memset(input, 0, sizeof(input)); memcpy(input, "#!AMR\n", 6);
        input[6] = (unsigned char)(mode * 8 + 4);
        for (cut = 0; cut < length; ++cut) assert(nj_amr_frames(input, 6 + cut) == 0);
        assert(nj_amr_frames(input, 6 + length) == 1);
    }
    for (mode = 9; mode < 15; ++mode) {
        input[6] = mode * 8;
        assert(nj_amr_decode(input, 7, pcm) == 0);
    }
    /* Exercise arbitrary speech payloads, not just headers rejected before
     * synthesis. High PRNG bits avoid the short cycle of low LCG bytes. */
    for (mode = 0; mode < 8; ++mode) for (trial = 0; trial < 128; ++trial) {
        unsigned off = 6, frame, byte;
        memcpy(input, "#!AMR\n", 6);
        for (frame = 0; frame < 16; ++frame) {
            input[off++] = (unsigned char)(mode * 8 + 4);
            for (byte = 0; byte < nj_amr_payload_bytes[mode]; ++byte)
                input[off++] = (unsigned char)(next_random() >> 24);
        }
        memset(pcm, 0xA5, 16 * 320 + 32);
        assert(nj_amr_decode(input, off, pcm + 16) == 16);
        for (n = 0; n < 16; ++n) {
            assert(pcm[n] == 0xA5);
            assert(pcm[16 + 16 * 320 + n] == 0xA5);
        }
    }
    for (trial = 0; trial < 5000; ++trial) {
        nj_u4 count, decoded;
        unsigned length = next_random() % sizeof(input);
        for (n = 0; n < (int)length; ++n) input[n] = (unsigned char)next_random();
        if (trial & 1) memcpy(input, "#!AMR\n", 6);
        count = nj_amr_frames(input, length);
        assert(count <= 1024);
        memset(pcm, 0xA5, sizeof(pcm));
        decoded = nj_amr_decode(input, length, pcm + 16);
        assert(decoded == count);
        for (n = 0; n < 16; ++n) {
            assert(pcm[n] == 0xA5);
            assert(pcm[16 + count * 320 + n] == 0xA5);
        }
    }
    {
        unsigned length, frame;
        int nonzero = 0;
        double energy = 0.0;
        /* Maximal SID payload is legal framing. It must synthesize CN,
         * preserve both guards, and continue through the NO_DATA frame. */
        memcpy(input, "#!AMR\n", 6); input[6] = 8 * 8 + 4;
        memset(input + 7, 0xFF, 5); input[12] = 15 * 8;
        memset(pcm, 0xA5, 640 + 32);
        assert(nj_amr_frames(input, 13) == 2);
        assert(nj_amr_decode(input, 13, pcm + 16) == 2);
        for (n = 0; n < 640; ++n) nonzero |= pcm[16 + n];
        assert(nonzero);
        for (n = 0; n < 16; ++n) {
            assert(pcm[n] == 0xA5);
            assert(pcm[16 + 640 + n] == 0xA5);
        }

        /* Independent format vector: reference and all three LSF indices
         * zero, energy index40, STI1, mode7. TS 26.092 table1 maps bits
         * 29..34 to energy, then STI/mode follow in TS 26.093. External
         * OpenCORE RMS is343.140019 for frames10..31; allow floating CNG
         * rounding while testing decoded level and LP gain, not just noise. */
        memset(input + 7, 0, 5);
        input[10] = 0x05; input[11] = 0x1E;
        length = 12;
        for (frame = 1; frame < 32; ++frame) input[length++] = 15 * 8;
        memset(pcm, 0xA5, 32 * 320 + 32);
        assert(nj_amr_frames(input, length) == 32);
        assert(nj_amr_decode(input, length, pcm + 16) == 32);
        for (n = 10 * 160; n < 32 * 160; ++n) {
            int sample = pcm[16 + 2 * n] | ((int)pcm[17 + 2 * n] << 8);
            if (sample >= 32768) sample -= 65536;
            energy += (double)sample * sample;
        }
        assert(energy > 330.0 * 330.0 * 3520.0);
        assert(energy < 356.0 * 356.0 * 3520.0);
        for (n = 0; n < 16; ++n) {
            assert(pcm[n] == 0xA5);
            assert(pcm[16 + 32 * 320 + n] == 0xA5);
        }

        /* Energy index0 is the explicit minimum/silence SID model. This
         * valid silence oracle replaces the old all-FF-must-be-zero claim. */
        input[10] = 0;
        memset(pcm, 0xA5, 32 * 320 + 32);
        assert(nj_amr_decode(input, length, pcm + 16) == 32);
        for (n = 0; n < 32 * 320; ++n) assert(pcm[16 + n] == 0);
        for (n = 0; n < 16; ++n) {
            assert(pcm[n] == 0xA5);
            assert(pcm[16 + 32 * 320 + n] == 0xA5);
        }
    }
    {
        unsigned char *large = malloc(15011), *output = malloc(15000 * 320 + 16);
        assert(large && output);
        memcpy(large, "#!AMR\n", 6); memset(large + 6, 15 * 8, 15005);
        memset(output, 0xA5, 15000 * 320 + 16);
        assert(nj_amr_frames(large, 15011) == 15000);
        assert(nj_amr_decode(large, 15011, output) == 15000);
        for (n = 0; n < 16; ++n) assert(output[15000 * 320 + n] == 0xA5);
        free(large); free(output);
    }
}

static void store_be32(unsigned char *dst, unsigned value)
{
    dst[0] = (unsigned char)(value >> 24); dst[1] = (unsigned char)(value >> 16);
    dst[2] = (unsigned char)(value >> 8); dst[3] = (unsigned char)value;
}

/* One chunk of constant-size, one-frame samples. The minimal nested ISO
 * boxes are sufficient for the local iterator; the frame data is no-data.
 * Explicit byte offsets keep this boundary test independent of muxer code. */
static void container_frame_cap(void)
{
    static const struct { unsigned offset, size; const char *type; } box[] = {
        {0,8,"ftyp"}, {8,177,"moov"}, {16,169,"trak"}, {24,161,"mdia"},
        {32,153,"minf"}, {40,145,"stbl"}, {48,69,"stsd"}, {64,53,"samr"},
        {100,17,"damr"}, {117,20,"stsz"}, {137,28,"stsc"}, {165,20,"stco"}
    };
    unsigned char *file = calloc(193 + 15001, 1);
    unsigned char *output = malloc(15001 * 320 + 16);
    unsigned i, count;
    assert(file && output);
    for (i = 0; i < sizeof(box) / sizeof(box[0]); ++i) {
        store_be32(file + box[i].offset, box[i].size);
        memcpy(file + box[i].offset + 4, box[i].type, 4);
    }
    store_be32(file + 60, 1); /* stsd entry count */
    file[79] = 1; file[89] = 1; /* data reference, channel count */
    store_be32(file + 96, 8000u << 16); file[116] = 1; /* rate, frames/sample */
    store_be32(file + 129, 1); /* constant sample size */
    store_be32(file + 149, 1); store_be32(file + 153, 1); /* stsc entry/first */
    store_be32(file + 161, 1); /* description index */
    store_be32(file + 177, 1); store_be32(file + 181, 193); /* one chunk */
    memcpy(file + 189, "mdat", 4); memset(file + 193, 15 * 8, 15001);
    for (count = 15000; count <= 15001; ++count) {
        store_be32(file + 133, count); store_be32(file + 157, count);
        store_be32(file + 185, count + 8);
        memset(output, 0xA5, 15001 * 320 + 16);
        assert(nj_amr_mp4_frames(file, 193 + count) == (count == 15000 ? count : 0));
        assert(nj_amr_mp4_decode(file, 193 + count, output) == (count == 15000 ? count : 0));
        for (i = 0; i < 16; ++i) assert(output[15000 * 320 + i] == 0xA5);
    }
    free(file); free(output);
}

int main(void)
{
    elementary_math(); spectral_polynomials(); pitch_format(); pulse_formats();
    phase_convolution(); gain_and_filters(); bounded_inputs(); container_frame_cap();
    puts("PASS: AMR equations, pulse formats, gain/filter identities, bounded inputs");
    return 0;
}
