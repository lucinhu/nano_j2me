#!/usr/bin/env bash
# Fixed-workload VM benchmark. Configure the CMake build separately so feature
# flags, target CPU, and toolchain are explicit and reproducible.
#
#   tools/check_jit_perf.sh --build-fixture-only
#   tools/check_jit_perf.sh --build-dir build-rv32 \
#     --runner '.local-tools/rv32-jit/qemu-riscv32' --require-execution
#   tools/check_jit_perf.sh --build-dir build-arm64 --no-build \
#     --runner '.local-tools/arm64-jit/qemu-aarch64' --iterations 20000
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTIFACTS="$ROOT/.local-tools/jit-deopt"
mkdir -p "$ARTIFACTS"
BUILD_DIR=''
JAR_PATH="$ARTIFACTS/jitperf.jar"
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
[[ "$ITERATIONS" =~ ^[0-9]+$ ]] && (( ITERATIONS >= 1 && ITERATIONS <= 10000000 )) ||
    { usage; exit 2; }
case "$MODE" in all|interpreter|probe-only|execute) ;; *) usage; exit 2 ;; esac
if [[ "$REQUIRE_EXECUTION" == 1 && "$MODE" != all && "$MODE" != execute ]]; then
    echo 'FAIL: --require-execution accepts only --mode all or execute.' >&2
    exit 2
fi
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
LOG="$ARTIFACTS/check.perf.$(date +%Y%m%d-%H%M%S).$$.log"
printf 'JIT performance command:'
printf ' %q' "${RUNNER[@]}" "$BINARY" "${ARGS[@]}"
printf '\n'
echo 'Compare identical workloads/configurations; QEMU wall time does not predict board speed.'
set +e
"${RUNNER[@]}" "$BINARY" "${ARGS[@]}" 2>&1 | tee "$LOG"
STATUS=${PIPESTATUS[0]}
set -e
if [[ "$STATUS" == 77 ]]; then
    echo "SKIP perf: JIT execution unavailable; log=$LOG"
    exit 77
fi
if [[ "$STATUS" != 0 ]]; then
    echo "FAIL perf: test_jit_perf exited $STATUS; log=$LOG" >&2
    exit 1
fi

# The fixture prints one RESULT and one FALLBACK line per kernel/mode. Validate
# the new deopt/map counters and cross-mode checksum equality here, so a green
# benchmark cannot hide a regressed fallback ratio or leaked executable memory.
EXECUTIONS=$(grep -c '^RESULT ' "$LOG" || true)
EXPECTED_RESULTS=6
if [[ "$MODE" == all ]]; then EXPECTED_RESULTS=18; fi
if [[ "$EXECUTIONS" != "$EXPECTED_RESULTS" ]]; then
    echo "FAIL perf: expected $EXPECTED_RESULTS RESULT lines, got $EXECUTIONS; log=$LOG" >&2
    exit 1
fi
for field in method_map_evictions faults metadata_bytes peak_code_bytes code_requested_bytes; do
    if ! grep -q " $field=" "$LOG"; then
        echo "FAIL perf: RESULT lines omit new stat $field; log=$LOG" >&2
        exit 1
    fi
done
if ! grep -q '^JIT_CONFIG .*method_entries=.*method_arena_bytes=.*max_faults=' "$LOG"; then
    echo "FAIL perf: JIT_CONFIG omits method-map capacity; log=$LOG" >&2
    exit 1
fi

