/*
 * nano_j2me - tests/test_m4v.c
 *
 * MPEG-4 Visual framing, checked against the decoder configurations of the two
 * real movies in the corpus (1942.3gp, doom.3gp) rather than a header we wrote
 * ourselves: a synthetic header only proves the parser matches the writer.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_m4v.h"

#include <stdio.h>

static int checks;
static int fails;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

/* esds DecoderSpecificInfo of vault/1942: 176x144, 25 fps, time_res 1000. */
static const nj_u1 cfg_1942[] = {
    0x00, 0x00, 0x01, 0xb0, 0xf0, 0x00, 0x00, 0x01, 0xb5, 0x0e, 0xe0, 0x40,
    0xc0, 0xcf, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00, 0x84,
    0x40, 0xfa, 0x28, 0x2c, 0x20, 0x90, 0xa3, 0x1f
};

/* esds DecoderSpecificInfo of vid/doom.3gp: 320x240, 15 fps. */
static const nj_u1 cfg_doom[] = {
    0x00, 0x00, 0x01, 0xb0, 0x00, 0x00, 0x00, 0x01, 0xb5, 0x89, 0x13, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x20, 0x00, 0xc4, 0x8d, 0x88, 0x00,
    0x7d, 0x0a, 0x04, 0x1e, 0x14, 0x63, 0x00, 0x00, 0x01, 0xb2, 0x61, 0x76,
    0x63, 0x32, 0x2e, 0x30, 0x2e, 0x31, 0x31, 0x2e, 0x31, 0x31, 0x31, 0x30
};

static int parse_cfg(const nj_u1 *b, nj_u4 n, nj_m4v_vol *vol)
{
    nj_u4 code = 0, pos = 0, from = 0;

    while (nj_m4v_start_code(b, n, from, &code, &pos)) {
        if (code == NJ_M4V_VOL) {
            return nj_m4v_parse_vol(b, n, pos, vol);
        }
        from = pos;
    }
    return 0;
}

int main(void)
{
    nj_m4v_vol vol;
    nj_u4 code = 0, pos = 0;

    ok(nj_m4v_start_code(cfg_1942, (nj_u4)sizeof(cfg_1942), 0u, &code, &pos) == 1 &&
       code == NJ_M4V_VOS, "the first start code of the 1942 config is VOS");

    ok(parse_cfg(cfg_1942, (nj_u4)sizeof(cfg_1942), &vol) == 1,
       "the 1942 decoder config parses");
    ok(vol.width == 176u && vol.height == 144u, "1942 is 176x144");
    ok(vol.time_res == 1000u, "1942 counts 1000 increments per second");
    ok(vol.shape == 0, "1942 is a rectangular layer");

    ok(parse_cfg(cfg_doom, (nj_u4)sizeof(cfg_doom), &vol) == 1,
       "the doom decoder config parses");
    ok(vol.width == 320u && vol.height == 240u, "doom is 320x240");

    ok(nj_m4v_accepts(cfg_1942, (nj_u4)sizeof(cfg_1942)) == 1,
       "the 1942 config is recognised as M4V");
    ok(nj_m4v_accepts(cfg_doom, (nj_u4)sizeof(cfg_doom)) == 1,
       "the doom config is recognised as M4V");

    /* A truncated config must be refused, not read past the buffer. */
    ok(nj_m4v_parse_vol(cfg_1942, 24u, 22u, &vol) == 0,
       "a truncated VOL is refused");

    {
        static const nj_u1 junk[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        ok(nj_m4v_accepts(junk, (nj_u4)sizeof(junk)) == 0, "noise is not M4V");
    }

    printf("test_m4v: %d checks, %d failure(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
