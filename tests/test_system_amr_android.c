/* SPDX-License-Identifier: MIT
 * Device/emulator test of the real system provider. Synthesized storage
 * packets contain no game assets. Build with the NDK, rename main if needed,
 * and invoke from a JNI worker in a normal Android app process. An API21
 * standalone shell process can lack native-service callback initialization.
 * Unlike host protocol mocks, every open here must reach the platform codec.
 */
#include "nj2me/nj_system_amr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static unsigned outstanding;
static void *take(void *ud, nj_u4 n)
{ void *p; (void)ud; p = malloc(n); if (p) ++outstanding; return p; }
static void drop(void *ud, void *p)
{ (void)ud; if (p) { assert(outstanding); --outstanding; free(p); } }

static void run(const nj_av_audio_ops *ops, unsigned mode, unsigned count)
{
    static const unsigned sizes[9] = {13,14,16,18,20,21,27,32,6};
    nj_u1 data[6 + 32 * 12], pcm[3840], guard[4];
    nj_av_mem mem = { NULL, take, drop };
    nj_av_info info;
    nj_av_step step;
    nj_av_dec *dec;
    const char *error = NULL;
    unsigned i, j, len = 6, used = 0, nonzero = 0;
    uint32_t hash = 2166136261u;
    memcpy(data, "#!AMR\n", 6);
    for (i = 0; i < count; ++i) {
        data[len++] = (nj_u1)((mode << 3) | 4);
        for (j = 0; j + 1 < sizes[mode]; ++j)
            data[len++] = (nj_u1)((j * 37 + 11 + mode * 5) & 255);
    }
    printf("opening real Android system codec mode=%u frames=%u\n", mode, count);
    dec = ops->open(data, len, &mem, &info, &error);
    if (!dec) { fprintf(stderr, "mode %u open failed: %s\n", mode,
                         error ? error : "unknown"); exit(1); }
    assert(info.rate == 8000 && info.channels == 1 && info.frames == count * 160);
    memset(&step, 0, sizeof(step));
    step.out = guard; step.out_cap = 1;
    memset(guard, 0xA5, sizeof(guard));
    assert(ops->decode(dec, &step) == NJ_AV_ERR_AGAIN && step.out_used == 0);
    assert(guard[0] == 0xA5 && guard[1] == 0xA5);
    while (used < count * 320) {
        memset(&step, 0, sizeof(step));
        step.out = pcm + used; step.out_cap = count * 320 - used;
        assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used);
        used += step.out_used;
    }
    for (i = 0; i < used; ++i) {
        nonzero += pcm[i] != 0;
        hash = (hash ^ pcm[i]) * 16777619u;
    }
    assert(ops->decode(dec, &step) == NJ_AV_ERR_END);
    assert(ops->seek(dec, INT64_MAX) == NJ_AV_OK);
    assert(ops->decode(dec, &step) == NJ_AV_ERR_END);
    assert(ops->seek(dec, 0) == NJ_AV_OK);
    memset(&step, 0, sizeof(step)); step.out = guard; step.out_cap = sizeof(guard);
    assert(ops->decode(dec, &step) == NJ_AV_OK && step.out_used == 4);
    assert(!memcmp(guard, pcm, 4));
    ops->close(dec);
    assert(!outstanding);
    printf("PASS provider=%s mode=%u frames=%u bytes=%u nonzero=%u fnv=%08x\n",
           ops->name, mode, count, used, nonzero, hash);
}
int main(int argc, char **argv)
{
    const nj_av_audio_ops *ops = nj_avcodec_system_amr();
    unsigned mode;
    assert(ops);
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(30);
    if (argc == 2 && !strcmp(argv[1], "--expect-no-app")) {
        nj_u1 data[19] = "#!AMR\n";
        nj_av_mem mem = { NULL, take, drop };
        nj_av_info info;
        const char *error = NULL;
        data[6] = 4;
        assert(!ops->open(data, sizeof(data), &mem, &info, &error));
        assert(error && strstr(error, "app runtime") && !outstanding);
        puts("PASS Android CLI: no VM, early NULL permits built-in fallback");
        alarm(0);
        return 0;
    }
    for (mode = 0; mode < 9; ++mode) run(ops, mode, 12);
    puts("real Android system AMR: 9 modes passed");
    alarm(0);
    return 0;
}
