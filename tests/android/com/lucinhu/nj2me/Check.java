package com.lucinhu.nj2me;

/**
 * Assertions for the port's off-device tests.
 *
 * <p>Small on purpose: these tests exist to pin decisions the player feels
 * (which key is down, which name is on the row), and a failure has to say
 * which decision broke in one line.
 */
final class Check {

    private static int passed;
    private static int failed;

    private Check() {
    }

    static void section(String name) {
        System.out.println("-- " + name);
    }

    static void eq(String what, Object got, Object want) {
        if (String.valueOf(got).equals(String.valueOf(want))) {
            passed++;
        } else {
            failed++;
            System.out.println("FAIL " + what + ": got " + got + ", want " + want);
        }
    }

    static void isTrue(String what, boolean ok) {
        if (ok) {
            passed++;
        } else {
            failed++;
            System.out.println("FAIL " + what);
        }
    }

    /** Ends the run: a non-zero exit is what ctest reads. */
    static void done() {
        System.out.println(passed + " checks passed, " + failed + " failed");
        if (failed > 0) {
            System.exit(1);
        }
    }
}
