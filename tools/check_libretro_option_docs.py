#!/usr/bin/env python3
"""Check that every libretro core option is documented on the upstream page.

The libretro/docs page (docs/libretro/upstream/nano_j2me.md, shipped to
upstream as docs/library/nano_j2me.md) lists the core options in a table.
A new option that is registered in the core but missing from the page is a
submission-quality problem: frontends show the page as the manual, and
reviewers notice an option whose values/descriptions are undocumented.

This gate extracts the option keys from `lr_options[]` in the libretro port
and checks that each key appears on the page.  It is deliberately narrow:
it does not judge wording, only that the key is present (the table row
carries the key in backticks).

Usage:
    tools/check_libretro_option_docs.py [--port PATH] [--page PATH]

Exit codes: 0 all documented, 1 missing keys, 77 skip (files absent).
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "src" / "port" / "libretro" / "nj_port_libretro.c"
PAGE = ROOT / "docs" / "libretro" / "upstream" / "nano_j2me.md"

OPTION_KEY = re.compile(r'^\s*\{ "(nano_j2me_[a-z0-9_]+)"')


def registered_keys(port: Path):
    """Option keys from the lr_options[] initialiser, in definition order."""
    text = port.read_text(encoding="utf-8", errors="replace")
    start = text.find("static const lr_option_meta lr_options[] = {")
    if start < 0:
        raise SystemExit(f"{port}: lr_options[] initialiser not found")
    end = text.find("\n};", start)
    if end < 0:
        raise SystemExit(f"{port}: lr_options[] initialiser is not terminated")
    keys = []
    for line in text[start:end].splitlines():
        match = OPTION_KEY.match(line)
        if match:
            key = match.group(1)
            if key not in keys:
                keys.append(key)
    if not keys:
        raise SystemExit(f"{port}: no option keys parsed from lr_options[]")
    return keys


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=str(PORT))
    parser.add_argument("--page", default=str(PAGE))
    args = parser.parse_args(argv)

    port = Path(args.port)
    page = Path(args.page)
    if not port.is_file() or not page.is_file():
        print("SKIP: libretro port or upstream docs page is absent")
        return 77

    keys = registered_keys(port)
    text = page.read_text(encoding="utf-8", errors="replace")

    missing = [key for key in keys if f"`{key}`" not in text]
    if missing:
        print(f"FAIL: {len(missing)} option(s) registered in the core but not "
              f"documented on {page.relative_to(ROOT)}:")
        for key in missing:
            print(f"  missing {key}")
        return 1

    print(f"ok   all {len(keys)} core options are documented on the upstream page")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
