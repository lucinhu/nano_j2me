#!/usr/bin/env python3
"""One row per corpus title: manifest facts plus one paced headless run.

Usage: tools/sweep_compat.py [output directory]

Writes rows.jsonl there, one JSON object per JAR; tools/make_compat_matrix.py
turns that into docs/COMPATIBILITY_MATRIX.md.  Titles that cannot start on the
generic handset are retried once with --device nokia6230.

Set NJ_KEYS_DIR to a directory of per-title key scripts: a file named
"<jar name without .jar>.txt" replaces the generic keys.txt for that title
(see tools/corpus_keys/README.md for the format and the curation rules).
"""
import json, os, re, subprocess, sys, zipfile
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.sys.argv[1] if len(os.sys.argv) > 1 else os.path.join(
    ROOT, ".local-tools", "debug-runs", "compat-matrix-20261025")
# build/nj2me can be stale relative to HEAD (it was: the 2026-11 sweep used a
# binary 20 commits behind); NJ_BIN lets the caller point at a fresh build.
BIN = os.environ.get("NJ_BIN") or os.path.join(ROOT, "build", "nj2me")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus_keymap import resolve as resolve_keys  # noqa: E402
RUN = 600
# Generic input recipe.  No pointer taps: several titles treat an arbitrary
# tap on the canvas as "select the button under the finger", and a tap at a
# soft-key/exit coordinate made 16 titles quit early in the round-71 probe
# (see docs/notes/2026-11-16-generic-input-recipe.md).  Titles that need a
# tap or a title-specific key get a script in tools/corpus_keys/ instead.
KEYS = ["30:fire", "55:soft1", "80:53", "140:left", "165:right", "190:48",
        "215:up", "240:down", "290:5", "320:fire", "375:star", "400:6",
        "430:fire", "470:soft1", "545:fire", "575:left"]

def jar_integrity(path):
    """Pre-flight archive check.

    A JAR whose compressed entries fail to inflate (or that is not a ZIP at
    all) cannot be loaded by any runtime: classify it `bad-jar` instead of
    blaming compatibility for a later NoClassDefFoundError.  DiamondTwister
    from the fetched corpus is the case that motivated this: the archived file
    is byte-identical to archive.org's md5, and unzip itself reports
    "invalid compressed data to inflate e.class".
    """
    try:
        with zipfile.ZipFile(path) as z:
            try:
                bad = z.testzip()
            except Exception as exc:      # zlib.error, CRC checks, ...
                return False, "archive entry is corrupt (%s)" % exc
            if bad is not None:
                return False, "archive entry %s is corrupt (inflate/CRC)" % bad
    except zipfile.BadZipFile:
        return False, "not a zip file"
    except OSError as exc:
        return False, "archive cannot be read: %s" % exc
    return True, ""


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

