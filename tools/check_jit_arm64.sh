#!/usr/bin/env bash
# Host encoding checks plus actual generated ARM64 code under qemu-aarch64 (or
# an AArch64 Linux host). The standalone harness links the emitter directly and
# invokes every generated block through the documented JIT ABI.
#
#   tools/check_jit_arm64.sh --require-execution
#   tools/check_jit_arm64.sh --encoding-only
#   tools/check_jit_arm64.sh --build-fixture-only
#   ARM64_CC='zig cc' QEMU_AARCH64=/path/qemu-aarch64 \
#     tools/check_jit_arm64.sh --require-execution
#
# Capacity macros are independent of the compiler ISA: every row links the
# emitter with the new method-map/fault-budget limits and runs the same
# encoding, fault-ABI, field/array and FPCR-preservation assertions.
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
WORK="$(mktemp -d "$ROOT/.local-tools/jit-deopt/check.arm64.XXXXXX")"
read -r -a HOST_CC <<< "${CC:-cc}"
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
        -Wl,--gc-sections -I"$ROOT/include" -I"$ROOT/src/core"
        -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1)
SOURCES=("$ROOT/tests/test_jit_arm64.c" "$ROOT/src/core/nj_jit_arm64.c")

ARMCC=()
ARMFLAGS=()
RUNNER=()
CAN_BUILD=0
CAN_EXECUTE=0
if [[ -n "${ARM64_CC:-}" ]]; then
    read -r -a ARMCC <<< "$ARM64_CC"
    read -r -a ARMFLAGS <<< "${ARM64_CFLAGS:-}"
elif [[ -x "$ROOT/.local-tools/arm64-verify/cc.sh" ]]; then
    ARMCC=("$ROOT/.local-tools/arm64-verify/cc.sh")
elif command -v zig >/dev/null 2>&1; then
    ARMCC=(zig cc)
    ARMFLAGS=(-target aarch64-linux-musl -static)
elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    ARMCC=(aarch64-linux-gnu-gcc)
    ARMFLAGS=(-static)
elif [[ "$(uname -m)" == aarch64 ]]; then
    ARMCC=("$HOST_CC")
