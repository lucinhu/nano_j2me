#!/usr/bin/env python3
"""Measure the Texture2D general (4x4) transforms the corpus authors.

A Texture2D payload is [component-transform flag][general flag][component
transform if set][4x4 row-major matrix if the general flag is set][sampler].
The loader stores the matrix into the Transformable's general slot and
recomposes T*R*S*M, which is what the sampler multiplies texture coordinates
by.  The five objects in the corpus that take this path are all in Counter
Strike's map files and all encode a scale plus a translation, so this tool
prints them and writes the numbers a note can cite.

It reuses the container/object walk of check_m3g_polygon_modes.py, which is
itself cross-checked against the in-tree probe.

Usage: python3 tools/check_m3g_texture_transforms.py [DIR ...] [--json FILE]
       (defaults to tests/games)
"""
import collections
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_m3g_polygon_modes import IDENT, objects   # noqa: E402  (shared walk)

TEXTURE2D = 17


def payload_adjust(payload):
    """The object payload viewed with the Object3D header minimised to 12
    bytes, which is the layout the type fields are documented against."""
    ntr = struct.unpack_from("<I", payload, 4)[0]
    at = 8 + 4 * ntr
    npar = struct.unpack_from("<I", payload, at)[0]
    at += 4
    for _ in range(npar):
        at += 8 + struct.unpack_from("<I", payload, at + 4)[0]
    return payload[at - 12:]


def general_transform(payload):
    """The 4x4 floats of a Texture2D with a general transform, or None."""
    a = payload_adjust(payload)
    if a is None or len(a) < 14:
        return None
    xf = 12 + (40 if (a[12] & 1) else 0)
    if len(a) < xf + 2 or not a[xf + 1]:
        return None
    base = xf + 2
    if len(a) != base + 64 + 12:
        return None
    return struct.unpack_from("<16f", a, base)


def main(argv):
    roots = [a for a in argv if not a.startswith("--")]
    json_path = None
    if "--json" in argv:
        i = argv.index("--json")
        json_path = argv[i + 1] if i + 1 < len(argv) else None
        roots = [a for a in roots if a != json_path]
    if not roots:
        roots = ["tests/games"]

    files = 0
    textures = 0
    general = []
    for root in roots:
        import glob
        import zipfile

        jars = sorted(glob.glob(os.path.join(root, "**", "*.jar"),
                                recursive=True)) or [root]
        for jar in jars:
            try:
                z = zipfile.ZipFile(jar)
            except (OSError, zipfile.BadZipFile):
                continue
            for name in z.namelist():
                low = name.lower()
                if not (low.endswith(".m3g") or low.endswith(".m3d")):
                    continue
                try:
                    objs = objects(z.read(name))
                except (OSError, RuntimeError):
                    objs = None
                if objs is None:
                    print("BAD-CONTAINER %s!%s" % (os.path.basename(jar), name))
                    continue
                files += 1
                for otype, payload in objs:
                    if otype != TEXTURE2D:
                        continue
                    textures += 1
                    m = general_transform(payload)
                    if m is not None:
                        general.append((os.path.basename(jar), name,
                                        [round(v, 3) for v in m]))
    print("m3g files=%d Texture2D=%d general-transform=%d" %
          (files, textures, len(general)))
    shapes = collections.Counter()
    for _, _, m in general:
        shapes[(m[0], m[5], m[10], m[3], m[7])] += 1
    print("scale/translate (m0, m5, m10, m3, m7):")
    for k, v in sorted(shapes.items()):
        print("  %-42s x%d" % (str(k), v))
    for title, name, m in general:
        print("  %s ! %s" % (title, name))
        for r in range(4):
            print("     ", m[r * 4:r * 4 + 4])
    if json_path:
        out = {
            "what": "Texture2D general (4x4) transforms in the collected corpus",
            "how": "python3 tools/check_m3g_texture_transforms.py --json "
                   "docs/m3g-texture-transform-results.json tests/games",
            "m3g_files": files,
            "texture_objects": textures,
            "general_transform_objects": len(general),
            "objects": [
                {"jar": t, "entry": n,
                 "matrix": [m[r * 4:r * 4 + 4] for r in range(4)]}
                for t, n, m in general
            ],
        }
        with open(json_path, "w") as f:
            json.dump(out, f, indent=1, ensure_ascii=False)
            f.write("\n")
        print("wrote %s" % json_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