def one(job):
    path, base = job
    row = {"file": base, "size": os.path.getsize(path),
           "jar_path": os.path.abspath(path)}
    attrs, apis, nclass = manifest(path)
    row.update({"name": attrs.get("midlet-name", ""), "version": attrs.get("MIDlet-Version", ""),
                "profile": attrs.get("MicroEdition-Profile", ""),
                "config": attrs.get("MicroEdition-Configuration", ""),
                "apis": sorted(apis), "classes": nclass})
    ok, why = jar_integrity(path)
    if not ok:
        row.update({"verdict": "bad-jar", "rc": 2, "heap": 16,
                    "frames": 0, "paints": 0, "note": why})
        return row
    keys = resolve_keys(base, os.path.join(OUT, "keys.txt"))
    if os.path.basename(os.path.dirname(keys)) == "corpus_keys":
        row["keys"] = os.path.basename(keys)
    for heap in (16, 32):
        rms = os.path.join(OUT, "rms", base.replace(".jar", "") + "-" + str(heap))
        cmd = [BIN, "--port", "headless", "--size", "240x320",
               "--heap", str(heap), "--fps", "30", "--run-frames", str(RUN),
               "--keys", "@" + keys, "--rms-dir", rms, "--stats", path]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            err, rc = p.stderr, p.returncode
        except subprocess.TimeoutExpired:
            row.update({"rc": 124, "heap": heap, "verdict": "timeout"})
            break
        stats = dict(re.findall(r"(\w+)=(\d+)", (re.search(r"stats:.*", err) or [""])[0])) if "stats:" in err else {}
        if "the MIDlet called notifyDestroyed()" in err:
            # The title asked to exit; whether it painted first is only known
            # from the stats below, so do not claim anything yet.
            row["note"] = "exited itself (notifyDestroyed)"
        elif "the MIDlet installed no Displayable" in err:
            row["note"] = "installed no Displayable"
        elif "not a usable MIDlet suite" in err:
            row["note"] = "not a usable MIDlet suite (bad JAR or no MIDlet-1)"
        thrown = re.findall(r"thrown:\s*([\w/$]+)", err)
        uncaught = len(re.findall(r"uncaught exception in thread \d+:", err))
        row.update({"rc": rc, "heap": heap,
                    "frames": int(stats.get("frames", 0)), "paints": int(stats.get("paints", 0)),
                    "bytecodes": int(stats.get("bytecodes", 0)),
                    "gfx": int(stats.get("gfx", 0)),
                    "natives": int(stats.get("natives", 0)),
                    "tris": int(stats.get("tris", 0)), "m3g": int(stats.get("m3g", 0)),
                    "thrown": len(thrown), "uncaught": uncaught,
                    "exceptions": ",".join(sorted(set(thrown))[:4])})
        if rc == 0 and row["frames"] >= RUN // 2:
            if uncaught == 0:
                row["verdict"] = "ok"
                if row["gfx"] == 0 and row["paints"] > 0:
                    # Frames were presented but the title never issued a
                    # draw call: the screen stays whatever canvas init left
                    # there.  Not a runtime crash, but worth flagging instead
                    # of letting "ok" hide a blank title (rounds 135/136 found
                    # three such fetched titles by visual probing).
                    row["note"] = ("painted %d frames but made no draw calls"
                                   " (blank canvas)" % row["paints"])
                break
            if heap == 16 and "OutOfMemoryError" in err:
                # The thread that died was the title exhausting the heap, not a
                # runtime defect: retry with 32 MB like the boot-OOM path.
                row["verdict"] = "retry-heap32"
                continue
            row["verdict"] = "title-exception"
            m = re.search(r"uncaught exception in thread \d+: ([\w./$]+) @ (\S+)", err)
            row["note"] = ("game code threw %s in a thread while the MIDlet kept painting"
                           % (m.group(1) if m else "an exception"))
            break
        if "not a usable MIDlet suite" in err or "bad JAR" in err:
            row.update({"verdict": "bad-jar"})
            break
        if heap == 32 or "OutOfMemoryError" not in err:
            if rc == 0 and uncaught == 0 and "OutOfMemoryError" not in err:
                # It ran and exited on its own.  A title that painted first is
                # a title that worked for as long as it wanted to run (March of
                # Heroes paints 105 frames and then quits); only one that never
                # painted gets "no-frame".  Keeping them apart matters because
                # "never painted" is the label that sends us looking for a bug.
                if row["frames"] > 0 or row["paints"] > 0:
                    row["verdict"] = "title-exit"
                    row["note"] = ("exited itself (notifyDestroyed) after %d frames / %d paints"
                                   % (row["frames"], row["paints"]))
                else:
                    row["verdict"] = "no-frame"
                    row["note"] = ("exited itself (notifyDestroyed) without painting a frame")
            else:
                row["verdict"] = ("oom" if "OutOfMemoryError" in err else "failure")
            break
        row["verdict"] = "retry-heap32"
    if row.get("verdict") == "title-exit":
        # The generic key script can *cause* a self-exit: a title that maps
        # fire/soft1 to the "exit" item on its first screen quits at frame 30
        # even though a player would never press it there.  Round 122 found 13
        # of 14 title-exit rows run to the full budget when no input arrives,
        # so retry once without keys before calling a title self-exiting.
        none_path = os.path.join(OUT, "keys-none.txt")
        if not os.path.exists(none_path):
            with open(none_path, "w", encoding="utf-8") as fh:
                fh.write("# deliberately empty: run without input\n")
        cmd = [BIN, "--port", "headless", "--size", "240x320",
               "--heap", str(row.get("heap", 16)), "--fps", "30",
               "--run-frames", str(RUN), "--keys", "@" + none_path,
               "--rms-dir", os.path.join(OUT, "rms", base.replace(".jar", "") + "-nokeys"),
               "--stats", path]
        if row.get("device"):
            cmd[cmd.index("--stats"):cmd.index("--stats")] = ["--device", row["device"]]
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            m = re.search(r"stats:.*", p.stderr)
            stats = dict(re.findall(r"(\w+)=(\d+)", m[0])) if m else {}
            frames = int(stats.get("frames", 0))
            uncaught = len(re.findall(r"uncaught exception in thread \d+:", p.stderr))
            if p.returncode == 0 and frames >= RUN // 2 and uncaught == 0:
                row.update({"keys": "none", "frames": frames,
                            "paints": int(stats.get("paints", 0)),
                            "bytecodes": int(stats.get("bytecodes", 0)),
                            "thrown": 0, "uncaught": 0, "verdict": "ok",
                            "note": "runs without input; the generic key script made it exit"})
                return row
            row["note"] = (row.get("note", "") +
                           "; also exits without input").strip("; ")
        except subprocess.TimeoutExpired:
            pass
    if row.get("verdict") in ("failure", "timeout"):
        # A title that reads Nokia device properties cannot start on the generic
        # handset; retry once with a real profile so the row measures the title
        # instead of the recipe.
        cmd = [BIN, "--port", "headless", "--size", "240x320",
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
    dirs = [(ROOT + "/tests/games", "")]
    extra = os.environ.get("NJ_SWEEP_DIRS", "")
    for d in [x for x in extra.split(":") if x]:
        dirs.append((d, "vault/"))
    jobs = []
    for d, prefix in dirs:
        if not os.path.isdir(d):
            continue
        jobs += [(os.path.join(d, f), prefix + f)
                 for f in os.listdir(d) if f.endswith(".jar")]
    jobs.sort(key=lambda item: item[1])
    seen = set()
    rows_path = os.path.join(OUT, "rows.jsonl")
    if os.path.exists(rows_path):
        with open(rows_path, encoding="utf-8") as fh:
            for line in fh:
                try:
                    seen.add(json.loads(line)["file"])
                except Exception:                 # noqa: BLE001 - skip a torn line
                    pass
    jobs = [j for j in jobs if j[1] not in seen]
    os.makedirs(os.path.join(OUT, "rms"), exist_ok=True)
    with open(os.path.join(OUT, "keys.txt"), "w") as fh:
        fh.write("\n".join(KEYS) + "\n")
    done = 0
    with open(rows_path, "a") as fh, ProcessPoolExecutor(max_workers=10) as ex:
        for row in ex.map(one, jobs):
            fh.write(json.dumps(row, ensure_ascii=False) + "\n")
            fh.flush()
            done += 1
            if done % 25 == 0:
                print("done", done, flush=True)
    try:
        head = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
    except OSError:
        head = ""
    with open(os.path.join(OUT, "meta.json"), "w", encoding="utf-8") as fh:
        json.dump({"commit": head or "unknown",
                   "date": __import__("time").strftime("%Y-%m-%d")}, fh)
        fh.write("\n")
    print("ALL DONE", len(jobs), flush=True)

if __name__ == "__main__":
    main()
