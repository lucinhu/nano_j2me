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

    /*
     * A nested class with a constructor, plus a static initialiser below.
     *
     * These two exist for one specific regression: a `return` (void) used to
     * leave a spurious slot on the caller's operand stack, so every `new X()`
     * where X is Java - i.e. every constructor - shifted the caller's stack by
     * one.  main() itself never showed it because a method called from C
     * leaves through a different path; a fixture with no Java constructor at
     * all therefore passed while the bug was present.
     */
    static final class Box {
        int value;
        String tag;
        Box(int v) { value = v; tag = "box"; }
        int doubled() { return value * 2; }
    }

    /* Forces a <clinit>, which runs Java code from inside the `new` opcode. */
    static final int[] TABLE = { 3, 1, 4, 1, 5 };

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

        /*
         * Two-slot arguments in the SECOND position.
         *
         * A long or a double occupies two operand slots, and a native is handed
         * a pointer to the raw slots - so the second wide argument begins at
         * slot 2, not slot 1.  Reading it from slot 1 splices the first
         * argument's high half onto the second argument's low half.
         *
         * Every call below went through the wrong slot.  The symptom is never
         * "a slightly wrong number": Math.max(0L, 60L) returned 0, so a frame
         * pacer written as Thread.sleep(Math.max(0 - elapsed, 60L)) asked for a
         * sleep of 4,400 years and the game never drew another frame.  A C-side
         * test cannot catch this - it packs arguments one value per slot, which
         * happens to agree with the wrong index - so it has to be checked from
         * bytecode, which is what this fixture is.
         */
        boolean wideOk = true;
        // dconst_1 must push IEEE-754 1.0, not the integer bit pattern 1.
        // A runtime operand prevents javac from folding the reciprocal.
        double reciprocal = 1.0 / root;
        wideOk = wideOk && reciprocal > 0.707 && reciprocal < 0.708;
        wideOk = wideOk && Double.doubleToLongBits(1.0) == 4607182418800017408L;
        wideOk = wideOk && (Math.max(0L, 60L) == 60L);
        wideOk = wideOk && (Math.max(5L, 7L) == 7L);
        wideOk = wideOk && (Math.min(5L, 7L) == 5L);
        wideOk = wideOk && (Math.max(2.5, 0.5) == 2.5);
        wideOk = wideOk && (Math.min(2.5, 0.5) == 0.5);
        wideOk = wideOk && ((int) Math.pow(2.0, 10.0) == 1024);
        wideOk = wideOk && ((int) (Math.atan2(1.0, 1.0) * 1000.0) == 785);
        // Boxing a wide value: the object's field is filled from TWO slots, so
        // the caller must zero the upper one.  Leaving it as whatever was on
        // the C stack boxes a long whose top half is garbage.
        wideOk = wideOk && (Long.valueOf(60L).longValue() == 60L);
        wideOk = wideOk && (Long.valueOf(-1234567890123L).longValue() == -1234567890123L);
        wideOk = wideOk && (Double.valueOf(2.5).doubleValue() == 2.5);
        System.out.println(wideOk ? "wideargs ok" : "ERROR: wideargs");

        // Recursion.
        System.out.println(String.valueOf(fib(15)));

        // Array copy, the workhorse of every sprite blitter.
        int[] copy = new int[8];
        System.arraycopy(squares, 0, copy, 0, 8);
        System.out.println(describe(copy));

        // Constructors: the void-return path, plus a <clinit> triggered from
        // inside `new`.
        Box box = new Box(21);
        System.out.println(String.valueOf(box.value));
        System.out.println(String.valueOf(box.doubled()));
        System.out.println(box.tag);
        System.out.println(String.valueOf(TABLE[4]));

        // Two constructors in a row, so a leaked slot cannot be absorbed.
        Box a = new Box(1);
        Box b2 = new Box(2);
        System.out.println(String.valueOf(a.value + b2.value));

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

# Package EVERY class file, not just the top-level one.  A nested class is a
# separate HelloWorld$Box.class entry, and a game that references one gets
# NoClassDefFoundError if the packer only listed the outer file.
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )

echo "wrote $OUT"
unzip -l "$OUT"
