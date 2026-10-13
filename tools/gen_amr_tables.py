#!/usr/bin/env python3
"""nano_j2me - AMR-NB decoder table generator.

Emits

    src/midp/nj_amr_tables.h    the AMR narrowband constant tables (checked in)

The numbers are the AMR speech codec's own constants - the LSF quantiser
codebooks of 3GPP TS 26.090 table 8 (q_plsf_3) and table 9 (q_plsf_5), the gain
quantisers of TS 26.090 table 7, the per-mode bit allocation of TS 26.101, the
algebraic codebook track map, and the decoder's high-pass and phase-dispersion
filters.  They are data, not code: every AMR-NB implementation in existence
carries the same values, and the runtime needs its own copy of them because
the decoder in src/midp/nj_amr.c is freestanding (no libc, no libm, no
allocation beyond the caller's).

Transcribing 1600 lines of tables by hand is how tables acquire typos, so the
transcription is mechanical and reproducible from two source files that already
carry them in a parseable form - FFmpeg's libavcodec/amrnbdata.h (which is
itself a transcription of the standard's tables) and the four interpolator /
attenuation tables in libavcodec/acelp_vectors.c.  Point the generator at a
checkout of those two files:

    tools/gen_amr_tables.py amrnbdata.h acelp_vectors.c > src/midp/nj_amr_tables.h

The emitted names, types and comments are this project's own; only the
constants come across, and only constants that exist in the standard.  Nothing
from either source file other than those constants is reproduced.

Two rules the output must keep, because src/midp/nj_amr.c indexes them:

  * `order_*` tables are the bit-allocation maps.  Each entry is
    `bits, field_offset, bit_position...`, where a bit position addresses the
    payload LSB-first within a byte and the field is assembled MSB-first, and
    where `field_offset` is a byte offset into the unpacked frame struct
    (NJ_AMR_OFF / NJ_AMR_OFF_SUB in nj_amr.h).  The generator rewrites those two
    macros but never the numbers.
  * `nj_amr_payload_bytes[]` holds the size of each mode's payload WITHOUT its
    one-byte frame header (4.75 kbit/s is 12 payload bytes + 1 header byte).
"""

import re
import sys

# ---------------------------------------------------------------------------
# What to copy, and what to call it here.
#
# The rename table is explicit rather than a regex: the generated header is
# read by people debugging audio, and a name like nj_amr_lsf_3_1_7k95 says
# which codebook and which mode it belongs to.
# ---------------------------------------------------------------------------
AMR_DATA_TABLES = {
    # --- bit allocation (TS 26.101) ---
    "order_MODE_4k75": "nj_amr_order_4k75",
    "order_MODE_5k15": "nj_amr_order_5k15",
    "order_MODE_5k9": "nj_amr_order_5k9",
    "order_MODE_6k7": "nj_amr_order_6k7",
    "order_MODE_7k4": "nj_amr_order_7k4",
    "order_MODE_7k95": "nj_amr_order_7k95",
    "order_MODE_10k2": "nj_amr_order_10k2",
    "order_MODE_12k2": "nj_amr_order_12k2",
    "frame_sizes_nb": "nj_amr_payload_bytes",
    # --- codebooks and quantisers (TS 26.090) ---
    "base_five_table": "nj_amr_base_five_table",
    "lsf_3_3_MODE_5k15": "nj_amr_lsf_3_3_5k15",
    "lsf_3_1_MODE_7k95": "nj_amr_lsf_3_1_7k95",
    "lsf_3_1": "nj_amr_lsf_3_1",
    "lsf_3_2": "nj_amr_lsf_3_2",
    "lsf_3_3": "nj_amr_lsf_3_3",
    "lsf_5_1": "nj_amr_lsf_5_1",
    "lsf_5_2": "nj_amr_lsf_5_2",
    "lsf_5_3": "nj_amr_lsf_5_3",
    "lsf_5_4": "nj_amr_lsf_5_4",
    "lsf_5_5": "nj_amr_lsf_5_5",
    "lsf_3_mean": "nj_amr_lsf_3_mean",
    "lsf_5_mean": "nj_amr_lsf_5_mean",
    "pred_fac": "nj_amr_pred_fac",
    "track_position": "nj_amr_track_position",
    "gray_decode": "nj_amr_gray_decode",
    "qua_gain_pit": "nj_amr_qua_gain_pit",
    "qua_gain_code": "nj_amr_qua_gain_code",
    "energy_mean": "nj_amr_energy_mean",
    "energy_pred_fac": "nj_amr_energy_pred_fac",
    "gains_MODE_4k75": "nj_amr_gains_4k75",
    "gains_high": "nj_amr_gains_high",
    "gains_low": "nj_amr_gains_low",
    # --- decoder filters ---
    "ir_filter_strong_MODE_7k95": "nj_amr_ir_filter_strong_7k95",
    "ir_filter_strong": "nj_amr_ir_filter_strong",
    "ir_filter_medium": "nj_amr_ir_filter_medium",
    "highpass_zeros": "nj_amr_highpass_zeros",
    "highpass_poles": "nj_amr_highpass_poles",
    "highpass_gain": "nj_amr_highpass_gain",
    # --- decoder start-up state ---
    "lsp_sub4_init": "nj_amr_lsp_sub4_init",
    "lsp_avg_init": "nj_amr_lsp_avg_init",
}

