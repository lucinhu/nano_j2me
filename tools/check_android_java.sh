#!/bin/sh
#
# Off-device checks for the Android port's plain-Java decisions: the keypad's
# press semantics (a keycap presses with the touch that lands on it, releases
# when the finger slides off) and the name a game is shown under (the MIDlet's
# own name, not the file's).
#
# A JDK is the only requirement: no SDK, no device, no emulator.
#
# Usage: tools/check_android_java.sh [TestClass ...]
#
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

JAVAC=${JAVAC:-javac}
JAVA=${JAVA:-java}
if ! command -v "$JAVAC" >/dev/null 2>&1 || ! command -v "$JAVA" >/dev/null 2>&1; then
    echo "check_android_java: no JDK, skipping"
    exit 0
fi

out=build/android/javatest
rm -rf "$out"
mkdir -p "$out"

# tests/android carries android.content stubs, so the port's classes compile
# without an SDK on the classpath.  No test may call into them.
sources=$(find tests/android -name '*.java' | LC_ALL=C sort)
# shellcheck disable=SC2086
if ! $JAVAC -nowarn -encoding UTF-8 -d "$out" \
        -sourcepath "src/port/android/java:tests/android" $sources; then
    echo "check_android_java: FAILED to compile"
    exit 1
fi

if [ "$#" -gt 0 ]; then
    classes=$*
else
    classes=$(cd "$out" && find . -name '*Test.class' | LC_ALL=C sort \
              | sed 's|^\./||; s|\.class$||; s|/|.|g')
fi

fail=0
for c in $classes; do
    echo "== $c"
    if ! $JAVA -cp "$out" "$c"; then
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check_android_java: FAILED"
    exit 1
fi
echo "check_android_java: ok"
