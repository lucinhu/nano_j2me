#!/usr/bin/env python3
"""Live-demo check for the nano_j2me web shell.

Opens a *deployed* copy of the browser port (by default the GitHub Pages demo
at https://lucinhu.github.io/nano_j2me/) in a real headless Firefox and walks
the same path a visitor takes:

  1. the site and its esm module load, the wasm engine boots ('kind' == 'wasm'),
  2. the manifest is served and parses,
  3. when a JAR is available, it is imported through the file input, the tile
     appears, the game starts, the canvas is painted and frames advance,
  4. no page or console errors happen along the way.

This is deliberately *not* part of `tools/check_web.sh --all`: it needs network
access, a deployed site and Python Playwright (the Node harness may run
without Playwright in CI).  Run it by hand after a deployment, or from
check_web.sh with --live [URL].

Requires: python3 -m playwright (with the Firefox browser installed), or a
system playwright.  The module is resolved from the normal Python import path.

Exit codes: 0 pass, 1 failure, 2 usage/missing dependency, 3 site unreachable.
"""

import argparse
import datetime
import json
import os
import sys
import time

DEFAULT_URL = "https://lucinhu.github.io/nano_j2me/"
SCREEN_W, SCREEN_H = 240, 320


def now_stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def default_evidence():
    return os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        ".local-tools",
        "debug-runs",
        "web-live-" + now_stamp(),
    )


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def read_json(url, timeout=30):
    import urllib.request

    req = urllib.request.Request(url, headers={"User-Agent": "nano_j2me-live-check"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.status, resp.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--jar", default=None, help="JAR to import (skipped when absent)")
    ap.add_argument("--evidence", default=None)
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args()

    evidence = os.path.abspath(args.evidence or default_evidence())
    os.makedirs(evidence, exist_ok=True)
    url = args.url.rstrip("/") + "/"
    jar = args.jar or os.path.join(repo_root(), "tests", "games", "hello.jar")
    if jar and not os.path.isfile(jar):
        jar = None

    checks = []

    def check(name, ok, detail=""):
        checks.append({"name": name, "ok": bool(ok), "detail": str(detail)})
        print(("PASS " if ok else "FAIL ") + name + (" — " + str(detail) if detail else ""))
        return bool(ok)

    # Pre-flight: never make Playwright explain a DNS problem.
    try:
        status, index_bytes = read_json(url + "index.html")
    except Exception as exc:  # noqa: BLE001 - network errors are the diagnosis
        print("FAIL site unreachable: %s (%s)" % (url, exc))
        return 3
    check("index.html is served", status == 200 and len(index_bytes) > 1000,
          "HTTP %s, %d bytes" % (status, len(index_bytes)))

    try:
        from playwright.sync_api import sync_playwright  # noqa: PLC0415
    except Exception as exc:  # noqa: BLE001
        print("SKIP browser checks: python playwright is not installed (%s)" % exc)
        print("     python3 -m pip install playwright && python3 -m playwright install firefox")
        json.dump({"url": url, "ok": False, "checks": checks, "skipped": "playwright"},
                  open(os.path.join(evidence, "live-check.json"), "w"),
                  ensure_ascii=False, indent=2)
        return 2

    errors = []
    started = time.time()
    with sync_playwright() as pw:
        browser = pw.firefox.launch()
        ctx = browser.new_context(viewport={"width": 1100, "height": 860}, locale="zh-CN")
        page = ctx.new_page()
        page.on("pageerror", lambda e: errors.append("pageerror: " + str(e)))
        page.on(
            "console",
            lambda m: errors.append("console.error: " + m.text) if m.type == "error" else None,
        )
        try:
            page.goto(url, wait_until="load", timeout=args.timeout * 1000)
            page.wait_for_function(
                "() => window.__nj2me && window.__nj2me.ready === true",
                timeout=args.timeout * 1000,
            )
            kind = page.evaluate("() => window.__nj2me.runtime && window.__nj2me.runtime.kind")
            check("shell boots and loads the wasm engine", kind == "wasm", "engine kind=%s" % kind)

            manifest_ok = page.evaluate(
                """async () => {
                    const link = document.querySelector('link[rel="manifest"]');
                    if (!link) { return 'no manifest link'; }
                    const resp = await fetch(link.href, { cache: 'no-store' });
                    if (!resp.ok) { return 'manifest HTTP ' + resp.status; }
                    const data = await resp.json();
                    return data && data.name ? 'ok' : 'manifest has no name';
                }"""
            )
            check("web app manifest is served and parses", manifest_ok == "ok", manifest_ok)

            if jar:
                page.set_input_files("#file-input", jar)
                page.wait_for_function(
                    "() => document.querySelectorAll('.game-tile').length >= 1",
                    timeout=args.timeout * 1000,
                )
                title = page.evaluate(
                    "() => (document.querySelector('.game-tile__title') || {}).textContent || ''"
                )
                check("importing a JAR adds a library tile", bool(title.strip()),
                      "tile title=%r" % title)
                page.locator(".game-tile").first.click()
                page.wait_for_selector("#game-view:not([hidden])", timeout=60000)
                page.wait_for_function(
                    "() => window.__nj2me.game && window.__nj2me.game.game",
                    timeout=args.timeout * 1000,
                )
                time.sleep(1.2)
                first = page.evaluate(_FRAME_STATE_JS)
                time.sleep(1.0)
                second = page.evaluate(_FRAME_STATE_JS)
                check("game canvas is 240x320 and painted",
                      first.get("width") == SCREEN_W
                      and first.get("height") == SCREEN_H
                      and first.get("nonBlack", 0) > 1000,
                      "canvas %sx%s, nonBlack=%s"
                      % (first.get("width"), first.get("height"), first.get("nonBlack")))
                check("frames advance while the game runs",
                      second.get("frames", 0) > first.get("frames", 0),
                      "frames %s -> %s" % (first.get("frames"), second.get("frames")))
                page.screenshot(path=os.path.join(evidence, "live-game.png"))
            else:
                print("SKIP game checks: no JAR available (tests/games/ is git-ignored)")
                page.screenshot(path=os.path.join(evidence, "live-library.png"))

            check("no page or console errors", len(errors) == 0, "; ".join(errors[:3]))
        finally:
            try:
                ctx.close()
            except Exception:  # noqa: BLE001
                pass
            browser.close()

    ok = all(c["ok"] for c in checks)
    report = {
        "url": url,
        "jar": jar,
        "ok": ok,
        "elapsedMs": int((time.time() - started) * 1000),
        "checks": checks,
        "errors": errors,
    }
    with open(os.path.join(evidence, "live-check.json"), "w") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=2)
    with open(os.path.join(evidence, "live-check.txt"), "w") as fh:
        fh.write("live demo: %s\n" % url)
        for c in checks:
            fh.write(("PASS " if c["ok"] else "FAIL ") + c["name"] +
                     ((" — " + c["detail"]) if c["detail"] else "") + "\n")
        fh.write("\nRESULT: %s\n" % ("PASS" if ok else "FAIL"))
    print("\nRESULT: %s" % ("PASS" if ok else "FAIL"))
    print("evidence: %s" % evidence)
    return 0 if ok else 1


_FRAME_STATE_JS = """() => {
    const g = window.__nj2me.game;
    const c = document.querySelector('#game-canvas');
    let nonBlack = 0;
    try {
        const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        for (let i = 0; i < d.length; i += 4) {
            if (d[i] + d[i + 1] + d[i + 2] > 12) { nonBlack++; }
        }
    } catch (e) {
        return {width: c.width, height: c.height, nonBlack: -1, frames: -1, error: String(e)};
    }
    return {width: c.width, height: c.height, nonBlack, frames: g ? g.runtime.frames() : -1};
}"""


if __name__ == "__main__":
    sys.exit(main())
