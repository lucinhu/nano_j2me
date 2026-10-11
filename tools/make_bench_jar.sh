#!/usr/bin/env bash
#
# Build a compute-bound MIDlet whose ONLY job is to execute a fixed, known
# number of bytecodes and exit.
#
# Everything else in the corpus sleeps: a game thread waits on Thread.sleep and
# the frame loop spins, so "bytecodes per second" measured on a game says more
# about the scheduler than about the interpreter.  This fixture runs a mix of
# the operations a J2ME game actually spends its time in - virtual calls,
# interface calls, array loads and stores, int and long arithmetic, string
# building, and a static initialiser - for a fixed iteration count, so the same
# work is done on every machine and the rate is comparable.
#
#   tools/make_bench_jar.sh
#   build/nj2me --port headless --heap 16 --fps 0 --stats tests/fixtures/bench.jar
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/tests/fixtures/bench.jar}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/src/bench" "$WORK/classes"

cat > "$WORK/src/bench/Bench.java" <<'JAVA'
package bench;

import javax.microedition.midlet.MIDlet;

/*
 * A fixed workload, deliberately built out of the shapes a real game uses:
 * a virtual call in a hot loop, an interface call, int and long arithmetic,
 * an array walk, and enough string work to touch the heap.
 */
public class Bench extends MIDlet {

    interface Op { int apply(int x); }

    static final class Mul implements Op {
        public int apply(int x) { return x * 3 + 1; }
    }
    static final class Xor implements Op {
        public int apply(int x) { return (x ^ 0x5a5a) + 7; }
    }

    static final int[] TABLE = new int[512];
    static {
        for (int i = 0; i < TABLE.length; i++) {
            TABLE[i] = (i * 2654435761L > 0) ? i : -i;
        }
    }

    static int vcall(int x) { return x * 3 + 1; }

    static int work(int rounds) {
        Op a = new Mul();
        Op b = new Xor();
        int acc = 1;
        long lacc = 1;
        for (int i = 0; i < rounds; i++) {
            acc = vcall(acc) & 0xffff;        /* static call */
            acc += a.apply(acc) & 0xff;        /* interface call, impl A */
            acc ^= b.apply(i) & 0x7f;          /* interface call, impl B */
            acc += TABLE[acc & 511];           /* array load */
            TABLE[i & 511] = acc;              /* array store */
            lacc = (lacc * 6364136223846793005L + 1442695040888963407L);
            if ((lacc >>> 60) == 0) { acc++; }
        }
        return acc ^ (int)(lacc >>> 32);
    }

    public void startApp() {
        int sum = 0;
        for (int r = 0; r < 80; r++) {
            sum += work(20000);
        }
        String s = "";
        for (int i = 0; i < 200; i++) {
            s = s + (char)('a' + (i % 26));
            if (s.length() > 64) { s = ""; }
        }
        System.out.println("BENCH sum=" + sum + " s=" + s.length());
        notifyDestroyed();
    }

    public void pauseApp() { }
    public void destroyApp(boolean u) { }
}
JAVA

CP="$(python3 "$ROOT/tools/midp_stubs.py" --quiet)"
javac -source 8 -target 8 -nowarn -cp "$CP" -d "$WORK/classes" \
      "$WORK/src/bench/Bench.java" 2>/dev/null

mkdir -p "$WORK/classes/META-INF"
cat > "$WORK/classes/META-INF/MANIFEST.MF" <<'MF'
Manifest-Version: 1.0
MIDlet-Name: Bench
MIDlet-Version: 1.0.0
MIDlet-Vendor: nano_j2me
MIDlet-1: Bench, , bench.Bench
MF

mkdir -p "$(dirname "$OUT")"
( cd "$WORK/classes" && jar cfm "$OUT" META-INF/MANIFEST.MF . )
echo "wrote $OUT"
