#!/usr/bin/env python3
"""Rebuild all inline MP3 tables from pinned MIT sources and formulas.

Fetch puremp3 src/tables.rs + LICENSE-MIT and NLayer LayerIIIDecoder.cs +
MpegFrame.cs + LICENSE at the pinned revisions below. Pass their local paths:
  --tables tables.rs --license LICENSE-MIT --nlayer LayerIIIDecoder.cs
  --frame MpegFrame.cs --nlayer-license NLayer-LICENSE --write
Use --check instead to verify the committed generated blocks. No network or
third-party Python packages are used. See docs/CODEC_PROVENANCE.md.

This generator is part of nano_j2me and uses its root MIT license.
"""
import argparse
import hashlib
import math
from pathlib import Path
import re
import struct

SOURCE_REV = "eeb913d6f61cd3ae77aa4d8cb38c6cb3f0f58602"
TABLES_SHA256 = "c59ddd06e355f9d45e308c23d438fe753918579f6a02f95950c857981d686aa9"
LICENSE_SHA256 = "e37223e2aa609fb8b6a095155db70da62a80592777f5d219c040936334d07ff6"
SOURCE_BASE = "https://raw.githubusercontent.com/Herschel/puremp3/" + SOURCE_REV
NLAYER_REV = "82f7a92e2c0b994157720c71b7d3cb41095adc70"
NLAYER_SHA256 = "23bf221aed434eb04650072205571fb836a3a028038af4fa90a56f18148f818d"
FRAME_SHA256 = "d3983a37b38f4044ca627760a7db51760942002a92dbc4333466ad97e548c299"
NLAYER_LICENSE_SHA256 = "5c0358c3876d9655bc5b2d6555030516266c996c0efffb678807d53ade152850"
PAIR_BOOKS = [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24]
TARGET = Path(__file__).resolve().parents[1] / "src/midp/nj_mp3.c"


def pinned(path, expected):
    raw = Path(path).read_bytes()
    actual = hashlib.sha256(raw).hexdigest()
    if actual != expected:
        raise ValueError(f"{path}: SHA256 {actual}, expected {expected}")
    return raw.decode("utf-8")


def rust_array(source, name):
    match = re.search(r"(?:pub )?const " + re.escape(name) + r"\s*:.*?=\s*\[(.*?)\];", source, re.S)
    if not match:
        raise ValueError("missing Rust array: " + name)
    body = re.sub(r"//[^\n]*", "", match[1])
    return re.findall(r"0x[0-9a-fA-F]+|[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?", body)


