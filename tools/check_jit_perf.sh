#!/usr/bin/env bash
# Fixed-workload VM benchmark. Configure the CMake build separately so feature
# flags, target CPU, and toolchain are explicit and reproducible.
#
# tools/check_jit_perf.sh --build-fixture-only
# tools/check_jit_perf.sh --build-dir build-rv32 \
#   --runner '.local-tools/rv32-jit/qemu-riscv32' --require-execution
# tools/check_jit_perf.sh --build-dir build-arm64 --no-build \
#   --runner '.local-tools/arm64-jit/qemu-aarch64' --iterations 20000
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR=''
JAR_PATH="$ROOT/tests/fixtures/jitperf.jar"
RUNNER_TEXT=''
ITERATIONS=20000
MODE=all
FIXTURE_ONLY=0
CUSTOM_JAR=0
NO_BUILD=0
REQUIRE_EXECUTION=0
usage() {
    echo "usage: $0 --build-fixture-only [--jar PATH]" >&2
    echo "       $0 --build-dir DIR [--runner 'PROGRAM [ARGS]'] [--jar PATH] [--no-build]" >&2
    echo '          [--iterations N] [--mode all|interpreter|probe-only|execute] [--require-execution]' >&2
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-fixture-only) FIXTURE_ONLY=1; shift ;;
        --no-build) NO_BUILD=1; shift ;;
        --require-execution) REQUIRE_EXECUTION=1; shift ;;
        --build-dir|--runner|--jar|--iterations|--mode)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            case "$1" in
                --build-dir) BUILD_DIR="$2" ;;
                --runner) RUNNER_TEXT="$2" ;;
                --jar) JAR_PATH="$2"; CUSTOM_JAR=1 ;;
                --iterations) ITERATIONS="$2" ;;
                --mode) MODE="$2" ;;
            esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage; exit 2 ;;
    esac
done
[[ "$ITERATIONS" =~ ^[0-9]+$ ]] || { usage; exit 2; }
case "$MODE" in all|interpreter|probe-only|execute) ;; *) usage; exit 2 ;; esac
if [[ "$FIXTURE_ONLY" == 0 && -z "$BUILD_DIR" ]]; then usage; exit 2; fi

build_fixture() (
    command -v javac >/dev/null || { echo 'FAIL: javac with --release 8 is required to build the fixture.' >&2; exit 1; }
    command -v jar >/dev/null || { echo 'FAIL: jar is required to build the fixture.' >&2; exit 1; }
    local temp_root classes resolved
    temp_root="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
    classes="$(mktemp -d "$temp_root/nano-jitperf.XXXXXX")"
    cleanup() {
        # Delete only the exact private temporary directory created above.
        resolved="$(cd "$classes" && pwd -P)"
        if [[ "$resolved" == "$classes" && "$resolved" == "$temp_root"/nano-jitperf.* ]]; then
            rm -rf -- "$resolved"
        else
            echo "Refusing unexpected temporary cleanup path: $resolved" >&2
        fi
    }
    trap cleanup EXIT
    javac --release 8 -g:none -d "$classes" "$ROOT/tests/jitperf/JitPerf.java"
    mkdir -p "$(dirname "$JAR_PATH")"
    # Stored entries keep fixture loading independent of an optional inflater.
    jar c0f "$JAR_PATH" -C "$classes" jitperf
    echo "Built fixed-kernel fixture: $JAR_PATH"
)
if [[ "$FIXTURE_ONLY" == 1 || "$CUSTOM_JAR" == 0 ]]; then build_fixture; fi
[[ -s "$JAR_PATH" ]] || { echo "FAIL: missing JAR: $JAR_PATH" >&2; exit 1; }
if [[ "$FIXTURE_ONLY" == 1 ]]; then exit 0; fi
[[ -d "$BUILD_DIR" ]] || { echo "FAIL: missing configured build directory: $BUILD_DIR" >&2; exit 1; }
if [[ "$NO_BUILD" == 0 ]]; then cmake --build "$BUILD_DIR" --target test_jit_perf; fi
BINARY="$BUILD_DIR/test_jit_perf"
if [[ ! -x "$BINARY" && -x "$BUILD_DIR/Debug/test_jit_perf" ]]; then BINARY="$BUILD_DIR/Debug/test_jit_perf"; fi
if [[ ! -x "$BINARY" && -x "$BUILD_DIR/Release/test_jit_perf" ]]; then BINARY="$BUILD_DIR/Release/test_jit_perf"; fi
[[ -x "$BINARY" ]] || { echo "FAIL: missing test_jit_perf executable in $BUILD_DIR" >&2; exit 1; }
RUNNER=()
if [[ -n "$RUNNER_TEXT" ]]; then read -r -a RUNNER <<< "$RUNNER_TEXT"; fi
ARGS=(--jar "$JAR_PATH" --iterations "$ITERATIONS" --mode "$MODE")
if [[ "$REQUIRE_EXECUTION" == 1 ]]; then ARGS+=(--require-execution); fi
printf 'JIT performance command:'
printf ' %q' "${RUNNER[@]}" "$BINARY" "${ARGS[@]}"
printf '\n'
echo 'Compare identical workloads/configurations; QEMU wall time does not predict board speed.'
"${RUNNER[@]}" "$BINARY" "${ARGS[@]}"
