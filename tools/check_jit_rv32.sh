#!/usr/bin/env bash
# Build the portable emitter on the host, then execute its RV32 output under
# qemu-riscv32 (or directly on RV32 Linux). The encoder test and the portable
# runtime are linked together, but the JIT is never enabled: every generated
# block is invoked directly through its C ABI.
#
#   tools/check_jit_rv32.sh --require-execution
#   QEMU_RISCV32=/path/qemu-riscv32 RV32_CC='zig cc' \
#     RV32_CFLAGS='-target riscv32-linux-musl -static' tools/check_jit_rv32.sh
#   tools/check_jit_rv32.sh --build-fixture-only
#
# Compiler ISA and emitter feature switches are deliberately independent. The
# harness can use a libc with a wider ISA; every generated instruction is
# checked against the enabled feature macros before the function is invoked.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE=auto
BUILD_ONLY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --require-execution) MODE=required; shift ;;
        --encoding-only) MODE=encoding; shift ;;
        --build-fixture-only) BUILD_ONLY=1; shift ;;
        --help|-h)
            echo "usage: $0 [--require-execution|--encoding-only] [--build-fixture-only]" >&2
            exit 0 ;;
        *) echo "usage: $0 [--require-execution|--encoding-only] [--build-fixture-only]" >&2
           exit 2 ;;
    esac
done
mkdir -p "$ROOT/.local-tools/jit-deopt"
WORK="$(mktemp -d "$ROOT/.local-tools/jit-deopt/check.rv32.XXXXXX")"
read -r -a HOST_CC <<< "${CC:-cc}"
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
        -Wl,--gc-sections -I"$ROOT/include" -I"$ROOT/src/core"
        -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1 -DNJ_ENABLE_FLOAT=1
        -DNJ_TEST_STANDALONE_JIT=1)
SOURCES=("$ROOT/tests/test_jit_rv32.c" "$ROOT/src/core/nj_jit_rv32.c"
         "$ROOT/src/core/nj_jit.c" "$ROOT/src/core/nj_engine.c")
# test_jit_rv32 references nj_jit_record_fault/nj_jit_instance_field and, on
# RV32, drives the real probe/execute path. --gc-sections drops the VM/port
# allocation paths it never reaches, keeping this a standalone encoder harness.

RVCC=()
RVFLAGS=()
RUNNER=()
CAN_BUILD=0
CAN_EXECUTE=0
if [[ -n "${RV32_CC:-}" ]]; then
    read -r -a RVCC <<< "$RV32_CC"
    read -r -a RVFLAGS <<< "${RV32_CFLAGS:-}"
elif [[ -x "$ROOT/.local-tools/rv32-jit/cc.sh" ]]; then
    RVCC=("$ROOT/.local-tools/rv32-jit/cc.sh")
elif command -v zig >/dev/null 2>&1; then
    RVCC=(zig cc)
    RVFLAGS=(-target riscv32-linux-musl -static)
elif command -v riscv32-linux-gnu-gcc >/dev/null 2>&1; then
    RVCC=(riscv32-linux-gnu-gcc)
    RVFLAGS=(-static -march=rv32imafdc -mabi=ilp32d)
