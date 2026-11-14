#!/usr/bin/env bash
#
# Verify the Emscripten/WASM browser port.
#
#   tools/check_web.sh --node --corpus      what CI and the docs run (default)
#   tools/check_web.sh --node               JS <-> C API contract test only
#   tools/check_web.sh --corpus             deterministic 41-title JAR smoke test only
#   tools/check_web.sh --browser            browser page test (graceful SKIP until task-3)
#   tools/check_web.sh --build --node --corpus   rebuild first, then test
#
# Suites (default when no suite flag is given: --node --corpus):
#   --node      node tools/web_test/web_api_test.mjs
#               Header/baseline contract: API/runtime version identity, JAR
#               probing + icons, start error paths, hello.jar 240-frame
#               render baseline (240x320, all pixels non-black), 320x240 and
#               speed options, key/pointer input on game15.jar, save-state
#               round trip + truncated-state rejection, audiodemo audio, heap
#               stability.  Runs in well under a second.
#   --corpus    node tools/web_test/web_corpus_test.mjs
#               Pinned corpus (39 JARs from tests/games/ + the audio and bench
#               fixtures; the known-bad 1945空战英雄_6105.jar is excluded) is
#               probed, started and run 120 deterministic frames each.  ~25 s
#               on this machine; the whole node+corpus suite stays under a
#               minute, comfortably inside the two-minute budget.
#   --browser   node tools/web_test/web_browser_test.mjs
#               Real-page Playwright test of the assembled shell (task-3).  The
#               file does not exist yet; until it does this prints
#               "SKIP: browser page test not available yet" and the run still
#               exits 0.  Once the file lands, Playwright (Firefox) is required
#               and the script is invoked as:
#                   node tools/web_test/web_browser_test.mjs --evidence <dir>
#               sorted after --node/--corpus order: node, corpus, browser.
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
# .local-tools/emsdk).  The browser suite additionally needs Playwright/Firefox
# but is skipped while the test file is absent.
#
# Exit status: 0 when every executed suite passed (a browser SKIP is not a
# failure), 1 when a suite failed, 2 on usage or missing-artifact errors.
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
do_build=0
corpus_frames=120
started_at=$(date +%s)

usage() {
    sed -n '2,66p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
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
    --all)     run_node=1; run_corpus=1; run_browser=1 ;;
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

if [ "$run_node" = 0 ] && [ "$run_corpus" = 0 ] && [ "$run_browser" = 0 ]; then
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
say "   suites:     node=$run_node corpus=$run_corpus browser=$run_browser build=$do_build"

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
status_node="SKIP"
status_corpus="SKIP"
status_browser="SKIP"

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
        # Task-3: the Playwright test of the assembled shell does not exist yet.
        # This hook is intentionally graceful: the suite is skipped, not failed.
        say "SKIP: browser page test not available yet (tools/web_test/web_browser_test.mjs will be added with the browser shell)."
        status_browser="SKIP"
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
    # Distinguish a genuine skip (file absent) from a not-run suite.
    if grep -q "SKIP: browser page test not available yet" "$log" 2>/dev/null; then
        say "   browser: SKIP (not available yet)"
    else
        say "   browser: $status_browser"
    fi
fi
say "   evidence: $evidence"
say "   elapsed:  ${elapsed}s"

if [ "$rc_node" -ne 0 ] || [ "$rc_corpus" -ne 0 ] || [ "$rc_browser" -ne 0 ]; then
    say "== RESULT: FAIL"
    exit 1
fi
say "== RESULT: PASS"
exit 0
