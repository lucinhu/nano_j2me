#!/usr/bin/env bash
#
# Verify the Emscripten/WASM browser port.
#
#   tools/check_web.sh --node --corpus      what CI and the docs run (default)
#   tools/check_web.sh --node               JS <-> C API contract test only
#   tools/check_web.sh --corpus             deterministic 42-title JAR smoke test only
#   tools/check_web.sh --browser            browser page test (Playwright Firefox)
#   tools/check_web.sh --theme              dark/light colour-scheme render check
#   tools/check_web.sh --sweep              slow full-corpus audit over tests/games/
#   tools/check_web.sh --perf               wasm throughput smoke test (4 light titles)
#   tools/check_web.sh --smoke              minimal post-build module smoke test
#   tools/check_web.sh --live               open the deployed demo and play a JAR in it
#   tools/check_web.sh --live=https://example.dev/  same, against another deployment
#   tools/check_web.sh --build --node --corpus   rebuild first, then test
#
# Suites (default when no suite flag is given: --node --corpus; --all means
# --node --corpus --browser --theme, without the slow --sweep audit):
#   --node      node tools/web_test/web_api_test.mjs
#               Header/baseline contract: API/runtime version identity, JAR
#               probing + icons, start error paths, hello.jar 240-frame
#               render baseline (240x320, all pixels non-black), 320x240 and
#               speed options, key/pointer input on game15.jar, save-state
#               round trip + truncated-state rejection, audiodemo audio, heap
#               stability.  Runs in well under a second.
#   --corpus    node tools/web_test/web_corpus_test.mjs
#               Pinned corpus (42 titles: 39 JARs from tests/games/ plus the
#               Midnight Pool regression title and the audio and bench fixtures;
#               the known-bad 1945空战英雄_6105.jar is excluded) is probed,
#               started and run 120 deterministic frames each.  ~9 s on this
#               machine; the whole node+corpus suite stays under half a minute.
#   --browser   node tools/web_test/web_browser_test.mjs
#               Real-page Playwright test of the assembled shell (42 checks):
#               library import/URL import/favourites/continue card/compat
#               badges, play/pause, save slots (naming, thumbnails, .njst
#               export/import), auto-save/auto-resume, diagnostics, gamepad,
#               one-engine-per-profile, installable PWA and offline reload.
#               Playwright (Firefox) is required; when it is not installed this
#               is a hard error.
#   --sweep     node tools/web_test/web_sweep_test.mjs
#               Full-corpus audit: every JAR under tests/games/ (minus the
#               known-bad titles) runs 90 frames in its own Node process with a
#               25 s timeout and is classified (ok/black/fatal/start/probe/
#               invalid/timeout).  The classification is compared with the
#               2026-11-15 native-parity baseline: a regression fails the run,
#               known parity cases (black title screens, heap-hungry titles,
#               constructor-loop titles) only warn.  Several minutes long and
#               deliberately not part of --all; treat it as a pre-release gate.
#               Evidence: sweep-<titles>.jsonl + sweep-summary.txt.
#   --perf      node tools/web_test/web_perf_test.mjs
#               Wasm throughput smoke test: four light/medium titles are warmed
#               up and then pump 600 frames each with a fixed 1000/60 ms
#               timestep.  Floors are deliberately conservative (a fraction of
#               the reference machine) so the suite catches order-of-magnitude
#               regressions without failing slow CI hosts; the measured numbers
#               are written to perf-summary.{txt,json}.  Well under a second,
#               and not part of --all.
#   --smoke     node tools/web_test/web_smoke_test.mjs
#               Post-build sanity without the private corpus: module load and
#               export list, runtime identity, option API + MEMFS round-trip,
#               and a 60-frame hello.jar render when tests/games/ happens to be
#               present (skipped otherwise).  This is what the GitHub Pages
#               workflow runs after tools/build_web.sh.
#   --live      python3 tools/web_test/web_live_check.py
#               Opens a *deployed* copy of the shell (default
#               https://lucinhu.github.io/nano_j2me/) in headless Firefox, boots
#               the wasm engine from it, imports hello.jar when tests/games/ is
#               present, starts it and asserts a painted 240x320 canvas with
#               advancing frames and no page/console errors.  Needs network and
#               Python Playwright (python3 -m pip install playwright &&
#               python3 -m playwright install firefox); not part of --all, run
#               it after a deployment.
#
# Options:
#   --build         run tools/build_web.sh (rebuild build/web/ + site) first;
#                   without it the existing artifacts are used.  A missing
#                   module is a hard error that tells you to --build.
#   --evidence DIR  evidence directory (default
#                   .local-tools/debug-runs/web-verify-<YYYYMMDD-HHMM>);
#                   created if needed, never committed (git-ignored by
#                   .local-tools/).  All suite output is tee'd to
#                   <dir>/check-web.log.
#   --frames N      corpus frames per title (default 120, minimum 30: the
#                   render assertions need the first frames to settle)
#   -h, --help      this help
#
# Environment: Node.js >= 18 on PATH.  Building additionally needs the
# Emscripten SDK as discovered by tools/build_web.sh (emcc on PATH, $EMSDK, or
# .local-tools/emsdk).  The browser suite additionally needs Playwright/Firefox;
# --live needs Python Playwright and network access.
#
# Exit status: 0 when every executed suite passed, 1 when a suite failed, 2 on
# usage or missing-artifact errors.
#
# This script only reads the build; it never commits and only writes under the
# evidence directory.