VECTOR_TABLES = {
    "ff_b60_sinc": "nj_amr_b60_sinc",
    "ff_pow_0_7": "nj_amr_pow_0_7",
    "ff_pow_0_75": "nj_amr_pow_0_75",
    "ff_pow_0_55": "nj_amr_pow_0_55",
}

TYPE_MAP = {
    "uint8_t": "nj_u1",
    "int8_t": "signed char",
    "uint16_t": "nj_u2",
    "int16_t": "nj_s2",
    "float": "float",
    "double": "float",
}

RENAME_SYMBOLS = [
    (r"\bAMR_BIT\(", "NJ_AMR_OFF("),
    (r"\bAMR_OF\(", "NJ_AMR_OFF_SUB("),
    (r"\bLP_FILTER_ORDER\b", "NJ_AMR_LPC_ORDER"),
    (r"\bAMR_SUBFRAME_SIZE\b", "NJ_AMR_SUBFRAME"),
    (r"\bN_MODES\b", "NJ_AMR_MODES"),
]

BLOCK_RE = re.compile(
    r"^(?:static\s+)?const\s+(?P<type>[A-Za-z_][A-Za-z0-9_ ]*?)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?P<dims>\[[^=]*?\])?\s*=\s*\{",
    re.MULTILINE,
)

HEADER = """/*
 * nano_j2me - nj_amr_tables.h
 *
 * GENERATED by tools/gen_amr_tables.py - do not edit by hand.
 *
 * The constant tables of the AMR narrowband speech codec: the LSF quantiser
 * codebooks and gain quantisers of 3GPP TS 26.090, the per-mode bit allocation
 * of 3GPP TS 26.101, the algebraic codebook track map, and the filters the
 * decoder runs the synthesis through.  Every AMR-NB implementation carries
 * these same numbers; they are the standard's data.
 *
 * nj_amr.c is the only file that includes this one.
 */
#ifndef NJ_AMR_TABLES_H
#define NJ_AMR_TABLES_H

#if !defined(NJ_AMR_LPC_ORDER) || !defined(NJ_AMR_SUBFRAME)
#  error "include nj_amr.h before nj_amr_tables.h"
#endif

"""

FOOTER = "#endif /* NJ_AMR_TABLES_H */\n"


def blocks(text):
    """Yield (name, raw block text) for every `static const T name[] = {...};`."""
    pos = 0
    while True:
        m = BLOCK_RE.search(text, pos)
        if not m:
            return
        start = m.start()
        depth = 0
        i = m.end() - 1
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        end = text.index(";", i) + 1
        yield m, text[start:end]
        pos = end


def rewrite(name_out, m, raw):
    body = raw[raw.index("{"):]
    typ = TYPE_MAP[m.group("type").strip()]
    dims = m.group("dims").strip()
    for pat, rep in RENAME_SYMBOLS:
        body = re.sub(pat, rep, body)
        dims = re.sub(pat, rep, dims)
    return "static const %s %s%s = %s\n" % (typ, name_out, dims, body)


# One table in the decoder is a lone constant rather than an array.
SCALAR_RE = re.compile(
    r"^static const\s+(?P<type>[A-Za-z_][A-Za-z0-9_ ]*?)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<value>[^;]+);",
    re.MULTILINE,
)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__.split("The emitted names")[0].strip() + "\n")
        sys.stderr.write("\nusage: gen_amr_tables.py amrnbdata.h "
                         "acelp_vectors.c > src/midp/nj_amr_tables.h\n")
        return 2

    wanted = dict(AMR_DATA_TABLES)
    wanted.update(VECTOR_TABLES)

    found = {}
    for path in argv[1:]:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        for m, raw in blocks(text):
            name = m.group("name")
            if name in wanted:
                found[name] = rewrite(wanted[name], m, raw)
        for m in SCALAR_RE.finditer(text):
            name = m.group("name")
            if name in wanted and name not in found:
                found[name] = "static const %s %s = %s;\n" % (
                    TYPE_MAP[m.group("type").strip()], wanted[name],
                    m.group("value").strip())

    missing = [n for n in wanted if n not in found]
    if missing:
        sys.stderr.write("missing tables: %s\n" % ", ".join(sorted(missing)))
        return 1

    out = [HEADER]
    for name in sorted(wanted, key=lambda n: wanted[n]):
        out.append(found[name])
    out.append(FOOTER)
    sys.stdout.write("".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
