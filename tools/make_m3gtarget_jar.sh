#!/usr/bin/env bash
# Fixed-work direct/buffered native M3G comparison; runtime supplies all APIs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/.local-tools/m3gtarget}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/nano-m3gtarget.XXXXXX")"
M3G_STUBS=''
cleanup() { rm -rf -- "$WORK"; if [[ -n "$M3G_STUBS" ]]; then rm -rf -- "$M3G_STUBS"; fi; }
trap cleanup EXIT
mkdir -p "$WORK/classes" "$OUT"
MIDP_STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
M3G_STUBS="$(python3 "$ROOT/tools/m3g_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$MIDP_STUBS:$M3G_STUBS" -d "$WORK/classes" \
    "$ROOT/tests/m3gtarget/M3GTargetBench.java"
for path in direct buffered select; do
    printf 'Manifest-Version: 1.0\r\nMIDlet-Name: M3GTargetBench\r\nMIDlet-1: M3GTargetBench, , M3GTargetBench\r\nRender-Path: %s\r\n\r\n' "$path" > "$WORK/manifest.mf"
    (cd "$WORK/classes" && jar c0fm "$OUT/m3g-$path.jar" "$WORK/manifest.mf" M3GTargetBench*.class)
done
printf 'Built direct, buffered and selectable M3G fixtures in %s\n' "$OUT"
