/* SPDX-License-Identifier: MIT
 * Independent checks against the equations and accuracy test in ITU-T H.263
 * (01/2005), sections 6.1.2, 6.2.4, Annex A and Annex F.2.  No external decoder
 * implementation or decoder-specific expected pixel arrays are used.
 *
 * cc -O2 -std=c99 -Iinclude tests/private/test_m4v_equations.c \
 *    build-mit-m4v/libnj2me.a -lz -lm -lcurl -lSDL2 \
 *    -o build-mit-m4v/test_m4v_equations
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "nj2me/nj_m4v.h"

static double basis[8][8];
static uint32_t rng = 1;

static int pixel_random(int low, int high)
{
    /* Annex A prescribes 32-bit wraparound in its pseudorandom sequence. */
    double fraction;
    rng = rng * UINT32_C(1103515245) + UINT32_C(12345);
    fraction = (double)(rng & UINT32_C(0x7ffffffe)) / 2147483647.0;
    return (int)(fraction * (low + high + 1)) - low;
}

static int nearest(double value)
{
    return (int)floor(value + 0.5);
}

static int clip(int value, int lo, int hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void forward(const int *pixels, nj_s4 *coefficients)
{
    double row[64];
    int x, y, u, v;
    for (y = 0; y < 8; y++) for (u = 0; u < 8; u++) {
        double sum = 0.0;
        for (x = 0; x < 8; x++) sum += pixels[y * 8 + x] * basis[u][x];
        row[y * 8 + u] = sum;
    }
    for (v = 0; v < 8; v++) for (u = 0; u < 8; u++) {
        double sum = 0.0;
        for (y = 0; y < 8; y++) sum += row[y * 8 + u] * basis[v][y];
        coefficients[v * 8 + u] = clip(nearest(sum), -2048, 2047);
    }
}

static void inverse(const nj_s4 *coefficients, int *pixels)
{
    double row[64];
    int x, y, u, v;
    for (v = 0; v < 8; v++) for (x = 0; x < 8; x++) {
        double sum = 0.0;
        for (u = 0; u < 8; u++) sum += coefficients[v * 8 + u] * basis[u][x];
        row[v * 8 + x] = sum;
    }
    for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
        double sum = 0.0;
        for (v = 0; v < 8; v++) sum += row[v * 8 + x] * basis[v][y];
        pixels[y * 8 + x] = clip(nearest(sum), -256, 255);
    }
}

static int accuracy(int low, int high, int negate)
{
    double total_error = 0, total_square = 0;
    double worst_mean = 0, worst_square = 0;
    long error[64] = {0}, square[64] = {0};
    int peak = 0, block, i;
    rng = 1;
    for (block = 0; block < 10000; block++) {
        int input[64], expected[64], actual[64];
        nj_s4 coefficients[64];
        for (i = 0; i < 64; i++) {
            input[i] = pixel_random(low, high);
            if (negate) input[i] = -input[i];
        }
        forward(input, coefficients);
        inverse(coefficients, expected);
        nj_m4v_idct(coefficients, actual);
        for (i = 0; i < 64; i++) {
            int delta = clip(actual[i], -256, 255) - expected[i];
            error[i] += delta;
            square[i] += delta * delta;
            if (abs(delta) > peak) peak = abs(delta);
        }
    }
    for (i = 0; i < 64; i++) {
        double mean = (double)error[i] / 10000;
        double mse = (double)square[i] / 10000;
        if (fabs(mean) > worst_mean) worst_mean = fabs(mean);
        if (mse > worst_square) worst_square = mse;
        total_error += mean / 64;
        total_square += mse / 64;
    }
    printf("Annex A L=%d H=%d negate=%d: peak=%d pixel mean=%.7f mse=%.7f overall mean=%.7f mse=%.7f\n",
           low, high, negate, peak, worst_mean, worst_square, total_error, total_square);
    return peak <= 1 && worst_mean <= .015 && worst_square <= .06 &&
           fabs(total_error) <= .0015 && total_square <= .02;
}

static int motion(void)
{
    nj_u1 plane[11 * 13], output[64];
    int mx, my, shift, rounding, x, y, i, errors = 0;
    for (i = 0; i < (int)sizeof plane; i++) plane[i] = (nj_u1)(i * i + 19 * i);
    for (shift = 0; shift <= 1; shift++) for (rounding = 0; rounding <= 1; rounding++) {
        for (mx = -21; mx <= 21; mx++) for (my = -21; my <= 21; my++) {
            double divisor = shift ? 4.0 : 2.0;
            int ox = (int)floor(mx / divisor), oy = (int)floor(my / divisor);
            int fx = mx / divisor != ox, fy = my / divisor != oy;
            nj_m4v_mc_block8(3, 1, mx, my, plane, 11, 11, 13, shift, rounding, output);
            for (y = 0; y < 8; y++) for (x = 0; x < 8; x++) {
                int bx = 3 + ox + x, by = 1 + oy + y;
                int a = plane[clip(by, 0, 12) * 11 + clip(bx, 0, 10)];
                int b = plane[clip(by, 0, 12) * 11 + clip(bx + 1, 0, 10)];
                int c = plane[clip(by + 1, 0, 12) * 11 + clip(bx, 0, 10)];
                int d = plane[clip(by + 1, 0, 12) * 11 + clip(bx + 1, 0, 10)];
                int expected = fx ? fy ? (a + b + c + d + 2 - rounding) / 4
                                      : (a + b + 1 - rounding) / 2
                                  : fy ? (a + c + 1 - rounding) / 2 : a;
                errors += expected != output[y * 8 + x];
            }
        }
    }
    printf("Motion: %d interpolation mismatches over 473344 samples\n", errors);
    return errors == 0;
}

static int chroma(void)
{
    /* Table F.1's sixteen fractional positions, in output half-pel units. */
    static const int fractions[16] = {0,0,0,1,1,1,1,1,1,1,1,1,1,1,2,2};
    int value, errors = 0;
    for (value = -4096; value <= 4096; value++) {
        int whole = (int)floor(value / 16.0);
        int index = value - 16 * whole;
        errors += nj_m4v_chroma_round(value) != 2 * whole + fractions[index];
    }
    printf("Chroma: %d mismatches over 8193 vectors\n", errors);
    return errors == 0;
}

int main(void)
{
    static const int ranges[3][2] = {{256,255}, {5,5}, {300,300}};
    int u, x, r, negate, success = 1;
    nj_s4 zero[64] = {0};
    int output[64];
    for (u = 0; u < 8; u++) for (x = 0; x < 8; x++)
        basis[u][x] = (u ? 0.5 : 1.0 / sqrt(8.0)) *
                      cos(acos(-1.0) * (2*x+1) * u / 16.0);
    nj_m4v_idct(zero, output);
    for (x = 0; x < 64; x++) success &= output[x] == 0;
    for (r = 0; r < 3; r++) for (negate = 0; negate <= 1; negate++)
        success &= accuracy(ranges[r][0], ranges[r][1], negate);
    success &= motion();
    success &= chroma();
    puts(success ? "PASS: H.263 equation and accuracy checks" : "FAIL: equation/accuracy checks");
    return success ? 0 : 1;
}
