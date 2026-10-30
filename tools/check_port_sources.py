#!/usr/bin/env python3
"""Every runtime source the desktop build compiles must be in the embedded port.

WHY THIS EXISTS.  tests/embedded/idf/components/nj2me/CMakeLists.txt writes its
source list out by hand, deliberately (a glob would hide a module that fails to
compile).  The price is drift: a new module added to CMakeLists.txt is simply
absent from the ESP image, and the failure appears as undefined references at
link time - or worse, as a class library that is silently smaller on the device.
That is exactly what happened when nj_avcodec.c, nj_mp4.c, the decoders, the
datagram connection and the PIM/Samsung modules were added: the ESP32-S3 image
stopped linking and nobody noticed until the port was rebuilt.

So the two lists are compared here and the check runs in ctest.  A source that
is genuinely desktop-only goes in DESKTOP_ONLY below with a reason; a source
that is missing from both lists is a build-breaking omission and fails.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESKTOP_CMAKE = ROOT / "CMakeLists.txt"
ESP_CMAKE = ROOT / "tests/embedded/idf/components/nj2me/CMakeLists.txt"

# Runtime sources the ESP image deliberately does not carry, with the reason.
DESKTOP_ONLY = {
    "src/runner/nj_run.c": "the command-line runner (argv, files, --device profiles)",
    "src/core/nj_save.c": "VM snapshots: the runner's --save-state/--load-state",
    "src/core/nj_detect.c": "handset/device-profile detection, used by the runner",
    "src/gfx/nj_ttf.c": "TrueType rasteriser; the device build uses the bitmap faces (NJ_ENABLE_TTF=0 there)",
    "src/gfx/nj_font_ttf.c": "the TrueType face glue for the same switch",
    "src/client/nj_library.c": "the desktop client's games library: scans a games directory, probes JARs, decodes icons; a device has no library window",
    "src/client/nj_client_config.c": "the desktop client's settings file (XDG paths, client.conf); a device has its own settings storage",
    "src/client/nj_client_ui.c": "the desktop client's launcher/browser/settings UI model; only the SDL2 client window draws it",
    "src/port/posix/nj_sdl2_client.c": "the SDL2 client's window and game loop; the device image has no desktop window or library",
}


def desktop_sources():
    text = DESKTOP_CMAKE.read_text(encoding="utf-8")
    start = text.index("set(NJ_SOURCES")
    end = text.index("\n)", start)
    return sorted(set(re.findall(r"src/[A-Za-z0-9_/]+\.c", text[start:end])))


def esp_sources():
    text = ESP_CMAKE.read_text(encoding="utf-8")
    return sorted(set(re.findall(r"\$\{NJ_ROOT\}/(src/[A-Za-z0-9_/]+\.c)", text)))


def main():
    desktop = desktop_sources()
    esp = set(esp_sources())
    problems = []
    for src in desktop:
        if src in esp:
            continue
        if src in DESKTOP_ONLY:
            continue
        problems.append(src)
    # The reverse direction matters too, for the shared runtime only: a source
    # the ESP list names that the desktop build does not is either a typo or a
    # file that no longer exists.  src/port/* is per-platform on purpose (the
    # ESP image names its own port, the desktop build does not).
    shared = ("src/core/", "src/midp/", "src/gfx/", "src/zip/", "src/runner/")
    desktop_set = set(desktop)
    for src in sorted(esp - desktop_set):
        if not src.startswith(shared):
            continue
        if not (ROOT / src).exists():
            problems.append(f"{src} (missing on disk)")
        else:
            problems.append(f"{src} (in the ESP list, not the desktop build)")

    if problems:
        print("embedded port source list is out of date:")
        for p in problems:
            print(f"  MISSING  {p}")
        print()
        print(f"add it to {ESP_CMAKE.relative_to(ROOT)} (or, for a genuinely")
        print("desktop-only unit, to DESKTOP_ONLY in this script with a reason).")
        return 1
    print(f"ok: {len(esp)} embedded sources, {len(desktop)} desktop sources, "
          f"{len(DESKTOP_ONLY)} documented desktop-only unit(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