set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
log=""
evidence=""
run_node=0
run_corpus=0
run_browser=0
run_theme=0
run_sweep=0
run_perf=0
run_smoke=0
run_live=0
live_url=""
do_build=0
corpus_frames=120
started_at=$(date +%s)

usage() {
    sed -n '2,/^set -uo pipefail/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'
}

die_usage() {
    echo "check_web.sh: $*" >&2
    echo "run 'tools/check_web.sh --help' for usage" >&2
    exit 2
}

# ---- argument parsing -------------------------------------------------------

while [ $# -gt 0 ]; do
    arg="$1"
    case "$arg" in
    --node)    run_node=1 ;;
    --corpus)  run_corpus=1 ;;
    --browser) run_browser=1 ;;
    --theme)   run_theme=1 ;;
    --sweep)   run_sweep=1 ;;
    --perf)    run_perf=1 ;;
    --smoke)   run_smoke=1 ;;
    --live)    run_live=1 ;;
    --live=*)  run_live=1; live_url="${arg#--live=}" ;;
    --all)     run_node=1; run_corpus=1; run_browser=1; run_theme=1 ;;
    --build)   do_build=1 ;;
    --evidence)
        [ $# -ge 2 ] || die_usage "--evidence needs a directory"
        evidence="$2"; shift ;;
    --evidence=*) evidence="${arg#--evidence=}" ;;
    --frames)
        [ $# -ge 2 ] || die_usage "--frames needs a number"
        corpus_frames="$2"; shift ;;
    --frames=*) corpus_frames="${arg#--frames=}" ;;
    -h|--help) usage; exit 0 ;;
    *) die_usage "unknown argument: $arg" ;;
    esac
    shift
done

if [ "$run_node" = 0 ] && [ "$run_corpus" = 0 ] && [ "$run_browser" = 0 ] && [ "$run_sweep" = 0 ] &&
    [ "$run_perf" = 0 ] && [ "$run_smoke" = 0 ] && [ "$run_live" = 0 ] && [ "$run_theme" = 0 ]; then
    run_node=1
    run_corpus=1
fi

case "$corpus_frames" in
    ''|*[!0-9]*) die_usage "--frames must be a positive integer, got '$corpus_frames'" ;;
esac
if [ "$corpus_frames" -lt 30 ] || [ "$corpus_frames" -gt 2000 ]; then
    die_usage "--frames must be between 30 and 2000, got $corpus_frames"
fi

# ---- evidence directory -----------------------------------------------------

if [ -z "$evidence" ]; then
    evidence="$here/.local-tools/debug-runs/web-verify-$(date +%Y%m%d-%H%M)"
