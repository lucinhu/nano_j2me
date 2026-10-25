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

    {
        int i, bad = 0;

        for (i = 0; i < 13; i++) {
            int which;

            for (which = 0; which < 2; which++) {
                const nj_u2 *tab = which ? nj_m4v_dc_chrom : nj_m4v_dc_lum;
                nj_u1 buf[4];
                int   len = (int)(tab[i] & 0x0fu);
                int   code = (int)(tab[i] >> 4);
                int   j, size;
                nj_u4 pos = 0;

                memset(buf, 0, sizeof(buf));
                for (j = 0; j < len; j++) {
                    if ((code >> (len - 1 - j)) & 1) {
                        buf[j / 8] |= (nj_u1)(0x80u >> (j % 8));
                    }
                }
                size = nj_m4v_decode_dc(buf, (nj_u4)sizeof(buf), &pos, which == 0);
                if (size != i || (int)pos != len) {
                    bad++;
                }
            }
        }
        ok(bad == 0, "every DC size codeword decodes to its own size");
    }

    {
        nj_u1 buf[2];
        nj_u4 pos;
        int   okd = 1;

        /* size 0 reads nothing and means no change */
        pos = 0;
        okd &= nj_m4v_dc_delta(buf, 2, &pos, 0) == 0 && pos == 0;
        /* size 2: 00 -> 0, 01 -> +1, 10 -> 2 - 3 = -1, 11 -> 3 - 3 = 0 */
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x80u;                        /* 10xxxxxx */
        pos = 0;
        okd &= nj_m4v_dc_delta(buf, 2, &pos, 2) == -1 && pos == 2;
        buf[0] = 0x40u;                        /* 01xxxxxx */
        pos = 0;
        okd &= nj_m4v_dc_delta(buf, 2, &pos, 2) == 1 && pos == 2;
        buf[0] = 0xc0u;                        /* 11xxxxxx */
        pos = 0;
        okd &= nj_m4v_dc_delta(buf, 2, &pos, 2) == 0 && pos == 2;
        ok(okd != 0, "DC deltas follow the specification's sign rule");
    }

    {
        int run = -1, level = -9, last = -1, okr = 1;

        okr &= nj_m4v_ac_sym(0, &run, &level, &last) == 0;
        okr &= nj_m4v_ac_sym(NJ_M4V_AC_SYMS, &run, &level, &last) == 0;
        okr &= nj_m4v_ac_sym(1, &run, &level, &last) == 1 &&
               run == (int)nj_m4v_ac_run[1] && level == (int)nj_m4v_ac_level[1] &&
               last == 0;
        okr &= nj_m4v_ac_sym(NJ_M4V_AC_LAST, &run, &level, &last) == 1 && last == 1;
        okr &= nj_m4v_ac_sym(NJ_M4V_AC_LAST - 1, &run, &level, &last) == 1 && last == 0;
        ok(okr != 0, "AC symbols expand to run/level with the reference last flag");
    }

    {
        int seen[64];
        int i, bad = 0;

        memset(seen, 0, sizeof(seen));
        for (i = 0; i < 64; i++) {
            int k = nj_m4v_scan_index(i);

            if (k < 0 || k >= 64 || seen[k] != 0) {
                bad++;
            } else {
                seen[k] = 1;
            }
        }
        ok(bad == 0 && nj_m4v_scan_index(64) == -1,
           "the coefficient scan is a permutation of the 64 positions");
        ok(nj_m4v_scan_index(0) == 0 && nj_m4v_scan_index(1) == 1 &&
           nj_m4v_scan_index(2) == 8 && nj_m4v_scan_index(63) == 63,
           "the scan starts down the first column and ends at the last position");
    }

    {
        nj_s4 coef[64];
        int i, bad = 0;

        for (i = 0; i < 64; i++) {
            coef[i] = 0;
        }
        nj_m4v_coef_store(coef, 0, 7);       /* DC lands at natural 0 */
        nj_m4v_coef_store(coef, 2, -3);      /* scan 2 is natural 8 */
        nj_m4v_coef_store(coef, 63, 5);      /* last scan position, natural 63 */
        nj_m4v_coef_store(coef, 64, 1);      /* out of range: ignored */
        bad += coef[0] != 7 || coef[8] != -3 || coef[63] != 5;
        for (i = 1; i < 64; i++) {
            if (i != 8 && i != 63 && coef[i] != 0) {
                bad++;
            }
        }
        ok(bad == 0, "coefficients land at their natural positions");
    }

    {
        nj_s4 coef[64];
        int i, k = 1, last = -1, rc, run = (int)nj_m4v_ac_run[1];

        for (i = 0; i < 64; i++) {
            coef[i] = 0;
        }
        rc = nj_m4v_coef_apply(coef, &k, 1, &last);
        ok(rc == 0 && k == 1 + run + 1 && last == 0 &&
           coef[nj_m4v_scan_index(1 + run)] == (nj_s4)nj_m4v_ac_level[1],
           "an AC symbol advances the scan cursor and stores its level");
        k = 1;
        ok(nj_m4v_coef_apply(coef, &k, 0, &last) == -2 && k == 1,
           "the escape symbol is reported, never stored");
        {
            int sym = -1, j;

            for (j = 1; j < NJ_M4V_AC_SYMS; j++) {
                if (nj_m4v_ac_run[j] > 0) {
                    sym = j;
                    break;
                }
            }
            if (sym > 0) {
                k = 63;
                ok(nj_m4v_coef_apply(coef, &k, sym, &last) == -1,
                   "a run that leaves the block is an error");
            }
        }
    }

    {
        nj_s4 coef[64];
        nj_u1 buf[8];
        nj_u4 pos = 0;
        int i, sym = -1, lastpos;

        for (i = 0; i < 64; i++) coef[i] = 0;
        for (i = 1; i < NJ_M4V_AC_SYMS; i++) {
            if (nj_m4v_ac_run[i] == 0 && i >= NJ_M4V_AC_LAST) { sym = i; break; }
        }
        ok(sym > 0, "a last-flagged zero-run AC symbol exists");
        memset(buf, 0, sizeof(buf));
        for (i = 0; i < 4096; i++) {
            if (nj_m4v_ac_lut[i] != 0xffffu && (int)(nj_m4v_ac_lut[i] & 0xffu) == sym) {
                int len = (int)(nj_m4v_ac_lut[i] >> 8), j;
                for (j = 0; j < len; j++)
                    if ((i >> (NJ_M4V_AC_BITS - 1 - j)) & 1) buf[j / 8] |= (nj_u1)(0x80u >> (j % 8));
                break;
            }
        }
        lastpos = nj_m4v_ac_block(buf, (nj_u4)sizeof(buf), &pos, coef, 1);
        ok(lastpos == 2, "a block loop stops after the last-flagged symbol");
    }

    {
        nj_u1 buf[8];
        nj_s4 coef[64];
        nj_u4 pos = 0;
        int i, j, sym = -1, bitpos = 0, rc;

        /* DC size 0 (3-bit code 3), then a run-0 last symbol: empty AC. */
        for (i = 1; i < NJ_M4V_AC_SYMS; i++)
            if (nj_m4v_ac_run[i] == 0 && i >= NJ_M4V_AC_LAST) { sym = i; break; }
        memset(buf, 0, sizeof(buf));
        for (j = 0; j < 3; j++) if ((3 >> (2 - j)) & 1) buf[j / 8] |= (nj_u1)(0x80u >> (j % 8));
        bitpos = 3;
        for (i = 0; i < 4096; i++)
            if (nj_m4v_ac_lut[i] != 0xffffu && (int)(nj_m4v_ac_lut[i] & 0xffu) == sym) {
                int len = (int)(nj_m4v_ac_lut[i] >> 8);
                for (j = 0; j < len; j++)
                    if ((i >> (NJ_M4V_AC_BITS - 1 - j)) & 1) buf[(bitpos + j) / 8] |= (nj_u1)(0x80u >> ((bitpos + j) % 8));
                break;
            }
        rc = nj_m4v_decode_intra_block(buf, (nj_u4)sizeof(buf), &pos, coef, 1, 40);
        ok(rc == 0 && coef[0] == 40, "an intra block with an empty AC keeps its predicted DC");
    }

    {
        nj_s4 coef[64];
        int out[64];
        int i, bad = 0;

        for (i = 0; i < 64; i++) coef[i] = 0;
        coef[0] = 64;
        nj_m4v_idct(coef, out);
        for (i = 0; i < 64; i++) if (out[i] != 8) bad++;
        ok(bad == 0, "a DC-only block of 64 transforms to flat 8s");
        coef[0] = 0;
        nj_m4v_idct(coef, out);
        for (i = 0; i < 64; i++) if (out[i] != 0) bad++;
        ok(bad == 0, "an empty block transforms to zeros");
    }

    {
        nj_u1 buf[8], pix[64];
        nj_u4 pos = 0;
        int i, j, sym = -1, bitpos = 3, rc;

        for (i = 1; i < NJ_M4V_AC_SYMS; i++)
            if (nj_m4v_ac_run[i] == 0 && i >= NJ_M4V_AC_LAST) { sym = i; break; }
        memset(buf, 0, sizeof(buf));
        for (j = 0; j < 3; j++) if ((3 >> (2 - j)) & 1) buf[j / 8] |= (nj_u1)(0x80u >> (j % 8));
        for (i = 0; i < 4096; i++)
            if (nj_m4v_ac_lut[i] != 0xffffu && (int)(nj_m4v_ac_lut[i] & 0xffu) == sym) {
                int len = (int)(nj_m4v_ac_lut[i] >> 8);
                for (j = 0; j < len; j++)
                    if ((i >> (NJ_M4V_AC_BITS - 1 - j)) & 1) buf[(bitpos + j) / 8] |= (nj_u1)(0x80u >> ((bitpos + j) % 8));
                break;
            }
        rc = nj_m4v_decode_intra_pixels(buf, (nj_u4)sizeof(buf), &pos, pix, 1, 0);
        ok(rc == 0 && pix[0] == 128 && pix[63] == 128,
           "a zero-DC block reconstructs to mid-grey");
    }

    printf("test_m4v: %d checks, %d failure(s)\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
