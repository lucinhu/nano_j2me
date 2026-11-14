/* Format-boundary regressions for the MIT event/packet/header replacement.
 * The existing corpus tests remain the independent end-to-end bitstream oracle. */
static void m4v_syntax_cases(void)
{
    int bad = 0, field, group, index, bitpos, marker_at, method;
    {
        int symbol, form, sign, scan;
        for (symbol = 0; symbol < 102; symbol++)
            for (form = 0; form < 3; form++)
                for (sign = 0; sign < 2; sign++)
                    for (scan = 0; scan < 3; scan++) {
                        nj_u1 data[16] = {0};
                        nj_s4 coefficients[64] = {0};
                        nj_u4 position = 0, code;
                        int length = 0, last = symbol >= 67;
                        int run = nj_m4v_ac_run[symbol], level = nj_m4v_ac_level[symbol];
                        for (code = 0; code < 4096; code++)
                            if ((nj_m4v_ac_lut[code] & 255u) == (nj_u4)symbol) {
                                length = nj_m4v_ac_lut[code] >> 8;
                                break;
                            }
                        if (!length) { bad++; continue; }
                        bitpos = 0;
                        if (form) {
                            put_bits(data, &bitpos, 3, 7);
                            put_bits(data, &bitpos, form == 1 ? 0 : 2, form);
                        }
                        put_bits(data, &bitpos, code >> (12-length), length);
                        put_bits(data, &bitpos, (nj_u4)sign, 1);
                        if (form == 1)
                            level += last ? nj_m4v_max_level_1[run] : nj_m4v_max_level_0[run];
                        if (form == 2)
                            run += 1 + (last ? nj_m4v_max_run_1[level] : nj_m4v_max_run_0[level]);
                        if (!last) { /* A final explicit event closes the block. */
                            put_bits(data, &bitpos, 3, 7); put_bits(data, &bitpos, 3, 2);
                            put_bits(data, &bitpos, 1, 1); put_bits(data, &bitpos, 0, 6);
                            put_bits(data, &bitpos, 1, 1); put_bits(data, &bitpos, 1, 12);
                            put_bits(data, &bitpos, 1, 1);
                        }
                        bad += nj_m4v_ac_block_scan(data, (nj_u4)((bitpos+7)/8), &position,
                                                    coefficients, 1, 31, scan) != 0;
                        bad += position != (nj_u4)bitpos ||
                            coefficients[nj_m4v_scan_index_mode(1+run, scan)] != (sign ? -level : level);
                    }
    }
    ok(bad == 0, "1836 signed intra VLC/extension events retain their run, level, last and scan semantics");
    bad = 0;
    for (field = -1; field < 22; field++) {
        nj_u1 data[64] = {0};
        nj_m4v_vol vol;
        int expected_i = 0, expected_p = 0, expected_b = 0;
        method = field >= 20 ? 1 : 0;
        bitpos = 0;
        put_bits(data, &bitpos, 0, 1); /* random access */
        put_bits(data, &bitpos, 1, 8); /* object type */
        put_bits(data, &bitpos, 0, 1); /* default version */
        put_bits(data, &bitpos, 1, 4); /* square pixels */
        put_bits(data, &bitpos, 0, 1); /* no VOL control */
        put_bits(data, &bitpos, 0, 2); /* rectangular */
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 1000, 16);
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 0, 1); /* variable frame rate */
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 32, 13);
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 32, 13);
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 0, 1); /* progressive */
        put_bits(data, &bitpos, 1, 1); /* OBMC disabled */
        put_bits(data, &bitpos, 0, 4); /* sprite/not8bit/quant/complexity-disable */
        put_bits(data, &bitpos, (nj_u4)method, 2);
        index = 0; marker_at = 0;
        for (group = 0; group < 4; group++) {
            put_bits(data, &bitpos, 0, 1); /* this group is present */
            while (index < (group == 0 ? 6 : group == 1 ? 10 : group == 2 ? 14 : 20)) {
                put_bits(data, &bitpos, index == field, 1);
                index++;
            }
            if (group == 1 || group == 3) {
                marker_at = bitpos;
                put_bits(data, &bitpos, 1, 1);
            }
        }
        if (method == 1) {
            put_bits(data, &bitpos, field == 20, 1);
            put_bits(data, &bitpos, field == 21, 1);
        }
        put_bits(data, &bitpos, 1, 1); /* resync disabled */
        put_bits(data, &bitpos, 0, 4); /* no partition, trailing flags */
        if ((field >= 0 && field <= 6) || field == 9 ||
            (field >= 10 && field <= 13) || field == 20)
            expected_i = field == 13 ? 4 : 8;
        if (field == 7 || field == 8 || field == 14 || field == 15 ||
            (field >= 17 && field <= 19) || field == 21) expected_p = 8;
        if (field == 16) expected_b = 8;
        bad += !nj_m4v_parse_vol(data, (nj_u4)((bitpos+7)/8), 0, &vol);
        bad += vol.cplx_i != expected_i || vol.cplx_p != expected_p ||
               vol.cplx_b != expected_b || vol.width != 32 || vol.height != 32;
        data[marker_at/8] ^= (nj_u1)(0x80u >> (marker_at%8));
        bad += nj_m4v_parse_vol(data, (nj_u4)((bitpos+7)/8), 0, &vol) != 0;
    }
    ok(bad == 0, "each optional complexity field retains its picture class and width");

    bad = 0;
    for (method = 0; method < 2; method++) {
        nj_u1 data[32] = {0};
        nj_m4v_vol vol;
        nj_m4v_vop vop;
        nj_u4 position = 0;
        memset(&vol, 0, sizeof(vol));
        vol.time_res = 1000; vol.quant_precision = 5;
        vol.cplx_i = 12; vol.cplx_p = 8;
        bitpos = 0;
        put_bits(data, &bitpos, (nj_u4)method, 2);
        put_bits(data, &bitpos, 0, 1); put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 21, 10); put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 1, 1); /* coded */
        if (method) put_bits(data, &bitpos, 1, 1); /* rounding */
        put_bits(data, &bitpos, 0, method ? 20 : 12); /* complexity payload */
        put_bits(data, &bitpos, 3, 3); put_bits(data, &bitpos, 7, 5);
        if (method) put_bits(data, &bitpos, 2, 3);
        bad += !nj_m4v_parse_vop_bits_vol(data, (nj_u4)((bitpos+7)/8), &position, &vol, &vop);
        bad += position != (nj_u4)bitpos || vop.type != method ||
               vop.qscale != 7 || vop.dc_vlc_thr != 3 || vop.time_increment != 21;
        position = 0;
        bad += nj_m4v_parse_vop_bits_vol(data, (nj_u4)((bitpos-1)/8), &position, &vol, &vop) != 0;
        bad += position != 0;
    }
    ok(bad == 0, "I/P complexity payloads end at the expected quantizer and MV fields");

    bad = 0;
    for (field = 0; field < 6; field++) {
        static const int levels[6] = {-2048,-2047,-1,0,1,2047};
        nj_u1 data[8] = {0};
        nj_s4 coefficients[64] = {0};
        nj_u4 position = 0;
        int result;
        bitpos = 0;
        put_bits(data, &bitpos, 3, 7); put_bits(data, &bitpos, 3, 2);
        put_bits(data, &bitpos, 1, 1); put_bits(data, &bitpos, 2, 6);
        put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, (nj_u4)levels[field] & 4095u, 12);
        put_bits(data, &bitpos, 1, 1);
        result = nj_m4v_ac_block_scan(data, 4, &position, coefficients, 2, 31, 0);
        if (levels[field] == 0 || levels[field] == -2048) bad += result != -1;
        else {
            bad += result != 0 || position != 30 ||
                   coefficients[nj_m4v_scan_index(4)] != levels[field];
            position = 0;
            bad += nj_m4v_ac_block_scan(data, 3, &position, coefficients, 2, 31, 0) != -1;
        }
        data[2] &= 0x7fu; /* first marker at bit 16 */
        position = 0;
        bad += nj_m4v_ac_block_scan(data, 4, &position, coefficients, 2, 31, 0) != -1;
    }
    ok(bad == 0, "escape events honor the scan origin, markers, signed levels and reserved values");

    bad = 0;
    for (field = 0; field < 4; field++) {
        nj_u1 data[8] = {0};
        nj_m4v_vol vol;
        nj_u4 position = 0;
        int quant = 9, number = 0, result;
        memset(&vol, 0, sizeof(vol));
        vol.width = 32; vol.height = 32; vol.quant_precision = 5;
        bitpos = 0;
        put_bits(data, &bitpos, field == 1 ? 0x3f : 0x7f, 8);
        put_bits(data, &bitpos, 0, 16); put_bits(data, &bitpos, 1, 1);
        put_bits(data, &bitpos, 2, 2);
        put_bits(data, &bitpos, field == 2 ? 0 : 4, 5);
        put_bits(data, &bitpos, field == 3, 1);
        result = nj_m4v_resync(data, 8, &position, &vol, 0, 0, &quant, &number);
        if (field == 0) bad += result != 1 || quant != 4 || number != 2;
        else bad += result >= 0 || position != 0 || quant != 9;
    }
    ok(bad == 0, "packet refusal preserves the cursor and rejects bad stuffing, zero QP and extensions");

    bad = 0;
    {
        nj_u4 seed = 0x4d345649u;
        int attempt;
        for (attempt = 0; attempt < 4096; attempt++) {
            nj_u1 data[64];
            nj_s4 guarded[66];
            nj_m4v_vol vol;
            nj_m4v_vop vop;
            nj_u4 position, length;
            int k;
            for (k = 0; k < 64; k++) {
                seed = seed*1664525u + 1013904223u;
                data[k] = (nj_u1)(seed >> 24);
            }
            length = seed % 65u;
            position = length ? (seed >> 8) % (length*8u+1u) : 0;
            memset(guarded, 0, sizeof(guarded));
            guarded[0] = guarded[65] = 0x12345678;
            (void)nj_m4v_ac_block_scan(data, length, &position, guarded+1,
                                        attempt%64, 1+attempt%31, attempt%3);
            bad += position > length*8u || guarded[0] != 0x12345678 ||
                   guarded[65] != 0x12345678;
            (void)nj_m4v_parse_vol(data, length, 0, &vol);
            position = 0;
            (void)nj_m4v_parse_vop_bits(data, length, &position, 1000, &vop);
            bad += position > length*8u;
        }
    }
    ok(bad == 0, "4096 deterministic malformed inputs respect parser bounds and coefficient guards");
}
