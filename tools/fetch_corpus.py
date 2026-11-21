"""Fetch MIDlet suites from archive.org collections for the compatibility sweep.

The corpus itself is not kept in the repository (size, provenance), so this is
the reproducible way to build one: it downloads every .zip/.jar in the named
archive.org items, unpacks the JARs inside, drops duplicates of suites already
on disk (by SHA-256), tiny files that are not suites, and writes the rest into
a cache directory.  Point the sweep at that directory with NJ_SWEEP_DIRS.

    python3 tools/fetch_corpus.py [output-dir] [collection ...]

When no output directory is given the corpus goes to a persistent cache so a
later sweep does not need to download anything again: $NJ_CORPUS_CACHE when
set, otherwise $XDG_CACHE_HOME/nano_j2me/corpus (or ~/.cache/nano_j2me/corpus
when XDG_CACHE_HOME is unset).  The cache is never committed; resume/interrupt
safety comes from the content-hash duplicate check.

Default collections are the ones already used for the published matrix.
"""
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE_ENV = "NJ_CORPUS_CACHE"
KNOWN = [os.path.join(ROOT, "tests", "games"),
         "/tmp/vault/Roms", "/tmp/vault2"]
DEFAULT_ITEMS = ["NokiaJavaGames", "J2ME-games", "the-promise-funy",
                 "my-top-95-mobile-java-games"]
MIN_BYTES = 20000


def default_cache_dir():
    """Persistent location for fetched suites (never committed, reused by sweeps)."""
    override = os.environ.get(CACHE_ENV, "").strip()
    if override:
        return os.path.abspath(os.path.expanduser(override))
    base = os.environ.get("XDG_CACHE_HOME", "").strip()
    if not base:
        base = os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "nano_j2me", "corpus")


def known_roots():
    """Directories whose JARs count as already-known (dedupe without downloading)."""
    roots = list(KNOWN)
    cache = default_cache_dir()
    if cache not in roots:
        roots.append(cache)
    return roots


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
    for root in known_roots():
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
        print("usage: tools/fetch_corpus.py [-d DIR] [ARCHIVE_ITEM ...]\n"
              "  downloads JARs/zips for the named items (default: built-in list)\n"
              "  -d/--dest DIR selects the output dir (a leading path-looking arg also does);\n"
              "  the default is $NJ_CORPUS_CACHE or %s" % default_cache_dir())
        return
    dest = None
    items = []
    args = list(argv[1:])
    while args:
        arg = args.pop(0)
        if arg in ("-d", "--dest"):
            if not args:
                print("fetch_corpus: --dest needs a directory")
                return 2
            dest = args.pop(0)
        elif arg.startswith("-"):
            print("fetch_corpus: unknown option", arg)
            return 2
        elif dest is None and not os.path.isabs(arg) and not arg.startswith((".", "~")) \
                and "/" not in arg:
            # A bare word is an archive.org item, not an output directory: the
            # persistent cache is the default, so item names stay unambiguous.
            items.append(arg)
        elif dest is None:
            dest = arg
        else:
            items.append(arg)
    out = dest if dest is not None else default_cache_dir()
    items = items or DEFAULT_ITEMS
    out = os.path.abspath(os.path.expanduser(out))
    os.makedirs(out, exist_ok=True)
    seen = known_hashes()
    print("output", out, flush=True)
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
