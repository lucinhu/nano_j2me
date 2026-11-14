#!/usr/bin/env python3
"""Regenerate full-pixel MPEG4 test references with external FFmpeg.

Only test data is emitted; no decoder code is copied.  Reference input bytes
come from existing tests.  Run from the repository root.  Requires ffmpeg in
PATH.  The runtime and normal test build do not require FFmpeg or Python.
"""
import hashlib
from pathlib import Path
import re
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def array(path, name):
    text = (ROOT / path).read_text()
    match = re.search(r"\b" + name + r"\[[^]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        raise ValueError(name)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})\b", match[1]))


def main():
    source = "tests/test_m4v.c"
    seq = array(source, "cfg_1942") + b"".join(array(source, f"m4v_sample_{i}") for i in range(4))
    inputs = [
        ("sequence", "m4v", seq),
        ("clip", "mov", array("tests/m4v_clip.h", "m4v_clip_3gp")),
        ("sliced", "mov", array("tests/m4v_slice_clip.h", "m4v_slice_3gp")),
        ("acpred", "mov", array("tests/m4v_clip.h", "m4v_acpred_3gp")),
    ]
    version = subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0]
    out = ["/* Generated test pixel data. Regenerate: python3 tests/private/make_m4v_reference.py",
           " * " + version,
           " * Decoder: -threads 1 -idct simple -flags +bitexact; output yuv420p.",
           " * Existing authored/synthetic fixture inputs are decoded, not new media.",
           " * Zlib-compressed raw YUV420p, decoded by the existing built-in inflater.",
           " * Generated pixel data only; this is not runtime code.",
           " */", "#ifndef NJ_TEST_M4V_REFERENCE_DATA_H", "#define NJ_TEST_M4V_REFERENCE_DATA_H", ""]
    for name, fmt, encoded in inputs:
        decoded = subprocess.check_output([
            "ffmpeg", "-v", "error", "-threads", "1", "-idct", "simple", "-flags", "+bitexact",
            "-f", fmt, "-i", "pipe:0", "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "yuv420p", "pipe:1"
        ], input=encoded)
        packed = zlib.compress(decoded, 9)
        out.append(f"/* Input SHA256 {hashlib.sha256(encoded).hexdigest()} */")
        out.append(f"#define M4V_REFERENCE_{name.upper()}_SIZE {len(decoded)}u")
        out.append(f"static const nj_u1 m4v_reference_{name}[{len(packed)}] = {{")
        for i in range(0, len(packed), 16):
            out.append("    " + ", ".join(f"0x{x:02x}" for x in packed[i:i+16]) + ",")
        out.append("};\n")
        print(f"{name}: {len(decoded)} decoded bytes, {len(packed)} packed")
    out.append("#endif\n")
    (ROOT / "tests/private/m4v_reference_data.h").write_text("\n".join(out))


if __name__ == "__main__":
    main()
