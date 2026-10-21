#!/usr/bin/env bash
#
# check_superinstr_digest.sh <nj2me-binary>
#
# The benchmark cannot see a wrong superinstruction.  Its branches are on
# constants and counters, so a fused branch that tested an uninitialized
# operand still produced the expected `BENCH sum` - it took a targeted digest
# to catch exactly that bug during the 2026-10-22 fusion round.
#
# This compiles a MIDlet whose methods each exercise one fused family, hashes
# every result into six per-category digests, and compares them with the SAME
# code running on the desktop JVM:
#
#   0  int conditional branches      iload/iload_k + ifeq..ifle
#   1  null branches                 aload/aload_k + ifnull/ifnonnull
#   2  store then load of one slot   istore_k + iload_k
#   3  constant + int arithmetic     iconst/bipush/sipush/ldc + iadd..ixor
#   4  division by zero              must still throw ArithmeticException
#   5  long/float                    must stay on the unfused path
#
# Exit status: 0 identical (or skipped: no javac/java), 1 any difference.
# The interpreter's bytecode total drops when fusions are on, which is why this
# test compares VALUES against a JVM instead of counts against a constant.
set -euo pipefail

BIN="${1:?usage: check_superinstr_digest.sh <nj2me-binary>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v javac >/dev/null 2>&1 || ! command -v java >/dev/null 2>&1; then
    echo "SKIP: javac/java not available"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src/cond" "$WORK/classes"

cat > "$WORK/src/cond/CondCore.java" <<'JAVA'
package cond;

/* Every method here is written so that javac emits the opcode pair the fused
 * handler recognises; the digest is order-sensitive. */
public class CondCore {
    static int[] hs = new int[6];

    static void mix(int c, int v) { hs[c] = (hs[c] ^ v) * 0x01000193; }

    static int eq0(int a) { return a == 0 ? 1 : 2; }
    static int ne0(int a) { return a != 0 ? 3 : 4; }
    static int lt0(int a) { return a < 0 ? 5 : 6; }
    static int ge0(int a) { return a >= 0 ? 7 : 8; }
    static int gt0(int a) { return a > 0 ? 9 : 10; }
    static int le0(int a) { return a <= 0 ? 11 : 12; }
    static int eq1(int a, int b) { return b == 0 ? 21 : 22; }
    static int lt2(int a, int b, int c) { return c < 0 ? 23 : 24; }
    static int gt3(int a, int b, int c, int d) { return d > 0 ? 25 : 26; }
    static int eqg(int a, int b, int c, int d, int e) { return e == 0 ? 31 : 32; }
    static int leg(int a, int b, int c, int d, int e) { return e <= 0 ? 33 : 34; }
    static int nul0(Object a) { return a == null ? 41 : 42; }
    static int nn1(Object a, Object b) { return b != null ? 43 : 44; }
    static int nulg(Object a, Object b, Object c, Object d, Object e) {
        return e == null ? 49 : 50;
    }
    static int sl0(int a) { a = a * 3 + 1; return a; }
    static int sl1(int a, int b) { b = b - 7; return b; }
    static int sl2(int a, int b, int c) { c = c & 255; return c; }
    static int sl3(int a, int b, int c, int d) { d = d ^ 0x5a; return d; }
    static int slg(int a, int b, int c, int d, int e) { e = e + 9; return e; }
    static int f_iadd(int x) { return x + 5; }
    static int f_isub(int x) { return x - 5; }
    static int f_imul(int x) { return x * 5; }
    static int f_idiv(int x) { return x / 5; }
    static int f_irem(int x) { return x % 5; }
    static int f_ishl(int x) { return x << 5; }
    static int f_ishr(int x) { return x >> 5; }
    static int f_iushr(int x) { return x >>> 5; }
    static int f_iand(int x) { return x & 0xffff; }
    static int f_ior(int x) { return x | 0x0f0f; }
    static int f_ixor(int x) { return x ^ 0x5a5a; }
    static int f_m1(int x) { return x * -1; }
    static int f_div0(int x, int z) {
        try { return x / z; } catch (ArithmeticException e) { return -999; }
    }
    static int f_rem0(int x, int z) {
        try { return x % z; } catch (ArithmeticException e) { return -998; }
    }
    static long l_add(long a, long b) { return a + b; }
    static float fl_mul(float a) { return a * 3.5f; }

