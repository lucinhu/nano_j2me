#!/usr/bin/env bash
# Build the portable emitter on the host, then execute its RV32 output under
# qemu-riscv32 (or directly on RV32 Linux). No VM/runtime linkage is required.
#
#   tools/check_jit_rv32.sh --require-execution
#   QEMU_RISCV32=/path/qemu-riscv32 RV32_CC='zig cc' \
#     RV32_CFLAGS='-target riscv32-linux-musl -static' tools/check_jit_rv32.sh
#
# Compiler ISA and emitter feature switches are deliberately independent. The
# harness can use libc's full ISA; every generated instruction is checked for
# forbidden extensions before the generated function is actually invoked.
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
mkdir -p "$ROOT/.local-tools/rv32-jit"
WORK="$(mktemp -d "$ROOT/.local-tools/rv32-jit/check.XXXXXX")"
read -r -a HOST_CC <<< "${CC:-cc}"
COMMON=(-std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/include" -I"$ROOT/src/core"
        -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=1 -DNJ_ENABLE_FLOAT=1)
SOURCES=("$ROOT/tests/test_jit_rv32.c" "$ROOT/src/core/nj_jit_rv32.c")

RVCC=()
RVFLAGS=()
RUNNER=()
CAN_EXECUTE=0
if [[ "$MODE" != encoding ]]; then
    if [[ -n "${RV32_CC:-}" ]]; then
        read -r -a RVCC <<< "$RV32_CC"
        read -r -a RVFLAGS <<< "${RV32_CFLAGS:-}"
    elif command -v zig >/dev/null 2>&1; then
        RVCC=(zig cc)
        RVFLAGS=(-target riscv32-linux-musl -static)
    elif command -v riscv32-linux-gnu-gcc >/dev/null 2>&1; then
        RVCC=(riscv32-linux-gnu-gcc)
        RVFLAGS=(-static -march=rv32imafdc -mabi=ilp32d)
    elif [[ "$(uname -m)" == riscv32 ]]; then
        RVCC=("${HOST_CC[@]}")
    fi
    if [[ "$(uname -m)" == riscv32 ]]; then
        CAN_EXECUTE=1
    elif [[ -n "${QEMU_RISCV32:-}" ]]; then
        RUNNER=("$QEMU_RISCV32")
        CAN_EXECUTE=1
    elif command -v qemu-riscv32 >/dev/null 2>&1; then
        RUNNER=(qemu-riscv32)
        CAN_EXECUTE=1
    elif command -v qemu-riscv32-static >/dev/null 2>&1; then
        RUNNER=(qemu-riscv32-static)
        CAN_EXECUTE=1
    elif [[ -x "$ROOT/.local-tools/rv32-jit/qemu-riscv32" ]]; then
        RUNNER=("$ROOT/.local-tools/rv32-jit/qemu-riscv32")
        CAN_EXECUTE=1
    fi
    [[ ${#RVCC[@]} -gt 0 ]] || CAN_EXECUTE=0
fi

echo "RV32 JIT check artifacts: $WORK"
if [[ "$CAN_EXECUTE" == 1 ]]; then
    printf 'RV32 compiler:'; printf ' %q' "${RVCC[@]}" "${RVFLAGS[@]}"; printf '\n'
    if [[ ${#RUNNER[@]} -gt 0 ]]; then "${RUNNER[@]}" --version | head -n 1; fi
else
    echo 'SKIP RV32 execution: need an RV32 Linux compiler and qemu-riscv32 (or RV32 Linux host).'
    [[ "$MODE" != required ]] || { echo 'FAIL: --require-execution forbids this skip.' >&2; exit 1; }
fi

# D=1/F=0 must fail at preprocessing, and the diagnostic must identify the
# dependency. Also check the advertised Boolean/engine configuration contract.
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
reject_config d-without-f 'NJ_JIT_RV32_D requires NJ_JIT_RV32_F=1' \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_D=1 -DNJ_JIT_RV32_F=0
reject_config nonboolean 'JIT feature macros must be 0 or 1' \
    -DNJ_ENABLE_JIT=1 -DNJ_JIT_RV32_M=2
reject_config engine-disabled 'NJ_ENABLE_JIT requires NJ_ENABLE_ENGINE=1' \
    -DNJ_ENABLE_JIT=1 -DNJ_ENABLE_ENGINE=0

# Disabling F necessarily disables D. A has no atomic lowering today, but its
# separate row protects independence from M/F/D/C and still executes the blocks.
ROWS=(
    'rv32i 0 0 0 0 0 1'
    'all 1 1 1 1 1 1'
    'no-m 0 1 1 1 1 1'
    'no-a 1 0 1 1 1 1'
    'no-f 1 1 0 0 1 1'
    'no-d 1 1 1 0 1 1'
    'no-c 1 1 1 1 0 1'
    'no-long 1 1 1 1 1 0'
)
for row in "${ROWS[@]}"; do
    read -r name m a f d c long <<< "$row"
    DEFS=(-DNJ_JIT_RV32_M="$m" -DNJ_JIT_RV32_A="$a" -DNJ_JIT_RV32_F="$f"
          -DNJ_JIT_RV32_D="$d" -DNJ_JIT_RV32_C="$c" -DNJ_ENABLE_LONG="$long")
    "${HOST_CC[@]}" "${COMMON[@]}" "${DEFS[@]}" "${SOURCES[@]}" -o "$WORK/host-$name"
    "$WORK/host-$name" | tee "$WORK/host-$name.log"
    if [[ "$CAN_EXECUTE" == 1 ]]; then
        "${RVCC[@]}" "${RVFLAGS[@]}" "${COMMON[@]}" "${DEFS[@]}" "${SOURCES[@]}" \
            -o "$WORK/rv32-$name"
        "${RUNNER[@]}" "$WORK/rv32-$name" --require-execution | tee "$WORK/rv32-$name.log"
    fi
done
if [[ "$CAN_EXECUTE" == 1 ]]; then
    echo 'PASS: all eight configurations executed real generated RV32 machine code.'
else
    echo 'PASS: host encoding/metadata and invalid-config checks only; RV32 execution SKIPPED.'
fi
