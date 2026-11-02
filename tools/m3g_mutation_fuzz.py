#!/usr/bin/env python3
"""Deterministic mutation driver for real .m3g corpus assets (task-31).

Two modes:

  baseline  copy every extracted seed into <run>/fs/mut/ and write a manifest
            so the MIDlet harness loads each one unchanged.  This separates
            "asset is valid" from "mutation is a finding" and records the
            object counts of the healthy files.

  mutate    apply a fixed LCG-driven mutation schedule to the seeds.  A mutant
            is one or two of: bit flip, byte overwrite, truncation, 32-bit
            overwrite (huge/zero counts, lengths), section header corruption
            (compression scheme / compressed length / uncompressed length),
            object header corruption (type byte / object length), byte-range
            deletion and garbage append.  Mutants are written next to a
            manifest; <run>/map.tsv maps mutant -> seed so a crash can be
            attributed and minimised.

The runtime side is tests/m3gmutdemo/M3gMutDemo.java (Loader.load only).
Everything is deterministic: same arguments -> byte-identical run directory.
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import sys

MASK64 = (1 << 64) - 1
MULT = 6364136223846793005
ADD = 1442695040888963407

BIG32 = (0, 1, 2, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)
BYTES = (0x00, 0x01, 0x7F, 0x80, 0xFF)


class Rng:
    def __init__(self, seed):
        self.x = seed & MASK64

    def next_u32(self):
        self.x = (self.x * MULT + ADD) & MASK64
        return (self.x >> 33) & 0xFFFFFFFF

    def rand(self, n):
        return 0 if n <= 0 else self.next_u32() % n

    def pick(self, seq):
        return seq[self.rand(len(seq))]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def put_u32(data, off, value):
    struct.pack_into("<I", data, off, value & 0xFFFFFFFF)


def sections(data):
    """Offsets of plausible sections; stops at the first malformed one."""
    out = []
    off = 12
    while off + 13 <= len(data):
        scheme = data[off]
        clen = u32(data, off + 1)
        if scheme > 1 or clen < 13 or off + clen > len(data):
            break
        out.append(off)
        off += clen
    return out


def objects(data, sec_off):
    """Offsets/sizes of objects in a stored (scheme 0) section body."""
    out = []
    if data[sec_off] != 0:
        return out
    clen = u32(data, sec_off + 1)
    begin = sec_off + 9
    end = sec_off + clen - 4
    off = begin
    while off + 5 <= end:
        olen = u32(data, off + 1)
        if off + 5 + olen > end:
            break
        out.append((off, olen))
        off += 5 + olen
    return out


def mutate(data, rng, seed_name):
    """Apply one or two mutations; returns (bytes, list-of-op-names)."""
    d = bytearray(data)
    ops = []
    nops = 1 + (rng.next_u32() & 1)
    for _ in range(nops):
        kind = rng.rand(9)
        if kind == 0 and len(d) > 0:  # bit flip
            bit = rng.rand(len(d) * 8)
            d[bit // 8] ^= 1 << (bit % 8)
            ops.append("bitflip@%d" % (bit // 8))
        elif kind == 1 and len(d) > 0:  # byte overwrite
            off = rng.rand(len(d))
            d[off] = rng.pick(BYTES)
            ops.append("byte@%d" % off)
        elif kind == 2 and len(d) > 1:  # truncation
            keep = rng.rand(len(d))
            del d[keep:]
            ops.append("trunc@%d" % keep)
        elif kind == 3 and len(d) >= 4:  # 32-bit overwrite
            off = rng.rand(len(d) - 3)
            put_u32(d, off, rng.pick(BIG32))
            ops.append("u32@%d" % off)
        elif kind == 4:  # section header corruption
            secs = sections(d)
            if secs:
                s = rng.pick(secs)
                which = rng.rand(3)
                if which == 0:
                    d[s] = rng.rand(4)
                    ops.append("scheme@%d" % s)
                elif which == 1 and s + 5 <= len(d):
                    put_u32(d, s + 1, rng.pick((0, 1, 12, 13, 0x10000,
                                                0x7FFFFFFF, 0xFFFFFFFF,
                                                len(d), len(d) + 1)))
                    ops.append("clen@%d" % s)
                elif s + 9 <= len(d):
                    put_u32(d, s + 5, rng.pick((0, 1, 0x100, 0x10000,
                                                0x7FFFFFFF, 0xFFFFFFFF)))
                    ops.append("ulen@%d" % s)
        elif kind == 5:  # object header corruption
            objs = []
            for s in sections(d):
                objs.extend(objects(d, s))
            if objs:
                off, _ = rng.pick(objs)
                if rng.next_u32() & 1:
                    d[off] = rng.pick((0, 1, 3, 9, 11, 14, 20, 21, 22, 23, 254,
                                       255))
                    ops.append("otype@%d" % off)
                elif off + 5 <= len(d):
                    put_u32(d, off + 1, rng.pick((0, 1, 4, 5, 0x1000, 0x10000,
                                                  0x7FFFFFFF, 0xFFFFFFFF)))
                    ops.append("olen@%d" % off)
        elif kind == 6 and len(d) < 1 << 20:  # garbage append
            extra = bytes(rng.rand(256) for _ in range(1 + rng.rand(64)))
            d.extend(extra)
            ops.append("append@%d" % len(extra))
        elif kind == 8:  # object payload 32-bit write (counts/formats/flags)
            objs = []
            for s in sections(d):
                objs.extend(objects(d, s))
            if objs:
                off, olen = rng.pick(objs)
                if olen >= 4:
                    at = off + 5 + rng.rand(olen - 3)
                    put_u32(d, at, rng.pick(BIG32 + (4, 8, 16, 32, 64, 256)))
                    ops.append("payload@%d" % at)
        elif kind == 7 and len(d) > 20:  # byte-range deletion
            start = rng.rand(len(d) - 1)
            span = 1 + rng.rand(min(32, len(d) - start - 1))
            del d[start:start + span]
            ops.append("delete@%d+%d" % (start, span))
        else:
            # Degenerate seeds (empty/1-byte); fall back to a byte write.
            if len(d) > 0:
                d[rng.rand(len(d))] ^= 0xFF
            ops.append("bytefallback")
    return bytes(d), ops


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", required=True)
    ap.add_argument("--run", required=True)
    ap.add_argument("--mode", choices=("baseline", "mutate"), default="mutate")
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--seeds-file", default=None,
                    help="optional newline list of seed file names (subset)")
    ap.add_argument("--lcg-seed", type=lambda s: int(s, 0), default=0x5eed31)
    ap.add_argument("--keep", action="store_true",
                    help="keep existing mutants (default: clean mut dir)")
    args = ap.parse_args()

    seeds = sorted(f for f in os.listdir(args.seeds)
                   if os.path.isfile(os.path.join(args.seeds, f)))
    if args.seeds_file:
        allowed = {l.strip() for l in open(args.seeds_file, encoding="utf-8")
                   if l.strip()}
        seeds = [s for s in seeds if s in allowed]
    if not seeds:
        sys.exit("no seeds in %s" % args.seeds)

    mutdir = os.path.join(args.run, "fs", "mut")
    if not args.keep and os.path.isdir(mutdir):
        shutil.rmtree(mutdir)
    os.makedirs(mutdir, exist_ok=True)

    names = []
    map_lines = []
    hist = {}
    rng = Rng(args.lcg_seed)
    total_bytes = 0

    if args.mode == "baseline":
        for i, seed in enumerate(seeds):
            src = os.path.join(args.seeds, seed)
            name = "b%04d.m3g" % i
            shutil.copyfile(src, os.path.join(mutdir, name))
            names.append(name)
            map_lines.append("%s\t%s\toriginal" % (name, seed))
            total_bytes += os.path.getsize(src)
    else:
        for k in range(args.count):
            seed = seeds[k % len(seeds)]
            with open(os.path.join(args.seeds, seed), "rb") as f:
                data = f.read()
            out, ops = mutate(data, rng, seed)
            name = "m%04d.m3g" % k
            with open(os.path.join(mutdir, name), "wb") as f:
                f.write(out)
            names.append(name)
            map_lines.append("%s\t%s\t%s" % (name, seed, ",".join(ops)))
            total_bytes += len(out)
            for op in ops:
                hist[op.split("@")[0]] = hist.get(op.split("@")[0], 0) + 1

    with open(os.path.join(mutdir, "manifest.txt"), "w") as f:
        f.write("\n".join(names) + "\n")
    with open(os.path.join(args.run, "map.tsv"), "w") as f:
        f.write("\n".join(map_lines) + "\n")

    print(json.dumps({
        "mode": args.mode,
        "seeds": len(seeds),
        "files": len(names),
        "bytes": total_bytes,
        "lcg_seed": hex(args.lcg_seed),
        "mut_dir": mutdir,
        "op_histogram": hist,
    }, indent=2))
    print("manifest: %s" % os.path.join(mutdir, "manifest.txt"))


if __name__ == "__main__":
    main()
