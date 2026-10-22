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
# every result into per-category digests, and compares them with the SAME
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
#  13  two loads + if_icmpXX         iload*; iload*; if_icmpXX, both encodings
#  14  long constant arithmetic      ldc2_w + ladd/lsub/lmul
#  15  loaded long arithmetic        lload/lload_k + ladd/lsub/lmul
#  16  counter read-back, any slot   iinc i; iload j with i != j
#  17  sum into a local, then read   iadd; istore_k; iload_k
#  18  class literals                ldc of CONSTANT_Class, lazy clinit
#  19  const arith then return       iconst..ldc + iadd..ixor + ireturn, all
#                                    const encodings, overflow, both shifts
#  20  ...and it can still throw     const + idiv/irem by zero, and the CALLER
#                                    keeps live operands below the folded
#                                    ireturn (long, array ref, nested calls)
#  21  long const comparisons        lconst_0/lconst_1 + lcmp + all six ifXX,
#                                    both operand orders, MIN/MAX edges
#  22  long comparisons in loops     lcmp/branch that keeps using the stack
#                                    afterwards, so a bad sp cannot hide
#  23  counter then branch           iinc i; iload j; ifXX with all six
#                                    conditions, same/different slot, iload_0..3
#                                    and the generic iload encoding
#  24  counter branch in loops       iinc + iload + unary branch in loops and
#                                    at the integer wrap edges
#  25  const arith into bitwise     const + arith whose result is then consumed
#                                    by iand/ior/ixor: a deeper LHS and an
#                                    already-computed RHS stay live, with shift
#                                    packing/masks and a throwing division on
#                                    the RHS.  The bitwise itself is NOT fused;
#                                    this is failure-match/deep-stack coverage
#                                    for the const-arith fold (that candidate
#                                    was withdrawn), not a fusion regression.
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
    static int[] hs = new int[26];

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

    /* 13: iload*; iload*; if_icmpXX (both the _k and the indexed encoding) */
    static int ic_lt(int a, int b) { return a < b ? 1 : 2; }
    static int ic_ge(int a, int b) { return a >= b ? 3 : 4; }
    static int ic_eq(int a, int b) { return a == b ? 5 : 6; }
    static int ic_ne(int a, int b) { return a != b ? 7 : 8; }
    static int ic_gt(int a, int b) { return a > b ? 9 : 10; }
    static int ic_le(int a, int b) { return a <= b ? 11 : 12; }
    static int ic_idx(int a, int b, int c, int d, int e) {
        return d > e ? 13 : 14;                       /* iload_3; iload 4 */
    }
    static int ic_loop(int a, int b) {
        int s = 0;
        for (int k = 0; k < 6; k++) { if (a < b) s += 1; else s += 2; a += 3; }
        return s;
    }

    /* 14: ldc2_w + ladd/lsub/lmul */
    static long lc_mul(long a) { return a * 6364136223846793005L; }
    static long lc_add(long a) { return a + 1442695040888963407L; }
    static long lc_sub(long a) { return a - 1234567890123L; }
    static long lc_chain(long a) {
        long r = a * 6364136223846793005L + 1442695040888963407L;
        return r * 3L - 5L;
    }

    /* 15: lload/lload_k + ladd/lsub/lmul */
    static long ll_add(long a, long b) { return a + b; }
    static long ll_sub(long a, long b) { return a - b; }
    static long ll_mul(long a, long b) { return a * b; }
    static long ll_loop(long a, long b) {
        long s = 0;
        for (int k = 0; k < 5; k++) { s = s + a * b; a += 3; }
        return s;
    }

    /* 16: iinc i; iload j  with i != j */
    static int inc_x(int a, int b) { a++; return b; }
    static int inc_y(int a, int b, int c, int d, int e) { e++; return a; }

    /* 17: iadd; istore_k; iload_k */
    static int add_st2(int a, int b) { int c = a + b; return c + 1; }
    static int add_st3(int a, int b, int c, int d) { d = a + b; return d * 2; }

    /* 18: ldc of a CONSTANT_Class.  The class literal has to hand back the
     * same mirror object Object.getClass() returns, and resolving it must not
     * run the class initializer: only the first active use does that. */
    static class Lit { int tag = 3; }
    static Object lit_obj = Lit.class;
    static int lit_ident() { return (Lit.class == Lit.class) ? 1 : 0; }
    static int lit_stored() { return (lit_obj == Lit.class) ? 2 : 0; }
    static int lit_getclass() { return (new Lit()).getClass() == Lit.class ? 3 : 0; }
    static int lit_sync() { synchronized (Lit.class) { return 4; } }
    static class Boom { static int x = 1; static { if (x == 1) throw new RuntimeException("boom"); } }
    static int lit_lazy() {
        Object k = Boom.class;                 /* must not initialize Boom */
        if (k == null) return 0;
        try { return Boom.x + 10; }
        catch (ExceptionInInitializerError e) { return 5; }
        catch (NoClassDefFoundError e) { return 5; }
    }

    /* 19: const + int arith + ireturn.  javac ends such a method with
     * <const-push> <arith>; ireturn, so the const/arith fold peeks one more
     * opcode to hand the result straight to the return.  Every int-arith
     * opcode and every constant encoding (iconst_m1..5, bipush, sipush, ldc)
     * is represented; r_loop keeps using the local and the stack after it. */
    static int r_iadd(int x) { return x + 5; }
    static int r_isub(int x) { return x - 5; }
    static int r_imul(int x) { return x * 5; }
    static int r_idiv(int x) { return x / 5; }
    static int r_irem(int x) { return x % 5; }
    static int r_ishl(int x) { return x << 5; }
    static int r_ishr(int x) { return x >> 5; }
    static int r_iushr(int x) { return x >>> 5; }
    static int r_iand(int x) { return x & 0xffff; }
    static int r_ior(int x) { return x | 0x0f0f; }
    static int r_ixor(int x) { return x ^ 0x5a5a; }
    static int r_bipush(int x) { return x + 100; }
    static int r_sipush(int x) { return x - 300; }
    static int r_ldc(int x) { return x * 70000; }
    static int r_min(int x) { return x + Integer.MIN_VALUE; }
    static int r_m1(int x) { return x * -1; }
    static int r_shl33(int x) { return x << 33; }
    static int r_divm1(int x) { return x / -1; }
    static int r_remm1(int x) { return x % -1; }
    static int r_loop(int x) {
        int s = 0;
        for (int k = 0; k < 5; k++) { s += x + 5; x = x * 3 - 7; }
        return s;
    }

    /* 20: the const+arith+ireturn fold must not swallow the exception path,
     * and the caller's live operands below the call must survive the return. */
    static int r_div0(int x) { try { return x / 0; } catch (ArithmeticException e) { return -999; } }
    static int r_rem0(int x) { try { return x % 0; } catch (ArithmeticException e) { return -998; } }
    static int r_call(int x) { return 1000 + r_iadd(x) + r_imul(x); }
    static int r_nest(int x) { return r_iadd(r_imul(x)); }
    static int r_longbelow(int x) { return (int) (10000000000L + r_iadd(x)); }
    static int r_arr(int x) {
        int[] a = new int[2];
        a[0] = 7 + r_isub(x);
        a[1] = r_iadd(x);
        return a[0] + a[1];
    }

    /* 21: lconst_0/lconst_1 + lcmp + ifXX, all six conditions, both operand
     * orders (z_rev/o_rev put the constant FIRST so lcmp is not adjacent). */
    static int z_eq(long x) { return x == 0L ? 1 : 2; }
    static int z_ne(long x) { return x != 0L ? 3 : 4; }
    static int z_lt(long x) { return x < 0L ? 5 : 6; }
    static int z_ge(long x) { return x >= 0L ? 7 : 8; }
    static int z_gt(long x) { return x > 0L ? 9 : 10; }
    static int z_le(long x) { return x <= 0L ? 11 : 12; }
    static int o_eq(long x) { return x == 1L ? 13 : 14; }
    static int o_ne(long x) { return x != 1L ? 15 : 16; }
    static int o_lt(long x) { return x < 1L ? 17 : 18; }
    static int o_ge(long x) { return x >= 1L ? 19 : 20; }
    static int o_gt(long x) { return x > 1L ? 21 : 22; }
    static int o_le(long x) { return x <= 1L ? 23 : 24; }
    static int z_rev(long x) { return 0L < x ? 25 : 26; }
    static int o_rev(long x) { return 1L >= x ? 27 : 28; }
    static int z_below(long x) { return 100 + (x == 0L ? 1 : 2) + (x < 0L ? 3 : 4); }

    /* 22: lcmp folds inside loops, where a stack pointer that is a slot or
     * two off corrupts the values that follow the comparison in the frame. */
    static int z_loop(long x) {
        int s = 0;
        for (int k = 0; k < 6; k++) {
            if (x == 0L) s += 1; else s += 2;
            s = s * 3 + k;
            x += 7L;
        }
        return s;
    }
    static int z_while(long x) {
        int s = 0;
        /* Halving keeps the loop short for large inputs; the fused lcmp+ifXX
         * still runs on every iteration. */
        while (x != 0L) { s = s * 5 + 1; x = (x > 1L) ? x / 2L : 0L; }
        return s;
    }
    static int o_loop(long x) {
        int s = 0;
        for (int k = 0; k < 6; k++) {
            if (x == 1L) s += 1; else s += 2;
            s = s * 5 + 3;
            x = (x > 1L) ? x - 1L : x + 1L;
        }
        return s;
    }

    /* 23: iinc i; iload j[; ifXX].  Same slot, different slot, all six unary
     * branches, both iload encodings (iload_0..3 and generic iload index). */
    static int is_same(int x) { x++; return x == 0 ? 1 : 2; }
    static int is_ne(int x) { x--; return x != 0 ? 3 : 4; }
    static int is_lt(int x) { x += 7; return x < 0 ? 5 : 6; }
    static int is_ge(int x) { x -= 7; return x >= 0 ? 7 : 8; }
    static int is_gt(int x) { x += 127; return x > 0 ? 9 : 10; }
    static int is_le(int x) { x += -128; return x <= 0 ? 11 : 12; }
    static int is_diff(int a, int b) { a++; return b < 0 ? 13 : 14; }
    static int is_idx(int a, int b, int c, int d, int e) { e++; return e >= 0 ? 15 : 16; }
    static int is_idx2(int a, int b, int c, int d, int e) { e--; return e == 0 ? 17 : 18; }

    /* 24: the counter-branch fold in loops, including counter wrap-around. */
    static int is_loop(int x) {
        int s = 0;
        for (int k = 0; k < 6; k++) { x++; if (x <= 0) s += 1; else s += 2; }
        return s;
    }
    static int is_loop5(int a, int b, int c, int d, int e) {
        int s = 0;
        for (int k = 0; k < 6; k++) { e -= 3; if (e < 0) s += 3; else s += 7; }
        return s;
    }
    static int is_max(int x) { x++; return x != 0 ? 19 : 20; }
    static int is_minloop(int x) {
        int s = 0;
        for (int k = 0; k < 4; k++) { x--; if (x > 0) s += 1; else s += 2; }
        return s;
    }

    /* 25: const + int arith whose result is consumed by a bitwise (iand/ior/
     * ixor).  The bitwise itself is NOT fused; this is failure-match and
     * deep-stack continuation coverage for the const-arith fold: the folded
     * value is the bitwise's already-computed RHS, with its LHS still live
     * below, so a fold that popped or wrote the wrong slot would corrupt the
     * deeper stack.  Shift packing, masks and a throwing idiv/irem on the RHS
     * are all represented; b_and_lhs is the negative control, where the fold
     * must not look through the following iload. */
    static int b_and(int a, int b)     { return b & (a + 5); }
    static int b_or(int a, int b)      { return b | (a * 3); }
    static int b_xor(int a, int b)     { return b ^ (a - 7); }
    static int b_and_sub(int a, int b) { return b & (a - 7); }
    static int b_and_lhs(int a, int b) { return (a + 5) & b; }   /* no fold here */
    static int b_or_pack(int a, int b) { return b | (a << 8); }
    static int b_and_shift(int a, int b) { return b & ((a << 4) & 0xff); }
    static int b_xor_shift(int a, int b) { return b ^ (a >> 3); }
    static int b_nest(int a, int b, int c) { return (c + b) & (a + 5); }
    static int b_deep(int a, int b)    { return (a * 2 + 1) ^ (b - 7); }
    static int b_masked(int a, int b)  { return (b & (a & 0x0f)) | ((a + 5) & 0xf0); }
    static int b_div0(int a, int b) {
        try { return b & (a / 0); } catch (ArithmeticException e) { return 11; }
    }
    static int b_rem0(int a, int b) {
        try { return b | (a % 0); } catch (ArithmeticException e) { return 12; }
    }
    static int b_div0_lhs(int a) {
        try { return (a / 0) & a; } catch (ArithmeticException e) { return 13; }
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
            mix(13, ic_lt(x, 5)); mix(13, ic_ge(x, 5)); mix(13, ic_eq(x, 5));
            mix(13, ic_ne(x, 5)); mix(13, ic_gt(x, 5)); mix(13, ic_le(x, 5));
            mix(13, ic_idx(0, 0, 0, x, x + 1)); mix(13, ic_loop(x, x + 2));
            mix(14, (int) lc_mul(x)); mix(14, (int) (lc_mul(x) >>> 32));
            mix(14, (int) lc_add(x)); mix(14, (int) lc_sub(x));
            mix(14, (int) lc_chain(x)); mix(14, (int) (lc_chain(x) >>> 32));
            mix(15, (int) ll_add((long) x, 7L)); mix(15, (int) ll_sub((long) x, 7L));
            mix(15, (int) ll_mul((long) x, 7L)); mix(15, (int) ll_loop((long) x, 3L));
            mix(15, (int) (ll_loop((long) x, 3L) >>> 32));
            mix(16, inc_x(x, x + 1)); mix(16, inc_y(x, 0, 0, 0, x + 2));
            mix(17, add_st2(x, x + 1)); mix(17, add_st3(x, x + 1, 0, 0));
            mix(18, lit_ident()); mix(18, lit_stored()); mix(18, lit_getclass());
            mix(18, lit_sync()); mix(18, lit_lazy());

            /* 19/20: const + arith + ireturn, incl. its exception path and
             * callers that keep operands live below the call. */
            mix(19, r_iadd(x)); mix(19, r_isub(x)); mix(19, r_imul(x));
            mix(19, r_idiv(x)); mix(19, r_irem(x)); mix(19, r_ishl(x));
            mix(19, r_ishr(x)); mix(19, r_iushr(x)); mix(19, r_iand(x));
            mix(19, r_ior(x)); mix(19, r_ixor(x)); mix(19, r_bipush(x));
            mix(19, r_sipush(x)); mix(19, r_ldc(x)); mix(19, r_min(x));
            mix(19, r_m1(x)); mix(19, r_shl33(x)); mix(19, r_divm1(x));
            mix(19, r_remm1(x)); mix(19, r_loop(x));
            mix(20, r_div0(x)); mix(20, r_rem0(x)); mix(20, r_call(x));
            mix(20, r_nest(x)); mix(20, r_longbelow(x)); mix(20, r_arr(x));

            /* 21/22: lconst_0/1 + lcmp [+ ifXX], and the same fold in loop
             * bodies that keep using the stack afterwards. */
            long lx = (long) x * 1000003L;
            mix(21, z_eq(lx)); mix(21, z_ne(lx)); mix(21, z_lt(lx));
            mix(21, z_ge(lx)); mix(21, z_gt(lx)); mix(21, z_le(lx));
            mix(21, o_eq(lx)); mix(21, o_ne(lx)); mix(21, o_lt(lx));
            mix(21, o_ge(lx)); mix(21, o_gt(lx)); mix(21, o_le(lx));
            mix(21, z_rev(lx)); mix(21, o_rev(lx)); mix(21, z_below(lx));
            mix(22, z_loop(lx)); mix(22, z_while(lx)); mix(22, o_loop(lx));

            /* 23/24: iinc + iload + unary branch, then the loop/wrap forms. */
            mix(23, is_same(x)); mix(23, is_ne(x)); mix(23, is_lt(x));
            mix(23, is_ge(x)); mix(23, is_gt(x)); mix(23, is_le(x));
            mix(23, is_diff(x, x)); mix(23, is_idx(0, 0, 0, 0, x));
            mix(23, is_idx2(0, 0, 0, 0, x));
            mix(24, is_loop(x)); mix(24, is_loop5(0, 0, 0, 0, x));
            mix(24, is_max(x)); mix(24, is_minloop(x));

            /* 25: the bitwise LHS is always already computed, so vary both
             * sides; the div/rem cases must keep throwing. */
            mix(25, b_and(x, 0x5a5a5a5a)); mix(25, b_or(x, 0x0f0f0f0f));
            mix(25, b_xor(x, -1)); mix(25, b_and_sub(x, 0x7fffffff));
            mix(25, b_and_lhs(x, 0x5a5a)); mix(25, b_or_pack(x, 0x0f0f));
            mix(25, b_and_shift(x, 0x5a5a)); mix(25, b_xor_shift(x, 0x3333));
            mix(25, b_nest(x, x, 0x1234)); mix(25, b_deep(x, -x));
            mix(25, b_masked(x, 0x5555)); mix(25, b_div0(x, 0x5a5a));
            mix(25, b_rem0(x, 0x5a5a)); mix(25, b_div0_lhs(x));
        }
        /* 21/22: exact 0L/1L operands and the long extremes, outside the
         * per-value loop so every condition sees both constants. */
        mix(21, z_eq(0L)); mix(21, z_ne(0L)); mix(21, z_lt(0L));
        mix(21, z_ge(0L)); mix(21, z_gt(0L)); mix(21, z_le(0L));
        mix(21, o_eq(1L)); mix(21, o_ne(1L)); mix(21, o_lt(1L));
        mix(21, o_ge(1L)); mix(21, o_gt(1L)); mix(21, o_le(1L));
        mix(21, z_lt(Long.MIN_VALUE)); mix(21, z_gt(Long.MAX_VALUE));
        mix(21, o_le(Long.MIN_VALUE)); mix(21, o_ge(Long.MAX_VALUE));
        mix(21, z_rev(0L)); mix(21, o_rev(1L));
        mix(22, z_loop(0L)); mix(22, z_loop(1L)); mix(22, z_while(7L));
        mix(22, o_loop(1L));
        /* 25: all-ones / zero operands and the wrap edges on both sides. */
        mix(25, b_and(0, -1)); mix(25, b_or(0, 0)); mix(25, b_xor(-1, -1));
        mix(25, b_and_lhs(Integer.MIN_VALUE, 0)); mix(25, b_and_lhs(7, 0));
        mix(25, b_nest(Integer.MIN_VALUE, Integer.MAX_VALUE, -1));
        mix(25, b_deep(Integer.MIN_VALUE, Integer.MIN_VALUE));
        mix(25, b_or_pack(Integer.MIN_VALUE, 0x0f0f));
        mix(25, b_and_shift(Integer.MAX_VALUE, 0x5a5a));
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