elif [ "${evidence#/}" = "$evidence" ]; then
    evidence="$here/$evidence"
fi
mkdir -p "$evidence" || { echo "check_web.sh: cannot create evidence dir $evidence" >&2; exit 2; }
log="$evidence/check-web.log"
: > "$log"

say() { echo "$*" | tee -a "$log"; }
say_err() { echo "$*" >&2; echo "$*" >> "$log"; }

say "== nano_j2me web verification"
say "   repository: $here"
say "   evidence:   $evidence"
say "   suites:     node=$run_node corpus=$run_corpus browser=$run_browser sweep=$run_sweep perf=$run_perf smoke=$run_smoke live=$run_live build=$do_build"

# ---- prerequisites ----------------------------------------------------------

if ! command -v node >/dev/null 2>&1; then
    say_err "check_web.sh: Node.js is not on PATH (the WASM module is tested with Node >= 18)"
    exit 2
fi
say "   node:       $(node --version)"

if [ "$do_build" = 1 ]; then
    say ""
    say "== build: tools/build_web.sh"
    "$here/tools/build_web.sh" 2>&1 | tee -a "$log"
    build_rc=${PIPESTATUS[0]}
    if [ "$build_rc" -ne 0 ]; then
        say_err "check_web.sh: build failed (exit $build_rc)"
        exit 1
    fi
fi

module_js="$here/build/web/js/nj2me.js"
module_wasm="$here/build/web/js/nj2me.wasm"
missing=""
[ -f "$module_js" ] || missing="$missing build/web/js/nj2me.js"
[ -f "$module_wasm" ] || missing="$missing build/web/js/nj2me.wasm"
if [ -n "$missing" ]; then
    say_err "check_web.sh: WASM module artifacts missing:$missing"
    say_err "  build them with tools/build_web.sh (or re-run this script with --build)"
    exit 2
fi

# ---- suite runner -----------------------------------------------------------

rc_node=0
rc_corpus=0
rc_browser=0
rc_sweep=0
rc_perf=0
rc_smoke=0
rc_live=0
rc_theme=0
status_node="SKIP"
status_corpus="SKIP"
status_browser="SKIP"
status_sweep="SKIP"
status_perf="SKIP"
status_smoke="SKIP"
status_live="SKIP"
status_theme="SKIP"

if [ "$run_node" = 1 ]; then
    say ""
    say "== suite: node API contract"
    node "$here/tools/web_test/web_api_test.mjs" --evidence "$evidence" 2>&1 | tee -a "$log"
    rc_node=${PIPESTATUS[0]}
    if [ "$rc_node" -eq 0 ]; then status_node="PASS"; else status_node="FAIL"; fi
fi

if [ "$run_corpus" = 1 ]; then
    say ""
    say "== suite: corpus smoke test (${corpus_frames} frames per title)"
    node "$here/tools/web_test/web_corpus_test.mjs" --evidence "$evidence" --frames "$corpus_frames" 2>&1 | tee -a "$log"
    rc_corpus=${PIPESTATUS[0]}
    if [ "$rc_corpus" -eq 0 ]; then status_corpus="PASS"; else status_corpus="FAIL"; fi
fi

if [ "$run_browser" = 1 ]; then
    say ""
    say "== suite: browser page test"
    browser_test="$here/tools/web_test/web_browser_test.mjs"
    if [ ! -f "$browser_test" ]; then
        say_err "check_web.sh: browser test is missing: $browser_test"
        rc_browser=1
    else
        playwright_bin="${PLAYWRIGHT_BIN:-$HOME/.local/bin/playwright}"
        if ! command -v playwright >/dev/null 2>&1 && [ ! -x "$playwright_bin" ]; then
            say_err "check_web.sh: browser test exists but Playwright is not available"
            say_err "  expected 'playwright' on PATH or $playwright_bin"
            rc_browser=1
        else
            if ! command -v playwright >/dev/null 2>&1; then
                export PLAYWRIGHT_BIN="$playwright_bin"
                export PATH="$HOME/.local/bin:$PATH"
            fi
            node "$browser_test" --evidence "$evidence" 2>&1 | tee -a "$log"
            rc_browser=${PIPESTATUS[0]}
        fi
        if [ "$rc_browser" -eq 0 ]; then status_browser="PASS"; else status_browser="FAIL"; fi
    fi
