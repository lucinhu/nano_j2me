#!/usr/bin/env bash
#
# Run the SocketConnection fixture on the headless port and check what it must
# print.
#
# This is the NEGATIVE half of the socket test, and it is the half that belongs
# in a hermetic suite: the headless port has no socket stack, so every
# Connector.open("socket://...") must come back as ConnectionNotFoundException
# whose message says the BUILD has no socket support - not a timeout, not a
# bare "connection not found", and not a crash.  The positive half needs a live
# server and runs on a device:
#
#   python3 tools/socket_echo_server.py 8080
#   adb shell am start -n com.lucinhu.nj2me/.GameActivity \
#       --es jarpath /sdcard/Android/data/com.lucinhu.nj2me/files/jars/socketdemo.jar
#
# which is where the round trip (write, read, end of stream, reopen) is proven.
#
#   tools/check_socket.sh <nj2me> <socketdemo.jar>
#
set -uo pipefail

BIN="${1:?usage: check_socket.sh <nj2me> <socketdemo.jar>}"
JAR="${2:?usage: check_socket.sh <nj2me> <socketdemo.jar>}"

OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

"$BIN" --port headless --run-frames 40 "$JAR" > "$OUT" 2>&1

fail=0
want() {
    if ! grep -qF -- "$1" "$OUT"; then
        echo "missing: $1"
        fail=1
    fi
}

want "SOCKET url socket://10.0.2.2:8080"
want "SOCKET talk FAILED javax.microedition.io.ConnectionNotFoundException: this build has no socket support; the port must supply it"
want "SOCKET reopen FAILED javax.microedition.io.ConnectionNotFoundException: this build has no socket support; the port must supply it"
want "SOCKET refuse javax.microedition.io.ConnectionNotFoundException: this build has no socket support; the port must supply it"
want "SOCKET done"

if [ "$fail" -ne 0 ]; then
    echo "--- the run said ---"
    cat "$OUT"
    exit 1
fi

echo "socket fixture: $(grep -c '^SOCKET' "$OUT") lines, every expected one present"
