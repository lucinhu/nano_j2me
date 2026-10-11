#!/usr/bin/env bash
#
# Build the whole suite with ASan + UBSan + LeakSanitizer and run it.
#
#     tools/run_sanitized_tests.sh
#
# A separate build directory and a separate run from the normal one, on purpose:
# the sanitizers change allocation behaviour, and a leak report is only
# meaningful if the binary was built for it.  `-DNJ_SANITIZE=ON` is a build
# option rather than a special test target so that EVERY test is sanitized -
# including test_play, which runs a whole MIDlet end to end - instead of
# whichever ones someone remembered to list.
#
# Debug rather than Release: ASan's reports name source lines, and at -O3 the
# line is often not the line.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${NJ_SAN_BUILD:-/tmp/nj2me-sanitize}"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
      -DNJ_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"

ctest --test-dir "$BUILD" --output-on-failure