fi

if [ "$run_sweep" = 1 ]; then
    say ""
    say "== suite: full-corpus sweep audit (this takes several minutes)"
    node "$here/tools/web_test/web_sweep_test.mjs" --evidence "$evidence" 2>&1 | tee -a "$log"
    rc_sweep=${PIPESTATUS[0]}
    if [ "$rc_sweep" -eq 0 ]; then status_sweep="PASS"; else status_sweep="FAIL"; fi
fi

if [ "$run_perf" = 1 ]; then
    say ""
    say "== suite: wasm throughput smoke test"
    node "$here/tools/web_test/web_perf_test.mjs" --evidence "$evidence" 2>&1 | tee -a "$log"
    rc_perf=${PIPESTATUS[0]}
    if [ "$rc_perf" -eq 0 ]; then status_perf="PASS"; else status_perf="FAIL"; fi
fi

if [ "$run_smoke" = 1 ]; then
    say ""
    say "== suite: module smoke test"
    node "$here/tools/web_test/web_smoke_test.mjs" --evidence "$evidence" 2>&1 | tee -a "$log"
    rc_smoke=${PIPESTATUS[0]}
    if [ "$rc_smoke" -eq 0 ]; then status_smoke="PASS"; else status_smoke="FAIL"; fi
fi

if [ "$run_live" = 1 ]; then
    say ""
    say "== suite: live deployed demo${live_url:+ ($live_url)}"
    live_args=(--evidence "$evidence")
    if [ -n "$live_url" ]; then live_args+=(--url "$live_url"); fi
    python3 "$here/tools/web_test/web_live_check.py" "${live_args[@]}" 2>&1 | tee -a "$log"
    rc_live=${PIPESTATUS[0]}
    if [ "$rc_live" -eq 0 ]; then status_live="PASS"; else status_live="FAIL"; fi
fi

if [ "$run_theme" = 1 ]; then
    say ""
    say "== suite: dark/light colour scheme"
    python3 "$here/tools/check_web_theme.py" --out "$evidence/shots" 2>&1 | tee -a "$log"
    rc_theme=${PIPESTATUS[0]}
    if [ "$rc_theme" -eq 0 ]; then status_theme="PASS"; else status_theme="FAIL"; fi
fi

# ---- summary ----------------------------------------------------------------

finished_at=$(date +%s)
elapsed=$((finished_at - started_at))
say ""
say "== check_web summary"
if [ "$run_node" = 1 ]; then
    say "   node:    $status_node"
fi
if [ "$run_corpus" = 1 ]; then
    say "   corpus:  $status_corpus"
fi
if [ "$run_browser" = 1 ]; then
    say "   browser: $status_browser"
fi
if [ "$run_sweep" = 1 ]; then
    say "   sweep:   $status_sweep"
fi
if [ "$run_perf" = 1 ]; then
    say "   perf:    $status_perf"
fi
if [ "$run_smoke" = 1 ]; then
    say "   smoke:   $status_smoke"
fi
if [ "$run_live" = 1 ]; then
    say "   live:    $status_live"
fi
if [ "$run_theme" = 1 ]; then
    say "   theme:   $status_theme"
fi
say "   evidence: $evidence"
say "   elapsed:  ${elapsed}s"

if [ "$rc_node" -ne 0 ] || [ "$rc_corpus" -ne 0 ] || [ "$rc_browser" -ne 0 ] || [ "$rc_sweep" -ne 0 ] ||
    [ "$rc_perf" -ne 0 ] || [ "$rc_smoke" -ne 0 ] || [ "$rc_live" -ne 0 ] || [ "$rc_theme" -ne 0 ]; then
    say "== RESULT: FAIL"
    exit 1
fi
say "== RESULT: PASS"
exit 0