awk '
function value(line, key,   n, a, i) {
    n = split(line, a, " ")
    for (i = 1; i <= n; ++i)
        if (index(a[i], key "=") == 1) return substr(a[i], length(key) + 2)
    return ""
}
function number(x) { return x ~ /^[0-9]+$/ }
function decimal(x) { return x ~ /^[0-9]+(\.[0-9]+)?$/ }
BEGIN { bad = 0; kernels = 0; mode_count = 0; max_fallback = 0.0; max_mem = 0 }
$1 == "JIT_CONFIG" {
    if (value($0, "method_entries") == "" || value($0, "method_arena_bytes") == "" ||
        value($0, "max_faults") == "") {
        print "FAIL perf: incomplete JIT_CONFIG"; bad = 1
    }
}
$1 == "RESULT" {
    mode = value($0, "mode"); kernel = value($0, "kernel")
    checksum = value($0, "checksum")
    if (mode == "" || kernel == "" || checksum == "") { print "FAIL perf: malformed RESULT"; bad = 1; next }
    key = kernel SUBSEP mode
    if (key in seen) { print "FAIL perf: duplicate RESULT " kernel "/" mode; bad = 1 }
    seen[key] = 1
    sum[kernel, mode] = checksum
    if (!(kernel in kernel_seen)) { kernel_seen[kernel] = 1; kernels++ }
    if (!(mode in mode_seen)) { mode_seen[mode] = 1; mode_count++ }
    if (mode == "probe-only") {
        if (value($0, "native_blocks") != "0" || value($0, "native_bytecodes") != "0" ||
            value($0, "compiled_blocks") != "0" || value($0, "code_allocations") != "0" ||
            value($0, "code_commits") != "0" || value($0, "code_bytes") != "0") {
            print "FAIL perf: probe-only allocated or executed code for " kernel; bad = 1
        }
    }
    if (mode == "interpreter") {
        if (value($0, "compiled_blocks") != "0" || value($0, "code_allocations") != "0") {
            print "FAIL perf: interpreter mode touched the JIT for " kernel; bad = 1
        }
    }
    if (mode == "execute") {
        if (!number(value($0, "compiled_blocks")) || value($0, "compiled_blocks") + 0 <= 0 ||
            !number(value($0, "native_blocks")) || value($0, "native_blocks") + 0 <= 0 ||
            !number(value($0, "code_commits")) || value($0, "code_commits") + 0 <= 0) {
            print "FAIL perf: execute mode did not commit/execute a block for " kernel; bad = 1
        }
        if (value($0, "code_allocations") != value($0, "code_frees")) {
            print "FAIL perf: execute mode leaked code memory for " kernel; bad = 1
        }
        if (value($0, "code_bytes") + 0 > value($0, "peak_code_bytes") + 0) {
            print "FAIL perf: code_bytes exceeds peak for " kernel; bad = 1
        }
        mem = value($0, "peak_code_bytes") + 0 + value($0, "metadata_bytes") + 0
        if (mem > max_mem) max_mem = mem
        if (!number(value($0, "faults"))) {
            print "FAIL perf: execute mode lacks fault count for " kernel; bad = 1
        }
    }
}
$1 == "FALLBACK" {
    mode = value($0, "mode"); ratio = value($0, "fallback_ratio")
    if (!number(value($0, "metadata_bytes")) || !number(value($0, "peak_code_bytes"))) {
        print "FAIL perf: FALLBACK lacks memory counters"; bad = 1
    }
    if (mode == "execute") {
        if (!decimal(ratio) || ratio + 0 < 0 || ratio + 0 > 1) {
            print "FAIL perf: invalid execute fallback ratio"; bad = 1
        } else if (ratio + 0 > max_fallback) max_fallback = ratio + 0
    }
}
END {
    if (kernels != 6) { print "FAIL perf: expected 6 kernels, saw " kernels; bad = 1 }
    # All available modes for a kernel must produce the identical checksum.
    for (k in kernel_seen) {
        ref = ""
        for (m in mode_seen) {
            if ((k SUBSEP m) in sum) {
                if (ref == "") ref = sum[k, m]
                else if (sum[k, m] != ref) {
                    print "FAIL perf: checksum mismatch for " k " (" m ")"; bad = 1
                }
            }
        }
    }
    printf "SUMMARY perf kernels=%d modes=%d results=%d execute_fallback_max=%.6f peak_footprint=%d\n",
           kernels, mode_count, 6 * mode_count, max_fallback, max_mem
    exit bad
}' "$LOG"

echo "PASS perf: six kernels match C oracles in every mode; map/deopt stats and fallback ratios checked; log=$LOG"
