"""Fetch MIDlet suites from archive.org collections for the compatibility sweep.

The corpus itself is not kept in the repository (size, provenance), so this is
the reproducible way to build one: it downloads every .zip in the named
archive.org items, unpacks the JARs inside, drops duplicates of suites already
on disk (by SHA-256), tiny files that are not suites, and writes the rest into
an output directory.  Point the sweep at that directory with NJ_SWEEP_DIRS.

    python3 tools/fetch_corpus.py /tmp/vault3 [collection ...]

Default collections are the ones already used for the published matrix; a run
can be interrupted and resumed because duplicates are skipped by content hash.
"""
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOWN = [os.path.join(ROOT, "tests", "games"), "/tmp/vault/Roms", "/tmp/vault2"]
DEFAULT_ITEMS = ["NokiaJavaGames", "J2ME-games", "the-promise-funy",
                 "my-top-95-mobile-java-games"]
MIN_BYTES = 20000


def fetch_bytes(url, attempts=3):
    """Download one URL, retrying transient errors; curl is the fallback.

    archive.org answers some TLS handshakes with a plain EOF from Python's
    ssl module while curl succeeds against the same URL, so when urllib keeps
    failing and curl is available, read the body through curl instead.
    """
    import shutil
    import subprocess
    import tempfile
    import time
    last = None
    for attempt in range(attempts):
        try:
            return urllib.request.urlopen(url, timeout=120).read()
        except Exception as exc:                     # retry, then fall back
            last = exc
            if attempt + 1 < attempts:
                time.sleep(1 + 2 * attempt)
    if shutil.which("curl"):
        fd, path = tempfile.mkstemp(suffix=".download")
        os.close(fd)
        try:
            rc = subprocess.run(
                ["curl", "-sL", "--fail", "--retry", "3", "-m", "600",
                 "-o", path, url],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
            if rc == 0:
                with open(path, "rb") as fh:
                    return fh.read()
        finally:
            try:
                os.remove(path)
            except OSError:
                pass
    raise last


def already_saved(out, name):
    """True when a file of that basename is already in the output directory."""
    base = os.path.basename(name).replace("/", "_")
    path = os.path.join(out, base)
    try:
        return os.path.getsize(path) >= MIN_BYTES
    except OSError:
        return False


def known_hashes():
    seen = set()
    for root in KNOWN:
        if not os.path.isdir(root):
            continue
        for name in os.listdir(root):
            if not name.lower().endswith(".jar"):
                continue
            try:
                with open(os.path.join(root, name), "rb") as fh:
                    seen.add(hashlib.sha256(fh.read()).hexdigest())
            except OSError:
                pass
    return seen


def main(argv):
    if len(argv) > 1 and argv[1] in ("-h", "--help"):
        print("usage: tools/fetch_corpus.py OUT_DIR [ARCHIVE_ITEM ...]\n"
              "  downloads loose JARs/zips for the named items (default: built-in list)")
        return
    out = argv[1] if len(argv) > 1 else "/tmp/vault3"
    if any(a.startswith("-") for a in argv[1:]):
        print("fetch_corpus: option-like argument in OUT_DIR/items; usage: "
              "tools/fetch_corpus.py OUT_DIR [ARCHIVE_ITEM ...]")
        return 2
    items = argv[2:] or DEFAULT_ITEMS
    os.makedirs(out, exist_ok=True)
    seen = known_hashes()
    print("known", len(seen), flush=True)
    added = 0
    for ident in items:
        try:
            meta = json.loads(fetch_bytes(
                "https://archive.org/metadata/" + ident).decode("utf-8"))
        except Exception as exc:                     # network: report and go on
            print("skip collection", ident, exc, flush=True)
            continue
        for entry in meta.get("files", []):
            name = entry.get("name", "")
            if name.lower().endswith(".jar"):
                if already_saved(out, name):
                    continue
                url = "https://archive.org/download/%s/%s" % (
                    ident, urllib.parse.quote(name))
                try:
                    blob = fetch_bytes(url)
                except Exception as exc:             # one bad file is not fatal
                    print("skip", name, exc, flush=True)
                    continue
                if len(blob) < MIN_BYTES:
                    continue
                digest = hashlib.sha256(blob).hexdigest()
                if digest in seen:
                    continue
                seen.add(digest)
                base = os.path.basename(name).replace("/", "_")
                with open(os.path.join(out, base), "wb") as fh:
                    fh.write(blob)
                added += 1
                continue
            if not name.lower().endswith(".zip"):
                if name.lower().endswith((".7z", ".rar")):
                    print("skip (unsupported archive)", name, flush=True)
                continue
            url = "https://archive.org/download/%s/%s" % (
                ident, urllib.parse.quote(name))
            try:
                data = fetch_bytes(url)
            except Exception as exc:                 # one bad zip is not fatal
                print("skip", name, exc, flush=True)
                continue
            tmp = os.path.join(out, ".download.zip")
            with open(tmp, "wb") as fh:
                fh.write(data)
            try:
                archive = zipfile.ZipFile(tmp)
            except Exception:
                print("skip (not a zip)", name, flush=True)
                continue
            for member in archive.namelist():
                if not member.lower().endswith(".jar"):
                    continue
                if already_saved(out, member):
                    continue
                try:
                    blob = archive.read(member)
                except Exception:
                    continue
                if len(blob) < MIN_BYTES:
                    continue
                digest = hashlib.sha256(blob).hexdigest()
                if digest in seen:
                    continue
                seen.add(digest)
                base = os.path.basename(member).replace("/", "_")
                with open(os.path.join(out, base), "wb") as fh:
                    fh.write(blob)
                added += 1
            os.remove(tmp)
    print("added", added, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