    public static int digest() {
        int[] xs = { 0, 1, -1, 2, -2, 100, -100, 255, 65535, -65536, 1234567,
                     Integer.MIN_VALUE, Integer.MAX_VALUE };
        for (int i = 0; i < xs.length; i++) {
            int x = xs[i];
            mix(0, eq0(x)); mix(0, ne0(x)); mix(0, lt0(x)); mix(0, ge0(x));
            mix(0, gt0(x)); mix(0, le0(x)); mix(0, eq1(0, x)); mix(0, lt2(0, 0, x));
            mix(0, gt3(0, 0, 0, x)); mix(0, eqg(0, 0, 0, 0, x)); mix(0, leg(0, 0, 0, 0, x));
            mix(1, nul0(null)); mix(1, nul0(new Object()));
            mix(1, nn1(null, x == 0 ? null : new Object()));
            mix(1, nul0(x == 0 ? null : new Object()));
            mix(1, nulg(null, null, null, null, x == 0 ? null : new Object()));
            mix(2, sl0(x)); mix(2, sl1(0, x)); mix(2, sl2(0, 0, x));
            mix(2, sl3(0, 0, 0, x)); mix(2, slg(0, 0, 0, 0, x));
            mix(3, f_iadd(x)); mix(3, f_isub(x)); mix(3, f_imul(x)); mix(3, f_idiv(x));
            mix(3, f_irem(x)); mix(3, f_ishl(x)); mix(3, f_ishr(x)); mix(3, f_iushr(x));
            mix(3, f_iand(x)); mix(3, f_ior(x)); mix(3, f_ixor(x)); mix(3, f_m1(x));
            mix(4, f_div0(x, 0)); mix(4, f_rem0(x, 0));
            long l = l_add((long) x, 1234567890123L);
            mix(5, (int) l); mix(5, (int) (l >>> 32)); mix(5, (int) fl_mul((float) x));
        }
        int all = 0x811c9dc5;
        for (int i = 0; i < hs.length; i++) all = (all ^ hs[i]) * 0x01000193;
        return all;
    }

    static void print() {
        System.out.println("DIGEST");
        System.out.println(digest());
        System.out.println("PARTS");
        for (int i = 0; i < hs.length; i++) System.out.println(hs[i]);
    }

    public static void main(String[] args) { print(); }
}
JAVA

cat > "$WORK/src/cond/Cond.java" <<'JAVA'
package cond;

import javax.microedition.midlet.MIDlet;

public class Cond extends MIDlet {
    public void startApp() { CondCore.print(); notifyDestroyed(); }
    public void pauseApp() { }
    public void destroyApp(boolean unconditional) { }
}
JAVA

CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/cond/CondCore.java" "$WORK/src/cond/Cond.java"

java -cp "$WORK/classes" cond.CondCore 2>/dev/null | grep -E '^-?[0-9]+$' > "$WORK/jvm.txt"

cat > "$WORK/manifest.mf" <<'MF'
Manifest-Version: 1.0
MIDlet-Name: Cond
MIDlet-Version: 1.0.0
MIDlet-Vendor: nano_j2me
MIDlet-1: Cond, , cond.Cond
MF
(cd "$WORK" && jar cfm cond.jar manifest.mf -C classes .)

set +e
"$BIN" --port headless --heap 8 --fps 0 "$WORK/cond.jar" > "$WORK/run.log" 2>&1
STATUS=$?
set -e
grep -E '^-?[0-9]+$' "$WORK/run.log" > "$WORK/interp.txt" || true

if [ ! -s "$WORK/jvm.txt" ]; then
    echo "FAIL: desktop JVM produced no digest"
    exit 1
fi
if ! diff -u "$WORK/jvm.txt" "$WORK/interp.txt" > "$WORK/diff.txt"; then
    echo "FAIL: interpreter digest differs from the desktop JVM (exit status $STATUS)"
    cat "$WORK/diff.txt"
    exit 1
fi
if grep -qE 'VerifyError|ArithmeticException: / by zero$' "$WORK/run.log" \
   && ! grep -q '^thrown: java/lang/ArithmeticException' "$WORK/run.log"; then
    echo "FAIL: uncaught error in the interpreter run (exit status $STATUS)"
    tail -20 "$WORK/run.log"
    exit 1
fi

echo "PASS: $(wc -l < "$WORK/jvm.txt") digest values identical to the desktop JVM"
