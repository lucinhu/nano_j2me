#!/usr/bin/env bash
#
# JSR 75's connection security types, end to end through the interpreter.
#
# 急速公路狂飙－新年版_84 runs `c instanceof javax.microedition.io.HttpsConnection`
# on every request and book7's checkers sample names SecureConnection and
# pki.Certificate.  Both were NoClassDefFoundError: the types did not exist, so
# the failure looked like a broken network rather than an unimplemented API.
#
# This check builds tests/httpsdemo/HttpsDemo.java against the stub API,
# packages it, runs it on the runtime and asserts the single line it prints.
# It is deliberately a Java-level check rather than a C unit test: what is
# being verified is that the interpreter RESOLVES the class for instanceof and
# checkcast (and that the two schemes answer differently), which a C test that
# calls nj_class_is_assignable() directly cannot prove.
#
#   build/nj2me --port headless tests/.../httpsdemo.jar
#     -> HTTPS http_is_https=false https_is_https=true https_is_http=true
#              url_ok=true secinfo=IOException types=true
#
# A build without the types reports `HTTPS ERROR java.lang.NoClassDefFoundError`.
#
# Usage: check_https_types.sh [jar]
#   NJ_BIN overrides the emulator under test (default build/nj2me).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${NJ_BIN:-$ROOT/build/nj2me}"
JAR="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if ! command -v javac >/dev/null 2>&1 || ! command -v jar >/dev/null 2>&1; then
    echo "SKIP: no JDK (javac/jar) to build the fixture"
    exit 77
fi
[ -x "$BIN" ] || { echo "SKIP: no emulator at $BIN"; exit 77; }

if [ -z "$JAR" ]; then
    STUBS="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)" || {
        echo "SKIP: the stub generator failed"; exit 77; }
    mkdir -p "$WORK/classes/META-INF"
    if ! javac --release 8 -nowarn -classpath "$STUBS" \
            -d "$WORK/classes" "$ROOT/tests/httpsdemo/HttpsDemo.java"; then
        echo "FAIL: the fixture does not compile against the stub API"
        exit 1
    fi
    printf 'Manifest-Version: 1.0\r\nMIDlet-Name: HttpsDemo\r\nMIDlet-1: HttpsDemo, , HttpsDemo\r\n\r\n' \
        > "$WORK/classes/META-INF/MANIFEST.MF"
    JAR="$WORK/httpsdemo.jar"
    ( cd "$WORK/classes" && jar cfm "$JAR" META-INF/MANIFEST.MF *.class ) || {
        echo "FAIL: could not package the fixture"; exit 1; }
fi

LOG="$WORK/run.log"
if ! timeout 120 "$BIN" --port headless --run-frames 120 --heap 8 "$JAR" \
        >"$LOG" 2>&1; then
    echo "FAIL: the emulator failed to run the fixture"
    sed -n '1,20p' "$LOG"
    exit 1
fi

LINE="$(grep -m1 '^HTTPS ' "$LOG" || true)"
if [ -z "$LINE" ]; then
    echo "FAIL: the fixture printed no HTTPS line"
    sed -n '1,20p' "$LOG"
    exit 1
fi
case "$LINE" in
    "HTTPS ERROR"*)
        echo "FAIL: the security types did not resolve: $LINE"
        exit 1
        ;;
esac

fail=0
expect() {   # $1 = field=value
    case "$LINE" in
        *"$1"*) ;;
        *) echo "FAIL: expected $1 in: $LINE"; fail=1 ;;
    esac
}
expect "http_is_https=false"        # an http:// connection is NOT an HttpsConnection
expect "https_is_https=true"        # and an https:// one is
expect "https_is_http=true"         # through HttpsConnection's HttpConnection super
expect "url_ok=true"                # the object really is an https connection
expect "secinfo=IOException"        # JSR 75's answer when TLS details are not exposed
expect "types=true"                 # SecureConnection / SecurityInfo / pki.Certificate

if [ "$fail" -ne 0 ]; then
    echo "  line: $LINE"
    exit 1
fi
echo "ok: $LINE"
exit 0
