"""Per-title key scripts and screen sizes shared by the corpus probes.

`tools/corpus_keys/` holds one script per title that needs input different from
the generic recipe (see its README for the naming rule and the curation rules).
`NJ_KEYS_DIR` points at such a directory; the probes then use
`<dir>/<jar name without .jar>.txt` when it exists and the generic script
otherwise.

`tools/corpus_screens/` holds one text file per title that must be measured at
a canvas size other than the 240x320 default (the file contains exactly one
of the `--size` values, e.g. `320x240`).  A title whose JAR name carries a
`_<w>x<h>` device suffix (the common `_320x240` retail build marker) is also
measured at that size, so a portrait probe does not report a landscape build
black.  `NJ_SCREENS_DIR` overrides the explicit-file directory.
"""
import os
import re


def keys_dir(explicit=None):
    if explicit:
        return explicit
    return os.environ.get("NJ_KEYS_DIR", "")


def resolve(jar_name, default=None, directory=None):
    """Return the key script for `jar_name`, or `default` when there is none.

    `jar_name` may be a bare file name or a path; only its base name is used.
    """
    directory = keys_dir(directory)
    if directory:
        stem = os.path.basename(str(jar_name))
        if stem.endswith(".jar"):
            stem = stem[:-len(".jar")]
        candidate = os.path.join(directory, stem + ".txt")
        if os.path.exists(candidate):
            return candidate
    return default

SCREENS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "corpus_screens")


def screens_dir(explicit=None):
    if explicit:
        return explicit
    return os.environ.get("NJ_SCREENS_DIR", "") or SCREENS_DIR


def screen(jar_name, default=None, directory=None):
    """Return the canvas size for `jar_name`, or `default` when it is the default.

    Explicit `tools/corpus_screens/<stem>.txt` wins; otherwise a `_<w>x<h>`
    token in the base name selects that size.  Only known CLI/core sizes are
    accepted, so a stray token cannot break a probe invocation.
    """
    known = ("240x320", "320x240", "176x208", "128x160")
    stem = os.path.basename(str(jar_name))
    if stem.endswith(".jar"):
        stem = stem[:-len(".jar")]
    directory = screens_dir(directory)
    if directory:
        candidate = os.path.join(directory, stem + ".txt")
        if os.path.exists(candidate):
            with open(candidate, "r", encoding="utf-8") as fh:
                value = fh.read().strip()
            if value in known:
                return value
    m = re.search(r"_(\d{3})x(\d{3})(?:$|[^0-9])", stem)
    if m:
        value = "%sx%s" % (m.group(1), m.group(2))
        if value in known:
            return value
    return default
