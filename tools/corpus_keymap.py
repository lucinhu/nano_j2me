"""Per-title key scripts shared by the corpus probes.

`tools/corpus_keys/` holds one script per title that needs input different from
the generic recipe (see its README for the naming rule and the curation rules).
`NJ_KEYS_DIR` points at such a directory; the probes then use
`<dir>/<jar name without .jar>.txt` when it exists and the generic script
otherwise.
"""
import os


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
