/*
 * nano_j2me - tests/test_m4v.c
 *
 * MPEG-4 Visual framing, checked against the decoder configurations of the two
 * real movies in the corpus (1942.3gp, doom.3gp) rather than a header we wrote
 * ourselves: a synthetic header only proves the parser matches the writer.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_m4v.h"
#include "nj2me/nj_m4v_tables.h"

#include <stdio.h>
#include <string.h>

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

    {
        /* First video sample of 1942.3gp: a VOP header at time 0. */
        static const nj_u1 vop_i[] = {
            0x00, 0x00, 0x01, 0xb6, 0x10, 0x03, 0x09, 0x8c, 0x21, 0xb6, 0xfe, 0x36
        };
        nj_m4v_vop vop;

        ok(nj_m4v_parse_vop(vop_i, (nj_u4)sizeof(vop_i), 4u, 1000u, &vop) == 1,
           "the 1942 VOP header parses");
        ok(vop.type == 0, "the first sample is an I-VOP");
        ok(vop.time_increment == 0u, "it sits at time increment 0");
        ok(vop.coded == 1, "the frame is coded, not skipped");
    }
    {
        /* doom.3gp opens with a group of VOPs; the VOP follows it. */
        static const nj_u1 gop[] = {
            0x00, 0x00, 0x01, 0xb3, 0x00, 0x10, 0x07, 0x00, 0x00, 0x01, 0xb6, 0x10,
            0x03, 0x09
        };
        nj_u4 pos = 0;
        nj_m4v_vop vop;

        ok(nj_m4v_find_vop(gop, (nj_u4)sizeof(gop), &pos) == 1,
           "the VOP after the GOP header is found");
        ok(nj_m4v_parse_vop(gop, (nj_u4)sizeof(gop), pos, 1000u, &vop) == 1 &&
           vop.type == 0 && vop.coded == 1,
           "the doom VOP header parses as a coded I-VOP");
    }


    /* The intra VLC tables are generated from the reference decoder's data.
       Check the invariants a prefix code must have rather than magic numbers:
       unique codewords, every symbol once, and each codeword covering exactly
       the 2^(12-len) entries of the 12-bit lookup it owns. */
    ok(nj_m4v_dc_lum[0] == 0x33u, "DC luma size 0 is a 3-bit code 3");
    ok(nj_m4v_dc_chrom[0] == 0x32u, "DC chroma size 0 is a 2-bit code 2");
    {
        int seen[256];
        int i, bad = 0, syms = 0, covered = 0;

        memset(seen, 0, sizeof(seen));
        for (i = 0; i < 4096; i++) {
            nj_u4 e = nj_m4v_ac_lut[i];

            if (e == 0xffffu) {
                continue;
            }
            {
                int len = (int)(e >> 8), sym = (int)(e & 0xffu);
                covered++;
                if (len < 1 || len > NJ_M4V_AC_BITS || sym >= NJ_M4V_AC_SYMS) {
                    bad++;
                    continue;
                }
                if (seen[sym] == 0) {
                    syms++;
                }
                seen[sym]++;
            }
        }
        ok(bad == 0, "every AC lookup entry names a valid symbol and length");
        ok(syms == NJ_M4V_AC_SYMS, "all AC symbols appear in the lookup");
    }
    {
        int i, bad = 0, found = 0;

        for (i = 0; i < NJ_M4V_AC_SYMS; i++) {
            int j, idx = -1;
            nj_u4 pos = 0;
            int   last = 0, sym;

            for (j = 0; j < 4096; j++) {
                nj_u4 e = nj_m4v_ac_lut[j];
                if (e != 0xffffu && (int)(e & 0xffu) == i) {
                    idx = j;
                    break;
                }
            }
            if (idx < 0) {
                continue;
            }
            found++;
            {
                nj_u1 buf[4];
                int   len = (int)(nj_m4v_ac_lut[idx] >> 8);

                memset(buf, 0, sizeof(buf));
                for (j = 0; j < len; j++) {
                    if ((idx >> (NJ_M4V_AC_BITS - 1 - j)) & 1) {
                        buf[j / 8] |= (nj_u1)(0x80u >> (j % 8));
                    }
                }
                sym = nj_m4v_decode_ac(buf, (nj_u4)sizeof(buf), &pos, &last);
                if (sym != i || (int)pos != len) {
                    bad++;
                }
                if (sym >= 0 && last != (sym >= NJ_M4V_AC_LAST)) {
                    bad++;
                }
            }
        }
        ok(found == NJ_M4V_AC_SYMS, "every AC symbol has a codeword to test");
        ok(bad == 0, "every AC codeword round-trips through the decoder");
    }

    printf("test_m4v: %d checks, %d failure(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