def huffman_trees(source):
    body = source.split("pub const HUFFMAN_TABLES:", 1)[1].split("\n];", 1)[0]
    trees = []
    for entry in re.findall(r"HuffmanTable\s*\{(.*?)\}", body, re.S):
        data = re.search(r"data:\s*&\[(.*?)\]", entry, re.S)
        if data:
            trees.append([int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", data[1])])
        else:
            name = re.search(r"data:\s*&([A-Z_0-9]+)", entry)[1]
            trees.append([int(x, 0) for x in rust_array(source, name)])
    if len(trees) != 34:
        raise ValueError(f"expected 34 Huffman trees, got {len(trees)}")
    return trees


def codewords(tree):
    """Enumerate the source tree, including its long-offset continuation nodes."""
    leaves = []
    def visit(pos, code, bits):
        if bits > 31 or pos >= len(tree):
            raise ValueError("invalid Huffman tree")
        node = tree[pos]
        if node >> 8 == 0:
            leaves.append((code << (32 - bits), bits, node))
            return
        for bit, shift in [(0, 8), (1, 0)]:
            child = pos
            while True:
                step = (tree[child] >> shift) & 255
                child += step
                if step < 250:
                    break
            visit(child, (code << 1) | bit, bits + 1)
    visit(0, 0, 0)
    return sorted(leaves)


def array(ctype, name, values, kind="int", width=8):
    def fmt(value):
        if kind == "float":
            # Rational-angle cosine zeros are exact; avoid libm residuals.
            return f"{(0.0 if abs(value) < 1e-14 else value):.9e}f"
        if kind == "hex":
            return f"0x{value:08x}u"
        return str(value)
    rows = ["    " + ", ".join(fmt(x) for x in values[i:i + width]) + ","
            for i in range(0, len(values), width)]
    return f"static const {ctype} {name}[{len(values)}] = {{\n" + "\n".join(rows) + "\n};"


def csharp_array(source, name, kind=int):
    """Read only the named initializer, excluding comments and C# type syntax."""
    body = re.search(r"\b" + re.escape(name) + r"\s*=\s*\{(.*?)\};", source, re.S)
    if not body:
        raise ValueError("missing C# array: " + name)
    body = re.sub(r"//[^\n]*", "", body[1])
    return [kind(x) for x in re.findall(r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?", body)]


def binary32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def matrix(ctype, name, rows):
    return (f"static const {ctype} {name}[{len(rows)}][{len(rows[0])}] = {{\n" +
            "\n".join("    { " + ", ".join(map(str, row)) + " }," for row in rows) + "\n};")


def generate(source, nlayer, frame):
    trees = huffman_trees(source)
    words = []
    books = []
    for book in PAIR_BOOKS:
        leaves = codewords(trees[book])
        books.append((len(words), len(leaves), max(x[1] for x in leaves)))
        words += leaves
    if len(words) != 1378:
        raise ValueError("expected 1378 pair codewords")
    out = {"mp3_hbook": "static const mp3_huff_book mp3_hbook[15] = {\n" +
           "\n".join("    { %d, %d, %d }," % row for row in books) + "\n};"}
    for col, name, ctype, kind in [(2, "sym", "nj_u1", "int"),
                                    (0, "code", "nj_u4", "hex"),
                                    (1, "len", "nj_u1", "int")]:
        out["mp3_huff_" + name] = array(ctype, "mp3_huff_" + name, [x[col] for x in words], kind, 8 if col == 0 else 16)
    quad = []
    for book in [32, 33]:
        leaves = codewords(trees[book])
        for prefix in range(64):
            matches = [(bits << 4) | symbol for code, bits, symbol in leaves
                       if prefix >> (6 - bits) == code >> (32 - bits)]
            if len(matches) != 1:
                raise ValueError("invalid quad prefix coverage")
            quad += matches
    out["mp3_quad_lut"] = array("nj_u1", "mp3_quad_lut", quad, width=16)
    # The ISO synthesis window is quantized to units of 1/65536. Source
    # decimal literals approximate those exact fixed-point coefficients.
    window = [round(float(x) * 65536) / 65536 for x in rust_array(source, "SYNTH_DTBL")]
    if len(window) != 512:
        raise ValueError("expected 512 synthesis coefficients")
    out["mp3_dwin"] = array("float", "mp3_dwin", window, "float")
    # NLayer provides the MPEG-2/2.5 bands missing from the selected puremp3
    # input. Preserve its complete copyright/permission notices below.
    for suffix, field, size in [("long", "_sfBandIndexLTable", 207),
                                ("short", "_sfBandIndexSTable", 126)]:
        values = csharp_array(nlayer, field)
        if len(values) != size:
            raise ValueError("invalid scalefactor band count: " + field)
        if suffix == "long":
            # NLayer's 24-kHz band 18 is 330; retain the existing decoder's
            # 332 boundary. This isolated compatibility correction is recorded
            # explicitly in CODEC_PROVENANCE.md, not attributed to NLayer.
            values[4 * 23 + 18] = 332
        out["mp3_band_" + suffix] = array("nj_u2", "mp3_band_" + suffix, values)
    for name, field in [("mp3_pretab", "PRETAB"), ("mp3_lsf_nsf", "_sfbBlockCntTab")]:
        out[name] = array("nj_u1", name, csharp_array(nlayer, field))
    slen = csharp_array(nlayer, "_slen")
    if len(slen) != 32:
        raise ValueError("invalid scalefactor size count")
    for i in range(2):
        name = "mp3_slen" + str(i + 1)
        out[name] = array("nj_u1", name, slen[16*i:16*(i+1)])
    indices = [PAIR_BOOKS.index(i) if i in PAIR_BOOKS else
               13 if 16 <= i <= 23 else 14 if 24 <= i <= 31 else 255 for i in range(32)]
    out["mp3_huff_idx"] = array("nj_u1", "mp3_huff_idx", indices)
    huff_body = source.split("pub const HUFFMAN_TABLES:", 1)[1].split("\n];", 1)[0]
    linbits = [int(x) for x in re.findall(r"linbits:\s*(\d+)", huff_body)]
    if len(linbits) != 34:
        raise ValueError("invalid linbits count")
    out["mp3_linbits"] = array("nj_u1", "mp3_linbits", linbits[:32])
    rates = csharp_array(frame, "_bitRateTable")
    if len(rates) != 90:
        raise ValueError("invalid bitrate count")
    out["mp3_bitrate_m1"] = array("nj_u2", "mp3_bitrate_m1", rates[30:45] + [0])
    out["mp3_bitrate_lsf"] = array("nj_u2", "mp3_bitrate_lsf", rates[75:90] + [0])
    # Sample-rate facts from NLayer's SampleRate switch; version divisions
    # and row indices are generated rather than transcribed as C arrays.
    base_rates = [int(x) for x in re.findall(r"case [012]: sr = (\d+);", frame)]
    if len(base_rates) != 3:
        raise ValueError("invalid sample-rate switch")
    out["mp3_rate_tab"] = matrix("nj_u2", "mp3_rate_tab", [[x // (1 << v) for x in base_rates] for v in range(3)])
    out["mp3_rate_idx"] = matrix("nj_u1", "mp3_rate_idx", [[3*v+i for i in range(3)] for v in range(3)])
    # Normalize the eight antialias slopes to a unit rotation. Round the input
    # slope (four decimal places) to binary32 before normalization to preserve
    # the decoder's arithmetic contract, rather than changing three PCM factors
    # to NLayer's differently rounded sine literals.
    cosine = csharp_array(nlayer, "_scs", float)
    sine = csharp_array(nlayer, "_sca", float)
    slopes = [binary32(round(s/c, 4)) for c, s in zip(cosine, sine)]
    out["mp3_aa_c"] = array("float", "mp3_aa_c", cosine, "float")
    out["mp3_aa_s"] = array("float", "mp3_aa_s", [s / math.sqrt(1+s*s) for s in slopes], "float")
    # The remaining generated values are evaluations of these formulas, not
    # copied decoder code. All angles below are in radians.
    floats = {
        "mp3_is_m1": [0.0] + [math.tan(i * math.pi / 12) / (1 + math.tan(i * math.pi / 12)) for i in range(1, 6)] + [1.0],
        "mp3_nmat": [math.cos(math.pi / 64 * (16 + i) * (2 * k + 1)) for i in range(64) for k in range(16)],
        "mp3_pow2f": [2 ** (i / 4) for i in range(4)],
        "p2r": [2 ** (i / 3) for i in range(3)],
        "mp3_is_lsf": [2 ** (-((i + 1) // 2 if channel == 0 else i // 2) * (scale + 1) / 4)
                       if i % 2 != channel else 1.0
                       for scale in range(2) for channel in range(2) for i in range(16)],
        "mp3_win_norm": [math.sin(math.pi / 36 * (i + 0.5)) for i in range(36)],
        "mp3_win_short": [math.sin(math.pi / 12 * (i + 0.5)) for i in range(12)],
    }
    for suffix, size, offset in [("la", 18, 0), ("lb", 18, 18), ("sa", 6, 0), ("sb", 6, 6)]:
        floats["mp3_imdct_" + suffix] = [math.cos(math.pi / (4 * size) * (2 * (i + offset) + size + 1) * (2 * k + 1))
                                           for i in range(size // 2) for k in range(size)]
    normal, short = floats["mp3_win_norm"], floats["mp3_win_short"]
    floats["mp3_win_start"] = normal[:18] + [1.] * 6 + short[6:] + [0.] * 6
    floats["mp3_win_stop"] = [0.] * 6 + short[:6] + [1.] * 6 + normal[18:]
    for name, values in floats.items():
        out[name] = array("float", name, values, "float")
    return out


def c_values(block):
    body = block.split("=", 1)[1]
    return [struct.pack("<f", float(x.rstrip("f"))) if "." in x or "e" in x.lower() and not x.lower().startswith("0x")
            else int(x.rstrip("u"), 0)
            for x in re.findall(r"0x[0-9a-fA-F]+u?|[-+]?\d+\.\d+(?:[eE][-+]?\d+)?f?|\d+", body)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tables", required=True)
    parser.add_argument("--license", required=True)
    parser.add_argument("--nlayer", required=True)
    parser.add_argument("--frame", required=True)
    parser.add_argument("--nlayer-license", required=True)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--write", action="store_true")
    actions.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = pinned(args.tables, TABLES_SHA256)
    license_text = pinned(args.license, LICENSE_SHA256)
    nlayer = pinned(args.nlayer, NLAYER_SHA256)
    frame = pinned(args.frame, FRAME_SHA256)
    nlayer_license = pinned(args.nlayer_license, NLAYER_LICENSE_SHA256)
    target = TARGET.read_text()
    updated = target
    changed = 0
    for name, block in generate(source, nlayer, frame).items():
        pattern = r"static const [^;=]+\b" + name + r"(?:\[\d+\])+\s*=\s*\{.*?\};"
        old = re.search(pattern, target, re.S)
        if not old:
            raise ValueError("missing target block " + name)
        before, after = c_values(old[0]), c_values(block)
        if len(before) != len(after):
            raise ValueError("table size changed: " + name)
        count = sum(a != b for a, b in zip(before, after))
        print(f"{name}: {len(after)} values, {count} binary32/integer changes")
        changed += count
        updated = re.sub(pattern, lambda _: block, updated, count=1, flags=re.S)
    fluendo_notice = re.sub(r"^ \* ?", "", nlayer.lstrip("\ufeff").split("*/", 1)[0].removeprefix("/*"), flags=re.M).strip()
    notice = ("/* ==== generated MP3 tables ====\n"
              " * Huffman/quad trees and synthesis window: puremp3 (MIT option).\n"
              " * Author: Mike Welsh <mwelsh@gmail.com> (upstream Cargo.toml).\n"
              " * Source: Herschel/puremp3 at " + SOURCE_REV + "\n"
              " * Reproduce: tools/gen_mp3_tables.py; docs/CODEC_PROVENANCE.md.\n"
              " * Upstream LICENSE-MIT follows verbatim (no copyright line supplied).\n *\n" +
              "\n".join(" * " + line if line else " *" for line in license_text.rstrip().splitlines()) +
              "\n *\n * Band/scalefactor/bitrate/antialias metadata: NLayer at " + NLAYER_REV +
              "\n * NLayer root license and file-level Fluendo MIT option follow.\n *\n" +
              "\n".join(" * " + line if line else " *" for line in nlayer_license.rstrip().splitlines()) +
              "\n *\n" +
              "\n".join(" * " + line.rstrip() if line.rstrip() else " *" for line in fluendo_notice.splitlines()) +
              "\n */")
    begin = updated.index("/* ==== generated MP3 tables")
    end = updated.index("*/", begin) + 2
    updated = updated[:begin] + notice + updated[end:]
    if args.write:
        TARGET.write_text(updated)
        print(f"updated {TARGET}; total changed values: {changed}")
    elif target != updated:
        raise SystemExit("generated blocks differ; rerun with --write")
    else:
        print("generated blocks match")


if __name__ == "__main__":
    main()