fi
[[ ${#ARMCC[@]} -gt 0 ]] && CAN_BUILD=1
if [[ "$MODE" != encoding ]]; then
    if [[ "$(uname -m)" == aarch64 ]]; then
        RUNNER=()
        CAN_EXECUTE="$CAN_BUILD"
    elif [[ -n "${QEMU_AARCH64:-}" ]]; then
        RUNNER=("$QEMU_AARCH64")
        CAN_EXECUTE="$CAN_BUILD"
    elif command -v qemu-aarch64 >/dev/null 2>&1; then
        RUNNER=(qemu-aarch64)
        CAN_EXECUTE="$CAN_BUILD"
    elif command -v qemu-aarch64-static >/dev/null 2>&1; then
        RUNNER=(qemu-aarch64-static)
        CAN_EXECUTE="$CAN_BUILD"
    elif [[ -x "$ROOT/.local-tools/arm64-jit/qemu-aarch64" ]]; then
        RUNNER=("$ROOT/.local-tools/arm64-jit/qemu-aarch64")
        CAN_EXECUTE="$CAN_BUILD"
    fi
fi

echo "ARM64 JIT check artifacts: $WORK"
if [[ "$CAN_BUILD" == 1 ]]; then
    printf 'ARM64 compiler:'; printf ' %q' "${ARMCC[@]}" "${ARMFLAGS[@]}"; printf '\n'
    if [[ "$CAN_EXECUTE" == 1 && ${#RUNNER[@]} -gt 0 ]]; then
        "${RUNNER[@]}" --version | head -n 1
    fi
elif [[ "$BUILD_ONLY" == 1 || "$MODE" == required ]]; then
    echo 'FAIL: no ARM64 compiler (ARM64_CC, local toolchain, zig or aarch64 gcc).' >&2
    exit 1
else
    echo 'SKIP ARM64 execution: need an AArch64 Linux compiler and qemu-aarch64 (or AArch64 host).'
fi
if [[ "$BUILD_ONLY" == 0 && "$CAN_EXECUTE" == 0 && "$MODE" == required ]]; then
    echo 'FAIL: --require-execution forbids this skip.' >&2
    exit 1
fi

# Configuration contract: feature dependencies, Boolean macros, and every new
# method-map/code-cache limit. These are host preprocessor checks.
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
for feature in FP SIMD LSE; do
    reject_config "nonboolean-$feature" 'JIT feature macros must be 0 or 1' \
        -DNJ_ENABLE_JIT=1 "-DNJ_JIT_ARM64_$feature=2"
done
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

# JIT off: no runtime API may remain and the emitter harness must refuse the
# configuration instead of silently running encoding tests against a dead JIT.
"${HOST_CC[@]}" -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" \
    -I"$ROOT/src/core" -DNJ_ENABLE_JIT=0 -DNJ_ENABLE_ENGINE=0 \
    -c "$ROOT/src/core/nj_jit.c" -o "$WORK/nj_jit-off.o"
if nm "$WORK/nj_jit-off.o" 2>/dev/null | grep -Eq ' [TtDd] nj_jit_(enable|get_stats|set_mode)$'; then
    echo 'FAIL: NJ_ENABLE_JIT=0 still emits the JIT runtime API' >&2; exit 1
fi
if "${HOST_CC[@]}" -std=c11 -I"$ROOT/include" -I"$ROOT/src/core" \
    -DNJ_ENABLE_JIT=0 -DNJ_ENABLE_ENGINE=0 -c "$ROOT/tests/test_jit_arm64.c" \
    -o "$WORK/test-jit-off.o" >"$WORK/jit-off.log" 2>&1; then
    echo 'FAIL: test_jit_arm64 accepted NJ_ENABLE_JIT=0' >&2; exit 1
fi
grep -Fq 'test_jit_arm64 requires NJ_ENABLE_JIT=1' "$WORK/jit-off.log" || {
    echo 'FAIL: JIT-off build failed for an unrelated reason' >&2; exit 1
}
echo 'PASS JIT off: runtime has no JIT API and the emitter test refuses to build'

# Feature rows plus capacity extremes. The capacity columns are the runtime map
# contract even though this harness calls the emitter directly; every row must
# link and execute the same generated-code assertions.
ROWS=(
    'base 0 0 0 1 1 32 4096 4 64 2 8'
    'all 1 1 1 1 1 32 4096 4 64 2 8'
    'no-fp 0 1 1 1 1 32 4096 4 64 2 8'
    'no-simd 1 0 1 1 1 32 4096 4 64 2 8'
    'no-lse 1 1 0 1 1 32 4096 4 64 2 8'
    'no-long 1 1 1 0 1 32 4096 4 64 2 8'
    'no-float 1 1 1 1 0 32 4096 4 64 2 8'
    'small-map 1 1 1 1 1 1 256 16 1 1 1'
    'big-map 1 1 1 1 1 256 65534 16 256 4 16383'
    'max-faults-16 1 1 1 1 1 32 4096 16 64 2 8'
    'stats-off 1 1 1 1 1 32 4096 4 64 2 8'
    'detailed 1 1 1 1 1 32 4096 4 64 2 8'
)
TOTAL_HOST_ASSERTIONS=0
TOTAL_ARM_ASSERTIONS=0
TOTAL_EXECUTIONS=0
RAN=0
for row in "${ROWS[@]}"; do
    read -r name fp simd lse long float mentries arena faults centries ways hot <<< "$row"
    DEFS=(-DNJ_JIT_ARM64_FP="$fp" -DNJ_JIT_ARM64_SIMD="$simd"
          -DNJ_JIT_ARM64_LSE="$lse" -DNJ_ENABLE_LONG="$long"
          -DNJ_ENABLE_FLOAT="$float"
          -DNJ_JIT_METHOD_ENTRIES="$mentries"
          -DNJ_JIT_METHOD_ARENA_BYTES="$arena"
          -DNJ_JIT_MAX_FAULTS="$faults"
          -DNJ_JIT_CACHE_ENTRIES="$centries" -DNJ_JIT_CACHE_WAYS="$ways"
          -DNJ_JIT_HOT_THRESHOLD="$hot")
    if [[ "$name" == stats-off ]]; then DEFS+=(-DNJ_JIT_STATS=0); fi
    if [[ "$name" == detailed ]]; then DEFS+=(-DNJ_JIT_DETAILED_STATS=1); fi
    echo "=== ARM64 $name ==="
    "${HOST_CC[@]}" "${COMMON[@]}" "${DEFS[@]}" "${SOURCES[@]}" \
        -o "$WORK/host-$name"
    if [[ "$BUILD_ONLY" == 1 ]]; then
        if [[ "$CAN_BUILD" == 1 ]]; then
            "${ARMCC[@]}" "${ARMFLAGS[@]}" "${COMMON[@]}" "${DEFS[@]}" \
                -static "${SOURCES[@]}" -o "$WORK/arm64-$name"
        fi
        echo "BUILT ARM64 $name"
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
        "${ARMCC[@]}" "${ARMFLAGS[@]}" "${COMMON[@]}" "${DEFS[@]}" \
            -static "${SOURCES[@]}" -o "$WORK/arm64-$name"
        "${RUNNER[@]}" "$WORK/arm64-$name" --require-execution \
            >"$WORK/arm64-$name.log" 2>&1 || {
            cat "$WORK/arm64-$name.log" >&2
            echo "FAIL: arm64 $name" >&2; exit 1;
        }
        arm_line="$(tail -n 1 "$WORK/arm64-$name.log")"
        echo "PASS arm64 $name: $arm_line"
        arm_assertions="$(sed -n 's/.*: \([0-9][0-9]*\) assertions.*/\1/p' <<< "$arm_line")"
        arm_execs="$(sed -n 's/.* \([0-9][0-9]*\) native executions.*/\1/p' <<< "$arm_line")"
        [[ -n "$arm_assertions" ]] && TOTAL_ARM_ASSERTIONS=$((TOTAL_ARM_ASSERTIONS + arm_assertions))
        [[ -n "$arm_execs" ]] && TOTAL_EXECUTIONS=$((TOTAL_EXECUTIONS + arm_execs))
        RAN=$((RAN + 1))
    elif [[ "$MODE" == required ]]; then
        echo 'FAIL: --require-execution requested but no ARM64 runner is available.' >&2
        exit 1
    fi
done

if [[ "$BUILD_ONLY" == 1 ]]; then
    echo "PASS built ARM64 fixture matrix in $WORK"
    exit 0
fi

# The LCG block is the cross-feature code-shape witness: all rows must emit the
# same 18 bytecodes; capacity/feature switches must not truncate it.
if [[ "$CAN_EXECUTE" == 1 ]]; then
    sample="$(awk '$1 == "SAMPLE" && $2 == "ARM64" { print $3; exit }' "$WORK/host-all.log")"
    sample_arm="$(awk '$1 == "SAMPLE" && $2 == "ARM64" { print $3; exit }' "$WORK/arm64-all.log")"
    [[ "$sample" == "insns=18" && "$sample_arm" == "insns=18" ]] || {
        echo "FAIL: LCG block shape differs (host '$sample', arm64 '$sample_arm')." >&2; exit 1;
    }
    echo "PASS LCG block: host and arm64 emit $sample."
fi

if [[ "$CAN_EXECUTE" == 1 ]]; then
    echo "SUMMARY ARM64 configs=$RAN host_assertions=$TOTAL_HOST_ASSERTIONS" \
         "arm64_assertions=$TOTAL_ARM_ASSERTIONS native_executions=$TOTAL_EXECUTIONS logs=$WORK"
    echo 'PASS: every configuration executed real generated ARM64 machine code.'
else
    echo "SUMMARY ARM64 configs=$(( ${#ROWS[@]} )) host_assertions=$TOTAL_HOST_ASSERTIONS" \
         "arm64_assertions=0 native_executions=0 logs=$WORK"
    echo 'PASS: host encoding/metadata, capacity and invalid-config checks only; ARM64 execution SKIPPED.'
fi
