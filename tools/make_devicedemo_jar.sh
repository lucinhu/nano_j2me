#!/usr/bin/env bash
#
# Build the device-profile fixture.
#
# DeviceDemo prints what System.getProperty() answers for the keys a J2ME title
# uses to decide which handset it is on (microedition.platform, the vendor IMEI
# and IMSI spellings) plus the runtime's own facts, so the same jar can be run
# under several --device profiles and compared.  tools/check_device_props.sh
# holds the expectations; this only builds it.
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/devicedemo.jar}"
case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/classes/META-INF"

STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac --release 8 -nowarn -classpath "$STUBS" -d "$WORK/classes" \
    "$ROOT/tests/devicedemo/DeviceDemo.java"

printf 'Manifest-Version: 1.0\r\nMIDlet-Name: DeviceDemo\r\nMIDlet-1: DeviceDemo, , DeviceDemo\r\n\r\n' \
    > "$WORK/classes/META-INF/MANIFEST.MF"
mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF *.class )
echo "wrote $OUT"
