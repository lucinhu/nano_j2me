#!/usr/bin/env python3
"""Verify the web shell renders in both colour schemes.

Serves build/web/ locally, opens index.html with Playwright/Firefox in dark and
light `color_scheme`, checks computed colours and console health, and writes a
pair of screenshots for the evidence trail. Exit codes: 0 pass, 1 failure,
2 missing build or Playwright.
"""
import argparse
import functools
import http.server
import os
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build", "web")
EVIDENCE = os.path.join(ROOT, ".local-tools", "debug-runs", "web-theme-check")
SHOTS = os.path.join(EVIDENCE, "shots")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=BUILD)
    ap.add_argument("--out", default=SHOTS)
    ap.add_argument("--timeout", type=int, default=30000)
    args = ap.parse_args()
    if not os.path.isfile(os.path.join(args.root, "index.html")):
        print(f"check_web_theme: missing {args.root}/index.html; build the web shell first")
        return 2
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("check_web_theme: playwright is not installed")
        return 2

    os.makedirs(args.out, exist_ok=True)
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=args.root)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    failures = 0
    try:
        with sync_playwright() as p:
            browser = p.firefox.launch()
            seen = {}
            for scheme in ("dark", "light"):
                ctx = browser.new_context(
                    viewport={"width": 1000, "height": 700}, color_scheme=scheme)
                page = ctx.new_page()
                errors = []
                page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
                page.on("pageerror", lambda e: errors.append(str(e)))
                page.goto(f"http://127.0.0.1:{port}/index.html", wait_until="load",
                          timeout=args.timeout)
                page.wait_for_timeout(1200)
                bg = page.evaluate("getComputedStyle(document.body).backgroundColor")
                cs = page.evaluate("getComputedStyle(document.documentElement).colorScheme")
                shot = os.path.join(args.out, f"{scheme}.png")
                page.screenshot(path=shot)
                seen[scheme] = (bg, cs, errors)
                print(f"{scheme}: background={bg} color-scheme={cs} console-errors={len(errors)} -> {shot}")
                ctx.close()
            browser.close()
    finally:
        srv.shutdown()
    if seen["dark"][0] == seen["light"][0]:
        print("FAIL: light scheme did not change the background colour")
        failures += 1
    for scheme in ("dark", "light"):
        if seen[scheme][1] != scheme:
            print(f"FAIL: color-scheme is {seen[scheme][1]!r} for {scheme}")
            failures += 1
        if seen[scheme][2]:
            print(f"FAIL: {scheme} console errors: {seen[scheme][2][:3]}")
            failures += 1
    print(f"check_web_theme: {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
