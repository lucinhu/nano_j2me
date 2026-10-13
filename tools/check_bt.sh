#!/bin/sh
#
# The Bluetooth fixture, on the fake radio the headless port can provide.
#
#     tools/check_bt.sh build/nj2me tests/fixtures/btdemo.jar
#
# The fake is opt-in (NJ_HEADLESS_BT=1) because the answer a port with no
# radio gives - LocalDevice.getLocalDevice() throwing BluetoothStateException -
# is what tests/test_wireless.c asserts, and a port cannot have both answers at
# once.  With the fake in place the whole JSR 82 walk runs: the local device,
# an inquiry that ends in deviceDiscovered() and inquiryCompleted(), a service
# search, an RFCOMM server accepted and echoed, and an L2CAP server.
#
# Exit status is 0 only when every expected line is present, so this can be a
# ctest test by itself.

set -u

NJ="${1:-build/nj2me}"
JAR="${2:-tests/fixtures/btdemo.jar}"

if [ ! -x "$NJ" ]; then
    echo "check_bt: no runtime at $NJ" >&2
    exit 2
fi
if [ ! -f "$JAR" ]; then
    echo "check_bt: no fixture at $JAR" >&2
    exit 2
fi

OUT="$(NJ_HEADLESS_BT=1 "$NJ" --port headless --fps 0 --run-frames 20000 "$JAR" 2>&1)"
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "check_bt: the runtime exited $RC" >&2
    echo "$OUT" >&2
    exit 1
fi

want() {
    if ! printf '%s\n' "$OUT" | grep -qF -- "$1"; then
        echo "check_bt: missing line: $1" >&2
        echo "--- the run said ---" >&2
        printf '%s\n' "$OUT" >&2
        exit 1
    fi
}

want "BT addr 001122334455"
want "BT class major 2 minor 3"
want "BT agent true"
want "BT discoverable true"
want "BT inquiry started true"
want "BT found 1 001122aabbcc Peer One major 2"
want "BT found 2 001122ddeeff Peer Two major 2"
want "BT inquiry done 0=0 found 2"
want "BT search uuid 1101"
want "BT search done 1=1"
want "BT services 1 count 2"
want "BT   record 0 url btspp://001122aabbcc:1"
want "BT client opened true"
want "BT server opened true"
want "BT accepted true"
want "BT read 16 peer-says-hello."
want "BT echo 5 ping."
want "BT l2cap accepted true"
want "BT l2cap recv 16 peer-says-hello."
want "BT l2cap mtu 672/672"
want "BT done"

echo "bluetooth fixture: every expected line present"
