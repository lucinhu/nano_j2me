#!/usr/bin/env bash
#
# Build the VM-core test fixture: a small Java class that exercises the parts of
# the runtime a J2ME MIDlet actually leans on, packed into a JAR the way a real
# MIDlet suite is.
#
#   - static and virtual dispatch, recursion
#   - int / long / float arithmetic and arrays
#   - String, StringBuffer, String.valueOf
#   - try/catch with a real NullPointerException
#   - System.out routed through the port's log callback
#
# Deliberately avoids `+` on Strings: javac lowers that to a StringBuilder call
# chain whose bytecode shape changes between JDK versions, and the fixture must
# stay stable across toolchains.  (StringBuilder *is* provided by the class
# library for real game JARs; the fixture just does not depend on it.)
#
# Usage: tools/make_hello_jar.sh [output.jar]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/hello.jar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

if ! command -v javac >/dev/null 2>&1; then
    echo "javac not found; cannot build $OUT" >&2
    exit 1
fi
if ! command -v jar >/dev/null 2>&1; then
    echo "jar not found; cannot build $OUT" >&2
    exit 1
fi

mkdir -p "$WORK/src" "$WORK/classes" "$(dirname "$OUT")"

cat > "$WORK/src/HelloWorld.java" <<'JAVA'
/*
 * nano_j2me VM-core fixture.  Kept dependency-free on purpose: it must run on
 * the earliest CLDC profile the runtime claims to support.
 */
public class HelloWorld {

    static final String TAG = "hello";

    static int fib(int n) {
        if (n < 2) {
            return n;
        }
        return fib(n - 1) + fib(n - 2);
    }

    static String describe(int[] values) {
        StringBuffer sb = new StringBuffer();
        int i;
        for (i = 0; i < values.length; i++) {
            if (i > 0) {
                sb.append(',');
            }
            sb.append(values[i]);
        }
        return sb.toString();
    }

    public static void main(String[] args) {
        System.out.println("hello from j2me");

        int[] squares = new int[8];
        int i;
        for (i = 0; i < squares.length; i++) {
            squares[i] = i * i;
        }
        System.out.println(describe(squares));

        int sum = 0;
        for (i = 0; i < squares.length; i++) {
            sum += squares[i];
        }
        System.out.println(String.valueOf(sum));

        // Static String methods.
        System.out.println("TAG".toLowerCase());
        System.out.println(String.valueOf("abcdef".substring(2, 4)));
        System.out.println(String.valueOf("  padded  ".trim().length()));

        // Exception handling: the runtime must raise and catch a real NPE.
        Object nothing = null;
        try {
            nothing.toString();
            System.out.println("ERROR: expected NullPointerException");
        } catch (NullPointerException e) {
            System.out.println("caught npe");
        }

        // 64-bit arithmetic: 3^20 does not fit in 32 bits.
        long big = 1L;
        for (i = 0; i < 20; i++) {
            big = big * 3L;
        }
        System.out.println(String.valueOf(big));

        // Floating point through java.lang.Math.
        double root = Math.sqrt(2.0);
        System.out.println(String.valueOf((int) (root * 1000.0)));

        // Recursion.
        System.out.println(String.valueOf(fib(15)));

        // Array copy, the workhorse of every sprite blitter.
        int[] copy = new int[8];
        System.arraycopy(squares, 0, copy, 0, 8);
        System.out.println(describe(copy));

        System.out.println("done");
    }
}
JAVA

# --release 8 keeps the bytecode shape close to what a J2ME toolchain emits.
# No stubs are needed here: this fixture only touches java.lang, exactly as a
# CLDC-only MIDlet would.
javac --release 8 -nowarn -d "$WORK/classes" "$WORK/src/HelloWorld.java"

mkdir -p "$WORK/classes/META-INF"
cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MANIFEST'
Manifest-Version: 1.0
MIDlet-Name: nano_j2me VM core fixture
MIDlet-Version: 1.0.0
MIDlet-Vendor: nano_j2me
MIDlet-1: HelloWorld, , HelloWorld
MicroEdition-Profile: MIDP-2.0
MicroEdition-Configuration: CLDC-1.1
MANIFEST

( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF HelloWorld.class )

echo "wrote $OUT"
unzip -l "$OUT"
