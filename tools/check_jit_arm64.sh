#!/usr/bin/env bash
# Host encoding checks plus actual generated ARM64 code execution on AArch64
# Linux or qemu-aarch64. Link only the standalone test and portable emitter.
#
#   tools/check_jit_arm64.sh --require-execution
#   QEMU_AARCH64=/path/qemu-aarch64 ARM64_CC='zig cc' \
#     ARM64_CFLAGS='-target aarch64-linux-musl -static' tools/check_jit_arm64.sh
#
# The compiler's ISA is independent of JIT feature macros. Every generated
# instruction is scanned, and each generated call verifies AAPCS64 preservation.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE=auto
case "${1:-}" in
    --require-execution) MODE=required ;;
    --encoding-only) MODE=encoding ;;
    '') ;;
    *) echo "usage: $0 [--require-execution|--encoding-only]" >&2; exit 2 ;;
esac
[[ $# -le 1 ]] || { echo 'too many arguments' >&2; exit 2; }
mkdir -p "$ROOT/.local-tools/arm64-jit"
WORK="$(mktemp -d "$ROOT/.local-tools/arm64-jit/check.XXXXXX")"
read -r -a HOST_CC <<< "${CC:-cc}"
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" -I"$ROOT/src/core"
        -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1)
SOURCES=("$ROOT/tests/test_jit_arm64.c" "$ROOT/src/core/nj_jit_arm64.c")
ARMCC=()
ARMFLAGS=()
RUNNER=()
CAN_EXECUTE=0
if [[ "$MODE" != encoding ]]; then
    if [[ -n "${ARM64_CC:-}" ]]; then
        read -r -a ARMCC <<< "$ARM64_CC"
        read -r -a ARMFLAGS <<< "${ARM64_CFLAGS:-}"
    elif [[ "$(uname -m)" == aarch64 ]]; then
        ARMCC=("${HOST_CC[@]}")
    elif command -v zig >/dev/null 2>&1; then
        ARMCC=(zig cc)
        ARMFLAGS=(-target aarch64-linux-musl -static)
    elif command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
        ARMCC=(aarch64-linux-gnu-gcc)
        ARMFLAGS=(-static)
    fi
    if [[ "$(uname -m)" == aarch64 ]]; then
        CAN_EXECUTE=1
    elif [[ -n "${QEMU_AARCH64:-}" ]]; then
        RUNNER=("$QEMU_AARCH64")
        CAN_EXECUTE=1
    elif command -v qemu-aarch64 >/dev/null 2>&1; then
        RUNNER=(qemu-aarch64)
        CAN_EXECUTE=1
    elif command -v qemu-aarch64-static >/dev/null 2>&1; then
        RUNNER=(qemu-aarch64-static)
        CAN_EXECUTE=1
    elif [[ -x "$ROOT/.local-tools/arm64-jit/qemu-aarch64" ]]; then
        RUNNER=("$ROOT/.local-tools/arm64-jit/qemu-aarch64")
        CAN_EXECUTE=1
    fi
    [[ ${#ARMCC[@]} -gt 0 ]] || CAN_EXECUTE=0
fi

echo "ARM64 JIT check artifacts: $WORK"
if [[ "$CAN_EXECUTE" == 1 ]]; then
    printf 'ARM64 compiler:'; printf ' %q' "${ARMCC[@]}" "${ARMFLAGS[@]}"; printf '\n'
    if [[ ${#RUNNER[@]} -gt 0 ]]; then "${RUNNER[@]}" --version | head -n 1; fi
elif [[ "$MODE" == encoding ]]; then
    echo 'SKIP ARM64 execution: --encoding-only was requested.'
else
    echo 'SKIP ARM64 execution: need an AArch64 Linux compiler and qemu-aarch64 (or AArch64 Linux host).'
    [[ "$MODE" != required ]] || { echo 'FAIL: --require-execution forbids this skip.' >&2; exit 1; }
fi

printf '#include "nj2me/nj_config.h"\n' > "$WORK/config.c"
reject_config() {
    local label="$1" expected="$2"
    shift 2
    if "${HOST_CC[@]}" -std=c11 -I"$ROOT/include" "$@" -c "$WORK/config.c" \
        -o "$WORK/config.o" >"$WORK/$label.log" 2>&1; then
        echo "FAIL: invalid configuration $label compiled" >&2; exit 1
    fi
    if ! grep -Fq "$expected" "$WORK/$label.log"; then
        echo "FAIL: $label failed for an unrelated reason; see $WORK/$label.log" >&2; exit 1
    fi
    echo "PASS rejected configuration: $label"
}
for feature in FP SIMD LSE; do
    reject_config "nonboolean-$feature" 'JIT feature macros must be 0 or 1' \
        -DNJ_ENABLE_JIT=1 "-DNJ_JIT_ARM64_$feature=2"
done
reject_config engine-disabled 'NJ_ENABLE_JIT requires NJ_ENABLE_ENGINE=1' \
    -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=0

# SIMD/LSE are reserved capabilities: their independent rows must retain scalar
# execution. Disabling FP still permits integer mul/div/rem and reference copies.
ROWS=(
    'base 0 0 0 1 1'
    'all 1 1 1 1 1'
    'no-fp 0 1 1 1 1'
    'no-simd 1 0 1 1 1'
    'no-lse 1 1 0 1 1'
    'no-long 1 1 1 0 1'
    'no-float 1 1 1 1 0'
)
for row in "${ROWS[@]}"; do
    read -r name fp simd lse long float <<< "$row"
    DEFS=(-DNJ_JIT_ARM64_FP="$fp" -DNJ_JIT_ARM64_SIMD="$simd"
          -DNJ_JIT_ARM64_LSE="$lse" -DNJ_ENABLE_LONG="$long" -DNJ_ENABLE_FLOAT="$float")
    "${HOST_CC[@]}" "${COMMON[@]}" "${DEFS[@]}" "${SOURCES[@]}" -o "$WORK/host-$name"
    "$WORK/host-$name" | tee "$WORK/host-$name.log"
    if [[ "$CAN_EXECUTE" == 1 ]]; then
        "${ARMCC[@]}" "${ARMFLAGS[@]}" "${COMMON[@]}" "${DEFS[@]}" "${SOURCES[@]}" \
            -o "$WORK/arm64-$name"
        "${RUNNER[@]}" "$WORK/arm64-$name" --require-execution | tee "$WORK/arm64-$name.log"
    fi
done
if [[ "$CAN_EXECUTE" == 1 ]]; then
    echo 'PASS: all seven configurations executed real generated ARM64 machine code.'
else
    echo 'PASS: host encoding/metadata and invalid-config checks only; ARM64 execution SKIPPED.'
fi
