#!/usr/bin/env bash
#
# Run the JSR 256 fixture on the headless port and check the lines it must
# print.
#
# The headless port ships a synthetic accelerometer (1.500, -2.250, 9.750,
# fixed on purpose), so this is a hermetic test of the whole path: SensorManager
# -> SensorInfo/ChannelInfo -> SensorConnection -> getData() and a DataListener.
# The listener half matters most, because dataReceived() is called from the
# frame pump and no other test in this tree can drive it.
#
#   tools/check_sensor.sh <nj2me> <sensordemo.jar>
#
set -uo pipefail

BIN="${1:?usage: check_sensor.sh <nj2me> <sensordemo.jar>}"
JAR="${2:?usage: check_sensor.sh <nj2me> <sensordemo.jar>}"

OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

# --fps 30: this fixture starts its own listener thread, so it needs the
# paced frame loop (the CLI now leaves the port's cap alone by default,
# and the headless port is uncapped for benchmark runs).
"$BIN" --port headless --fps 30 --run-frames 200 "$JAR" > "$OUT" 2>&1

fail=0
want() {
    if ! grep -qF -- "$1" "$OUT"; then
        echo "missing: $1"
        fail=1
    fi
}

want "SENSOR unknown count 0"
want "SENSOR count 1"
want "SENSOR url sensor:acceleration;contextType=user;model=headless"
want "SENSOR quantity acceleration context user model headless"
want "SENSOR rate 100 available true conn 1"
want "SENSOR channels 3"
want "SENSOR channel axis_x unit m/s^2 type 1 scale 1.000"
want "SENSOR range -20.000..20.000 res 0.001"
want "SENSOR open true"
want "SENSOR state 1 opened 1"
want "SENSOR data 1"
want "SENSOR x 1.500 y -2.250 z 9.750"
want "SENSOR sample axis_x ts yes valid true"
want "SENSOR buffered 1"
want "SENSOR chan axis_x url sensor:acceleration;contextType=user;model=headless"
want "SENSOR sampleRate 50 of 1"
want "SENSOR errors 0 text true"
want "SENSOR condition unsupported"
want "SENSOR listening 2 is 2"
want "SENSOR evt 3 lost false 1.500,-2.250,9.750"
want "SENSOR done"

if [ "$fail" -ne 0 ]; then
    echo "--- the run said ---"
    cat "$OUT"
    exit 1
fi

echo "sensor fixture: $(grep -c '^SENSOR' "$OUT") lines, every expected one present"