fi
[[ ${#RVCC[@]} -gt 0 ]] && CAN_BUILD=1

if [[ "$MODE" != encoding ]]; then
    if [[ "$(uname -m)" == riscv32 ]]; then
        RUNNER=()
        CAN_EXECUTE="$CAN_BUILD"
    elif [[ -n "${QEMU_RISCV32:-}" ]]; then
        RUNNER=("$QEMU_RISCV32")
        CAN_EXECUTE="$CAN_BUILD"
    elif command -v qemu-riscv32 >/dev/null 2>&1; then
        RUNNER=(qemu-riscv32)
        CAN_EXECUTE="$CAN_BUILD"
    elif [[ -x "$ROOT/.local-tools/rv32-jit/qemu-riscv32" ]]; then
        RUNNER=("$ROOT/.local-tools/rv32-jit/qemu-riscv32")
        CAN_EXECUTE="$CAN_BUILD"
    fi
fi

echo "RV32 JIT check artifacts: $WORK"
if [[ "$BUILD_ONLY" == 1 ]]; then
    [[ "$CAN_BUILD" == 1 ]] || {
        echo 'FAIL: --build-fixture-only needs an RV32 compiler (RV32_CC, zig, or the local toolchain).' >&2
        exit 1
    }
    printf 'RV32 compiler:'; printf ' %q' "${RVCC[@]}" "${RVFLAGS[@]}"; printf '\n'
elif [[ "$CAN_EXECUTE" == 1 ]]; then
    printf 'RV32 compiler:'; printf ' %q' "${RVCC[@]}" "${RVFLAGS[@]}"; printf '\n'
    if [[ ${#RUNNER[@]} -gt 0 ]]; then "${RUNNER[@]}" --version | head -n 1; fi
else
    echo 'SKIP RV32 execution: need an RV32 Linux compiler and qemu-riscv32 (or RV32 Linux host).'
    [[ "$MODE" != required || "$BUILD_ONLY" == 1 ]] || {
        echo 'FAIL: --require-execution forbids this skip.' >&2; exit 1;
    }
fi

# Configuration contract: dependencies, Boolean feature macros and every new
# capacity knob. These are preprocessor checks, so they run on the host.
printf '#include "nj2me/nj_config.h"\n' > "$WORK/config.c"
reject_config() {
    local label="$1" expected="$2"
    shift 2
    if "${HOST_CC[@]}" -std=c11 -I"$ROOT/include" "$@" -c "$WORK/config.c" \
        -o "$WORK/config.o" >"$WORK/$label.log" 2>&1; then
        echo "FAIL: invalid configuration $label compiled" >&2; exit 1
    fi
    if ! grep -Fq "$expected" "$WORK/$label.log"; then
        echo "FAIL: $label failed for an unrelated reason; see $WORK/$label.log" >&2
        exit 1
    fi
    echo "PASS rejected configuration: $label"
}
reject_config d-without-f 'NJ_JIT_RV32_D requires NJ_JIT_RV32_F=1' \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_D=1 -DNJ_JIT_RV32_F=0
reject_config nonboolean 'JIT feature macros must be 0 or 1' \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_M=2
reject_config nonboolean-cache 'JIT feature macros must be 0 or 1' \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_REG_CACHE=2
reject_config engine-disabled 'NJ_ENABLE_JIT requires NJ_ENABLE_ENGINE=1' \
    -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=0
REJECT_RANGE='JIT cache/compilation limits are out of range'
reject_config method-entries-zero "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_METHOD_ENTRIES=0
reject_config method-entries-big "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_METHOD_ENTRIES=257
reject_config arena-odd "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_METHOD_ARENA_BYTES=4095
reject_config arena-small "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_METHOD_ARENA_BYTES=254
reject_config max-faults-zero "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_MAX_FAULTS=0
reject_config max-faults-big "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_MAX_FAULTS=17
reject_config cache-entries-big "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_CACHE_ENTRIES=257
reject_config cache-ways-bad "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_CACHE_WAYS=3
reject_config hot-zero "$REJECT_RANGE" \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_HOT_THRESHOLD=0

# JIT off: the runtime compiles to no implementation and the emitter test must
# refuse the configuration instead of pretending to exercise a disabled JIT.
"${HOST_CC[@]}" -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
    -I"$ROOT/src/core" -DNJ_ENABLE_JIT=0 -DNJ_ENABLE_ENGINE=0 \
    -c "$ROOT/src/core/nj_jit.c" -o "$WORK/nj_jit-off.o"
if nm "$WORK/nj_jit-off.o" 2>/dev/null | grep -Eq ' [TtDd] nj_jit_(enable|get_stats|set_mode)$'; then
    echo 'FAIL: NJ_ENABLE_JIT=0 still emits the JIT runtime API' >&2; exit 1
fi
if "${HOST_CC[@]}" -std=c11 -I"$ROOT/include" -I"$ROOT/src/core" \
    -DNJ_ENABLE_JIT=0 -DNJ_ENABLE_ENGINE=0 -c "$ROOT/tests/test_jit_rv32.c" \
    -o "$WORK/test-jit-off.o" >"$WORK/jit-off.log" 2>&1; then
    echo 'FAIL: test_jit_rv32 accepted NJ_ENABLE_JIT=0' >&2; exit 1
fi
grep -Fq 'test_jit_rv32 requires NJ_ENABLE_JIT=1' "$WORK/jit-off.log" || {
    echo 'FAIL: JIT-off build failed for an unrelated reason' >&2; exit 1
}
echo 'PASS JIT off: runtime has no JIT API and the emitter test refuses to build'

# Feature rows plus capacity extremes. METHOD_* / CACHE_* / HOT_THRESHOLD are
# runtime map knobs; the direct-emitter harness still validates their effect on
# layout decisions and that every compiled configuration links and runs.
ROWS=(
    'rv32i 0 0 0 0 0 1 1 32 4096 4 64 2 8'
    'all 1 1 1 1 1 1 1 32 4096 4 64 2 8'
    'no-m 0 1 1 1 1 1 1 32 4096 4 64 2 8'
    'no-a 1 0 1 1 1 1 1 32 4096 4 64 2 8'
    'no-f 1 1 0 0 1 1 1 32 4096 4 64 2 8'
    'no-d 1 1 1 0 1 1 1 32 4096 4 64 2 8'
    'no-c 1 1 1 1 0 1 1 32 4096 4 64 2 8'
    'no-long 1 1 1 1 1 0 1 32 4096 4 64 2 8'
    'no-cache 1 1 1 1 1 1 0 32 4096 4 64 2 8'
    'small-map 1 1 1 1 1 1 1 1 256 16 1 1 1'
    'big-map 1 1 1 1 1 1 1 256 65534 16 256 4 16383'
    'max-faults-16 1 1 1 1 1 1 1 32 4096 16 64 2 8'
    'stats-off 1 1 1 1 1 1 1 32 4096 4 64 2 8'
)
DEFS_FOR_ROW() {
    local name="$1" m="$2" a="$3" f="$4" d="$5" c="$6" long="$7" cache="$8"
    local mentries="$9" arena="${10}" faults="${11}" centries="${12}"
    local ways="${13}" hot="${14}"
    ROW_DEFS=(-DNJ_JIT_RV32_M="$m" -DNJ_JIT_RV32_A="$a" -DNJ_JIT_RV32_F="$f"
              -DNJ_JIT_RV32_D="$d" -DNJ_JIT_RV32_C="$c" -DNJ_ENABLE_LONG="$long"
              -DNJ_JIT_RV32_REG_CACHE="$cache"
              -DNJ_JIT_METHOD_ENTRIES="$mentries"
              -DNJ_JIT_METHOD_ARENA_BYTES="$arena"
              -DNJ_JIT_MAX_FAULTS="$faults"
              -DNJ_JIT_CACHE_ENTRIES="$centries" -DNJ_JIT_CACHE_WAYS="$ways"
              -DNJ_JIT_HOT_THRESHOLD="$hot")
    if [[ "$name" == stats-off ]]; then ROW_DEFS+=(-DNJ_JIT_STATS=0); fi
    if [[ "$name" == detailed ]]; then ROW_DEFS+=(-DNJ_JIT_DETAILED_STATS=1); fi
    # Keep the matrix one row per line: capacity extremes are also pair-tested
    # with detailed statistics via the explicit detailed row below.
}
TOTAL_HOST_ASSERTIONS=0
TOTAL_RV32_ASSERTIONS=0
TOTAL_EXECUTIONS=0
RAN=0

# A dedicated detailed-stats row keeps the stats-off row honest.
ROWS+=('detailed 1 1 1 1 1 1 1 32 4096 4 64 2 8')
for row in "${ROWS[@]}"; do
    read -r name m a f d c long cache mentries arena faults centries ways hot <<< "$row"
    DEFS_FOR_ROW "$name" "$m" "$a" "$f" "$d" "$c" "$long" "$cache" \
        "$mentries" "$arena" "$faults" "$centries" "$ways" "$hot"
    echo "=== RV32 $name ==="
    "${HOST_CC[@]}" "${COMMON[@]}" "${ROW_DEFS[@]}" "${SOURCES[@]}" \
        -o "$WORK/host-$name"
    if [[ "$BUILD_ONLY" == 1 ]]; then
        if [[ "$CAN_BUILD" == 1 ]]; then
            "${RVCC[@]}" "${RVFLAGS[@]}" "${COMMON[@]}" "${ROW_DEFS[@]}" \
                -static "${SOURCES[@]}" -o "$WORK/rv32-$name"
        fi
        echo "BUILT RV32 $name (host${CAN_BUILD:+; rv32})"
        continue
    fi
    "$WORK/host-$name" >"$WORK/host-$name.log" 2>&1 || {
        cat "$WORK/host-$name.log" >&2
        echo "FAIL: host $name" >&2; exit 1;
    }
    host_line="$(tail -n 1 "$WORK/host-$name.log")"
    echo "PASS host $name: $host_line"
    host_assertions="$(sed -n 's/.*: \([0-9][0-9]*\) assertions.*/\1/p' <<< "$host_line")"
    [[ -n "$host_assertions" ]] && TOTAL_HOST_ASSERTIONS=$((TOTAL_HOST_ASSERTIONS + host_assertions))
    if [[ "$CAN_EXECUTE" == 1 ]]; then
        "${RVCC[@]}" "${RVFLAGS[@]}" "${COMMON[@]}" "${ROW_DEFS[@]}" \
            -static "${SOURCES[@]}" -o "$WORK/rv32-$name"
        "${RUNNER[@]}" "$WORK/rv32-$name" --require-execution \
            >"$WORK/rv32-$name.log" 2>&1 || {
            cat "$WORK/rv32-$name.log" >&2
            echo "FAIL: rv32 $name" >&2; exit 1;
        }
        rv_line="$(tail -n 1 "$WORK/rv32-$name.log")"
        echo "PASS rv32 $name: $rv_line"
        rv_assertions="$(sed -n 's/.*: \([0-9][0-9]*\) assertions.*/\1/p' <<< "$rv_line")"
        rv_execs="$(sed -n 's/.* \([0-9][0-9]*\) native executions.*/\1/p' <<< "$rv_line")"
        [[ -n "$rv_assertions" ]] && TOTAL_RV32_ASSERTIONS=$((TOTAL_RV32_ASSERTIONS + rv_assertions))
        [[ -n "$rv_execs" ]] && TOTAL_EXECUTIONS=$((TOTAL_EXECUTIONS + rv_execs))
        RAN=$((RAN + 1))
    elif [[ "$MODE" == required ]]; then
        echo 'FAIL: --require-execution requested but no RV32 runner is available.' >&2
        exit 1
    fi
done

if [[ "$BUILD_ONLY" == 1 ]]; then
    echo "PASS built RV32 fixture matrix in $WORK"
    exit 0
fi

# Compare identical LCG blocks; these are instruction counts, not QEMU timings.
read_sample() {
    awk '$1 == "SAMPLE" && $2 == "RV32" {
        for (i = 4; i <= 7; ++i) { split($i, value, "="); printf "%s ", value[2] }
        print ""
    }' "$1"
}
read -r on_insns on_bytes on_loads on_stores < <(read_sample "$WORK/host-all.log")
read -r off_insns off_bytes off_loads off_stores < <(read_sample "$WORK/host-no-cache.log")
if (( on_insns != 18 || off_insns != 18 || on_bytes > off_bytes ||
      on_loads >= off_loads || on_stores != off_stores )); then
    echo 'FAIL: register cache must reduce LCG loads while preserving every store and bytecode.' >&2
    exit 1
fi
printf 'PASS LCG cache on/off: bytes %s/%s, loads %s/%s, stores %s/%s, bytecodes %s/%s\n' \
    "$on_bytes" "$off_bytes" "$on_loads" "$off_loads" "$on_stores" "$off_stores" \
    "$on_insns" "$off_insns"

if [[ "$CAN_EXECUTE" == 1 ]]; then
    echo "SUMMARY RV32 configs=$RAN host_assertions=$TOTAL_HOST_ASSERTIONS" \
         "rv32_assertions=$TOTAL_RV32_ASSERTIONS native_executions=$TOTAL_EXECUTIONS logs=$WORK"
    echo 'PASS: every configuration executed real generated RV32 machine code.'
else
    echo "SUMMARY RV32 configs=$(( ${#ROWS[@]} )) host_assertions=$TOTAL_HOST_ASSERTIONS" \
         "rv32_assertions=0 native_executions=0 logs=$WORK"
    echo 'PASS: host encoding/metadata, capacity and invalid-config checks only; RV32 execution SKIPPED.'
fi
