#!/usr/bin/env python3
"""One row per corpus title: manifest facts plus one paced headless run.

Usage: tools/sweep_compat.py [output directory]

Writes rows.jsonl there, one JSON object per JAR; tools/make_compat_matrix.py
turns that into docs/COMPATIBILITY_MATRIX.md.  Titles that cannot start on the
generic handset are retried once with --device nokia6230.
"""
import json, os, re, subprocess, sys, zipfile
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.sys.argv[1] if len(os.sys.argv) > 1 else os.path.join(
    ROOT, ".local-tools", "debug-runs", "compat-matrix-20261025")
RUN = 600
KEYS = ["30:fire", "55:soft1", "80:53", "110:pressed@120,160", "112:released@120,160",
        "140:left", "165:right", "190:48", "215:up", "240:down", "265:soft2", "290:5",
        "320:fire", "345:pressed@60,300", "347:released@60,300", "375:stars",
        "400:6", "430:fire", "470:soft1", "510:pressed@180,300", "512:released@180,300",
        "545:fire", "575:left"]

def manifest(path):
    try:
        with zipfile.ZipFile(path) as z:
            m = z.read("META-INF/MANIFEST.MF").decode("utf-8", "replace")
            names = [n for n in z.namelist() if n.endswith(".class")]
            apis = set()
            for n in names:
                if n.startswith("com/nokia/"):
                    apis.add("nokia-ui")
                elif n.startswith("javax/microedition/m3g/"):
                    apis.add("m3g")
                elif n.startswith("com/mascotcapsule/"):
                    apis.add("mascot")
    except Exception as exc:                      # noqa: BLE001 - report, do not crash
        return {"bad": str(exc)[:60]}, set(), 0
    attrs = {}
    for line in re.split(r"\r\n|\n|\r", m):
        if ": " in line:
            k, v = line.split(": ", 1)
            attrs.setdefault(k.strip(), v.strip())
    attrs["midlet-name"] = attrs.get("MIDlet-Name", "")
    return attrs, apis, len(names)

def one(path):
    base = os.path.basename(path)
    row = {"file": base, "size": os.path.getsize(path)}
    attrs, apis, nclass = manifest(path)
    row.update({"name": attrs.get("midlet-name", ""), "version": attrs.get("MIDlet-Version", ""),
                "profile": attrs.get("MicroEdition-Profile", ""),
                "config": attrs.get("MicroEdition-Configuration", ""),
                "apis": sorted(apis), "classes": nclass})
    keys = os.path.join(OUT, "keys.txt")
    for heap in (16, 32):
        rms = os.path.join(OUT, "rms", base.replace(".jar", "") + "-" + str(heap))
        cmd = [ROOT + "/build/nj2me", "--port", "headless", "--size", "240x320",
               "--heap", str(heap), "--fps", "30", "--run-frames", str(RUN),
               "--keys", "@" + keys, "--rms-dir", rms, "--stats", path]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            err, rc = p.stderr, p.returncode
        except subprocess.TimeoutExpired:
            row.update({"rc": 124, "heap": heap, "verdict": "timeout"})
            break
        stats = dict(re.findall(r"(\w+)=(\d+)", (re.search(r"stats:.*", err) or [""])[0])) if "stats:" in err else {}
        thrown = re.findall(r"thrown:\s*([\w/$]+)", err)
        uncaught = len(re.findall(r"uncaught:\s*java/lang/(\w+)", err))
        row.update({"rc": rc, "heap": heap,
                    "frames": int(stats.get("frames", 0)), "paints": int(stats.get("paints", 0)),
                    "bytecodes": int(stats.get("bytecodes", 0)),
                    "tris": int(stats.get("tris", 0)), "m3g": int(stats.get("m3g", 0)),
                    "thrown": len(thrown), "uncaught": uncaught,
                    "exceptions": ",".join(sorted(set(thrown))[:4])})
        if rc == 0 and row["frames"] >= RUN // 2:
            row["verdict"] = "ok" if uncaught == 0 else "title-exception"
            break
        if "not a usable MIDlet suite" in err or "bad JAR" in err:
            row.update({"verdict": "bad-jar"})
            break
        if heap == 32 or "OutOfMemoryError" not in err:
            row["verdict"] = ("oom" if "OutOfMemoryError" in err else "failure")
            break
        row["verdict"] = "retry-heap32"
    if row.get("verdict") in ("failure", "timeout"):
        # A title that reads Nokia device properties cannot start on the generic
        # handset; retry once with a real profile so the row measures the title
        # instead of the recipe.
        cmd = [ROOT + "/build/nj2me", "--port", "headless", "--size", "240x320",
               "--heap", "32", "--device", "nokia6230", "--fps", "30",
               "--run-frames", str(RUN), "--keys", "@" + keys,
               "--rms-dir", os.path.join(OUT, "rms", base.replace(".jar", "") + "-dev"),
               "--stats", path]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            stats = dict(re.findall(r"(\w+)=(\d+)", (re.search(r"stats:.*", p.stderr) or [""])[0])) if "stats:" in p.stderr else {}
            frames = int(stats.get("frames", 0))
            if p.returncode == 0 and frames >= RUN // 2:
                row.update({"device": "nokia6230", "frames": frames,
                            "paints": int(stats.get("paints", 0)),
                            "bytecodes": int(stats.get("bytecodes", 0)),
                            "uncaught": 0, "verdict": "ok",
                            "note": "needs --device nokia6230"})
        except subprocess.TimeoutExpired:
            pass
    return row

def main():
    jobs = sorted(p for p in (ROOT + "/tests/games/" + f for f in os.listdir(ROOT + "/tests/games")) if p.endswith(".jar"))
    seen = set()
    rows_path = os.path.join(OUT, "rows.jsonl")
    if os.path.exists(rows_path):
        with open(rows_path, encoding="utf-8") as fh:
            for line in fh:
                try:
                    seen.add(json.loads(line)["file"])
                except Exception:                 # noqa: BLE001 - skip a torn line
                    pass
    jobs = [p for p in jobs if os.path.basename(p) not in seen]
    os.makedirs(os.path.join(OUT, "rms"), exist_ok=True)
    with open(os.path.join(OUT, "keys.txt"), "w") as fh:
        fh.write(",".join(KEYS) + "\n")
    done = 0
    with open(rows_path, "a") as fh, ProcessPoolExecutor(max_workers=10) as ex:
        for row in ex.map(one, jobs):
            fh.write(json.dumps(row, ensure_ascii=False) + "\n")
            fh.flush()
            done += 1
            if done % 25 == 0:
                print("done", done, flush=True)
    print("ALL DONE", len(jobs), flush=True)

if __name__ == "__main__":
    main()
