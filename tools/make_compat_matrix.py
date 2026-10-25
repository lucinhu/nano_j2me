#!/usr/bin/env python3
"""Turn a corpus sweep into docs/COMPATIBILITY_MATRIX.md.

Usage: tools/make_compat_matrix.py [rows directory]

The directory must hold rows.jsonl (one JSON object per JAR; see
.local-tools/debug-runs/compat-matrix-20261025/sweep.py for the recipe that
produced the committed table).  Optional extra_rows.jsonl adds rows.
"""
import collections
import json
import os
import subprocess
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = (os.sys.argv[1] if len(os.sys.argv) > 1
       else os.path.join(ROOT, ".local-tools", "debug-runs", "compat-matrix-20261025"))
DOC = ROOT + "/docs/COMPATIBILITY_MATRIX.md"

def read_meta(path):
    """Return the provenance the sweep recorded, or None.

    A table whose rows came from an older binary must not claim to have been
    built from the commit that happens to be checked out when it is rendered.
    """
    try:
        with open(os.path.join(path, "meta.json"), encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


VERDICT = {"ok": "运行正常", "title-exception": "标题自身异常", "oom": "堆不足",
           "bad-jar": "非可用包", "timeout": "超时", "failure": "失败",
           "no-frame": "未呈现画面",
           "retry-heap32": "16MB 不足",
           "title-exit": "标题自行退出"}


def load():
    rows, seen = [], set()
    for name in ("rows.jsonl", "extra_rows.jsonl"):
        path = os.path.join(OUT, name)
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            if row.get("file") not in seen:
                seen.add(row.get("file"))
                rows.append(row)
    return rows


def detect(row):
    """The JARs do not bundle the platform APIs they call: read the constant pool."""
    found = set()
    try:
        with zipfile.ZipFile(os.path.join(ROOT, "tests", "games", row.get("file", ""))) as jar:
            for info in jar.infolist():
                if not info.filename.endswith(".class"):
                    continue
                data = jar.read(info)          # entries are deflated: search the bytes
                if b"javax/microedition/m3g/" in data:
                    found.add("m3g")
                if b"com/mascotcapsule/micro3d/" in data:
                    found.add("mascot")
                if b"com/nokia/mid/" in data:
                    found.add("nokia-ui")
    except (OSError, zipfile.BadZipFile):
        return
    row["apis"] = sorted(found)


def main():
    rows = load()
    for row in rows:
        detect(row)
    verdicts = collections.Counter(row.get("verdict", "?") for row in rows)
    apis = collections.Counter(",".join(row.get("apis", [])) or "-" for row in rows)
    head = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()
    meta = read_meta(OUT)
    if meta and meta.get("commit"):
        built = "Built from `%s`, %s (recorded by `tools/sweep_compat.py`)." % (
            meta["commit"], meta.get("date", "unknown date"))
    else:
        built = "Built from `%s`, 2026-10-25." % head
    out = [
        "# Compatibility matrix", "",
        "One row per JAR in `tests/games` (%d titles), from one headless replay per title" % len(rows),
        "with the same recipe, so neighbouring rows are comparable:", "",
        "* port `headless`, screen `240x320`, `--fps 30`, `--run-frames 600`;",
        "* heap `16 MB`, retried at `32 MB` only after an out-of-memory;",
        "* one shared input script (fire, both soft keys, two screen taps, four arrows,",
        "  digits, `*`), a fresh RMS directory per title;",
        "* `运行正常` = exit 0, at least half the frames painted, nothing uncaught.",
        "* `未呈现画面` = exit 0, ran to the end on its own, never painted a frame"
        " (the run loop had nothing to show - a quiet title, not a crash).",
        "",
        "Corpus provenance and size are in `docs/CORPUS.md`; regenerate this table",
        "with `tools/make_compat_matrix.py <rows dir>` after a new sweep.",
        "", built, "",
        "| verdict | titles |", "| --- | --- |",
    ]
    out += ["| %s | %d |" % (VERDICT.get(k, k), n) for k, n in verdicts.most_common()]
    out += ["", "| 3D API used | titles |", "| --- | --- |"]
    out += ["| %s | %d |" % (k or "-", n) for k, n in apis.most_common()]
    out += ["", "| title | size KB | MIDlet name | vendor | version | profile | 3D |"
            " classes | heap MB | frames | bytecodes | uncaught | exceptions | verdict | note |",
            "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |"]
    for row in sorted(rows, key=lambda item: item.get("file", "")):
        out.append("| %s | %d | %s | %s | %s | %s | %s | %d | %d | %d | %d | %d | %s | %s | %s |" % (
            row.get("file", "?"), max(1, row.get("size", 0) // 1024),
            (row.get("name") or "-")[:28], (row.get("vendor") or "-")[:20],
            (row.get("version") or "-")[:10], (row.get("profile") or "-")[:10],
            ",".join(row.get("apis", [])) or "-",
            row.get("classes", 0), row.get("heap", 0), row.get("frames", 0),
            row.get("bytecodes", 0), row.get("uncaught", 0),
            (row.get("exceptions") or "-")[:32],
            VERDICT.get(row.get("verdict", "?"), row.get("verdict", "?")),
            (row.get("note") or "")[:44]))
    open(DOC, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("wrote", DOC, len(rows), dict(verdicts), dict(apis))


if __name__ == "__main__":
    main()
