#!/usr/bin/env bash
#
# Build the m3g surface fixture.
#
# M3GDemo calls the JSR 184 members a 3D title uses, so a member that was never
# implemented shows up as NoSuchMethodError here instead of only inside a game
# that has to be driven to its 3D scene.  tools/check_m3g_demo.sh holds the
# expectations; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/m3gdemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
M3G_STUBS="$(python3 "$ROOT/tools/m3g_stubs.py" --quiet)"
trap 'rm -rf "$WORK" "$M3G_STUBS"' EXIT
javac --release 8 -nowarn -classpath "$STUBS:$M3G_STUBS" -d "$WORK/classes" \
    "$ROOT/tests/m3gdemo/M3GDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: M3GDemo\r\nMIDlet-1: M3GDemo, , M3GDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
