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
#   6  field reads                   aload_0 + getfield, all five field types
#   7  array reads                   iload/iload_k + iaload..saload, all types
#   8  counter steps                 iinc + iload_k / iload, iinc + goto
#   9  sum into a local              iadd + istore_k
#  10  long comparisons              lcmp + ifeq..ifle
#  11  long-field loop chain         getfield(int) + i2l + lcmp + ifXX, with
#                                    and without the aload_0 fold in front
#  12  null receiver                the fused getfield must still throw NPE
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
    static int[] hs = new int[13];

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

    static class Holder { int i; long l; float f; double d; Object o; }

    /* 6: aload_0 + getfield, one method per field type */
    static int gf_i(Holder h) { return h.i; }
    static long gf_l(Holder h) { return h.l; }
    static float gf_f(Holder h) { return h.f; }
    static double gf_d(Holder h) { return h.d; }
    static Object gf_o(Holder h) { return h.o; }

    /* 7: iload/iload_k + <array load> */
    static int al_i(int[] a, int i) { return a[i]; }
    static int al_c(char[] a, int i) { return a[i]; }
    static int al_b(byte[] a, int i) { return a[i]; }
    static int al_s(short[] a, int i) { return a[i]; }
    static int al_z(boolean[] a, int i) { return a[i] ? 1 : 0; }
    static Object al_a(Object[] a, int i) { return a[i]; }
    static long al_l(long[] a, int i) { return a[i]; }
    static float al_f(float[] a, int i) { return a[i]; }
    static double al_d(double[] a, int i) { return a[i]; }

    /* 8: iinc + iload (same slot, both encodings) and iinc + goto */
    static int inc_ld(int x) { x++; return x; }
    static int inc_ld5(int a, int b, int c, int d, int e) { e++; return e; }
    static int inc_loop(int x) {
        int s = 0;
        for (int k = 0; k < 4; k++) { x++; s += x; }
        return s;
    }

    /* 9: iadd + istore_k */
    static int add_st(int a, int b) { int c = a + b; return c; }

    /* 10: lcmp + ifXX */
    static int lc_lt(long a, long b) { return a < b ? 1 : 2; }
    static int lc_ge(long a, long b) { return a >= b ? 3 : 4; }
    static int lc_eq(long a, long b) { return a == b ? 5 : 6; }
    static int lc_ne(long a, long b) { return a != b ? 7 : 8; }

    /* 11: getfield(int) + i2l + lcmp + ifXX, with (h first) and without
     * (long first) the aload_0 fold in front of the field read */
    static int ch_gt(Holder h, long x) { return x > h.i ? 1 : 2; }
    static int ch_le(long x, Holder h) { return x <= h.i ? 3 : 4; }

    /* 11b: the same chain inside a loop, where a stack pointer that is two
     * slots off corrupts the values that follow it in the same frame.  A
     * single call still returned the right answer with the wrong sp, so the
     * digest must keep using the stack after the chain. */
    static int ch_loop(Holder h, long x) {
        int s = 0;
        for (int k = 0; k < 6; k++) {
            if (x > h.i) s += 1; else s += 2;
            s = s * 3 + k;
            x += 7;
        }
        return s;
    }

    /* 12: the fused field read must still throw on a null receiver */
    static int gf_n(Holder h) {
        try { return h.i; } catch (NullPointerException e) { return -1; }
    }

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

            Holder h = new Holder();
            h.i = x; h.l = (long) x * 1000003L; h.f = (float) x * 1.5f;
            h.d = (double) x / 3.0; h.o = (x == 0) ? null : new Object();
            mix(6, gf_i(h)); mix(6, (int) gf_l(h)); mix(6, (int) gf_f(h));
            mix(6, (int) gf_d(h)); mix(6, gf_o(h) == null ? 1 : 2);
            mix(6, gf_n(h)); mix(6, gf_n(null));

            int ix = (x & 3);
            int[] ai = { x, x + 1, x + 2, x + 3 };
            char[] ac = { (char) x, 'A', (char) (x + 1) };
            byte[] ab = { (byte) x, (byte) (x + 1) };
            short[] as = { (short) x, (short) (x + 1) };
            boolean[] az = { x != 0, true };
            Object[] ao = { null, h, Integer.valueOf(x) };
            long[] al = { (long) x, 7L };
            float[] af = { (float) x, 1.25f };
            double[] ad = { (double) x, 2.5 };
            mix(7, al_i(ai, ix)); mix(7, al_c(ac, ix % 3)); mix(7, al_b(ab, ix % 2));
            mix(7, al_s(as, ix % 2)); mix(7, al_z(az, ix % 2));
            mix(7, al_a(ao, ix % 3) == null ? 1 : 2);
            mix(7, (int) al_l(al, ix % 2)); mix(7, (int) al_f(af, ix % 2));
            mix(7, (int) al_d(ad, ix % 2));

            mix(8, inc_ld(x)); mix(8, inc_ld5(0, 0, 0, 0, x)); mix(8, inc_loop(x));
            mix(9, add_st(x, x + 3));
            mix(10, lc_lt((long) x, 5L)); mix(10, lc_ge((long) x, 5L));
            mix(10, lc_eq((long) x, 5L)); mix(10, lc_ne((long) x, 5L));
            mix(11, ch_gt(h, (long) x)); mix(11, ch_le((long) x, h));
            mix(11, ch_loop(h, (long) x));
            mix(12, gf_n(null)); mix(12, gf_n(h));
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
