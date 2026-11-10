package randomdispatch;

import java.util.Random;

/** Same bytecode runs on the desktop JVM and the emulator. */
public class RandomDispatch {
    static long digest;
    static void mix(long v) { digest = digest * 1000003L + v; }
    static void emit(String name) {
        System.out.println("RANDOM-DISPATCH " + name + " " + digest);
        digest = 0;
    }

    static class Bits extends Random {
        int calls;
        int failAt;
        long trace;
        int[] values;
        Bits(long seed) { super(seed); }
        protected int next(int bits) {
            ++calls;
            trace = trace * 37 + bits;
            if (calls == failAt) throw new IllegalArgumentException("next fixture");
            if (values != null) return values[(calls - 1) % values.length];
            return super.next(bits);
        }
    }
    static class Inherited extends Bits {
        Inherited(long seed) { super(seed); }
    }
    static class Seed extends Random {
        static int calls;
        static long seen;
        static boolean fail;
        Seed() { super(); }
        Seed(long seed) { super(seed); }
        public void setSeed(long seed) {
            ++calls;
            seen = seed;
            if (fail) throw new IllegalArgumentException("seed fixture");
            super.setSeed(seed);
        }
    }
    static class Doubles extends Random {
        int calls;
        int failAt;
        double[] values = { 0.0, 0.0, 0.75, 0.5, 0.5, 0.75 };
        public double nextDouble() {
            ++calls;
            if (calls == failAt) throw new IllegalArgumentException("double fixture");
            return values[(calls - 1) % values.length];
        }
    }
    static class SuperDouble extends Bits {
        int doubleCalls;
        SuperDouble() { super(12345L); }
        public double nextDouble() { ++doubleCalls; return super.nextDouble(); }
    }

    static void methods() {
        Bits r = new Bits(12345L);
        mix(r.nextInt()); mix(r.calls); mix(r.trace);
        mix(r.nextInt(16)); mix(r.calls); mix(r.trace);
        mix(r.nextInt(1073741825)); mix(r.calls); mix(r.trace);
        mix(r.nextLong()); mix(r.calls); mix(r.trace);
        mix(r.nextBoolean() ? 1 : 0); mix(r.calls); mix(r.trace);
        mix(Float.floatToIntBits(r.nextFloat())); mix(r.calls); mix(r.trace);
        mix(Double.doubleToLongBits(r.nextDouble())); mix(r.calls); mix(r.trace);
        emit("methods");
    }
    static void inheritance() {
        Bits r = new Inherited(100L);
        for (int i = 0; i < 12; ++i) { mix(r.nextLong()); mix(r.nextInt(100)); }
        mix(r.calls); mix(r.trace); emit("inherited-super-next");
    }
    static void rejection() {
        Bits r = new Bits(0L);
        r.values = new int[] { Integer.MAX_VALUE, Integer.MAX_VALUE, 17 };
        mix(r.nextInt(1073741825)); mix(r.calls); mix(r.trace);
        r.calls = 0; r.trace = 0; r.values = new int[] { Integer.MAX_VALUE };
        mix(r.nextInt(4)); mix(r.calls); mix(r.trace);
        int before = r.calls;
        try { r.nextInt(0); mix(0); } catch (IllegalArgumentException e) { mix(1); }
        mix(r.calls - before); emit("rejection-invalid-power2");
    }
    static void signedLong() {
        Bits r = new Bits(0L);
        r.values = new int[] { 0x12345678, 0x80000000 };
        mix(r.nextLong()); mix(r.calls); mix(r.trace); emit("signed-long");
    }
    static void constructors() {
        Seed.calls = 0; Seed.fail = false;
        Seed r = new Seed(0x123456789abcdefL);
        mix(Seed.calls); mix(Seed.seen); mix(r.nextInt());
        Seed.calls = 0;
        r = new Seed();
        // Default seed selection differs between CLDC and modern Java SE.
        mix(Seed.calls);
        emit("constructor-set-seed");
        Seed.calls = 0; Seed.fail = true;
        try { new Seed(1L); mix(0); } catch (IllegalArgumentException e) { mix(1); }
        mix(Seed.calls);
        try { new Seed(); mix(0); } catch (IllegalArgumentException e) { mix(1); }
        mix(Seed.calls); Seed.fail = false; emit("constructor-throws");
    }
    static void throwsNext() {
        for (int method = 0; method < 7; ++method) {
            for (int at = 1; at <= 2; ++at) {
                Bits r = new Bits(12345L); r.failAt = at;
                if (method == 2) r.values = new int[] { Integer.MAX_VALUE };
                try {
                    switch (method) {
                    case 0: r.nextInt(); break;
                    case 1: r.nextInt(16); break;
                    case 2: r.nextInt(1073741825); break;
                    case 3: r.nextLong(); break;
                    case 4: r.nextBoolean(); break;
                    case 5: r.nextFloat(); break;
                    default: r.nextDouble(); break;
                    }
                    mix(0);
                } catch (IllegalArgumentException e) { mix(1); }
                mix(r.calls); mix(r.trace);
            }
        }
        emit("next-throws-short-circuit");
    }
    static void gaussians() {
        Doubles r = new Doubles();
        // Transcendental last-bit differences are outside this dispatch test.
        mix((long)(r.nextGaussian() * 1000000)); mix(r.calls);
        mix((long)(r.nextGaussian() * 1000000)); mix(r.calls);
        r.setSeed(1L);
        mix((long)(r.nextGaussian() * 1000000)); mix(r.calls);
        emit("gaussian-double-cache-reset");
        for (int at = 1; at <= 2; ++at) {
            r = new Doubles(); r.failAt = at;
            try { r.nextGaussian(); mix(0); } catch (IllegalArgumentException e) { mix(1); }
            mix(r.calls);
            r.failAt = 0;
            r.values = new double[] { 0.75, 0.5 };
            int before = r.calls;
            mix((long)(r.nextGaussian() * 1000000)); mix(r.calls - before);
        }
        emit("gaussian-throws-no-cache");
        SuperDouble s = new SuperDouble();
        mix((long)(s.nextGaussian() * 1000000)); mix(s.doubleCalls);
        mix((long)(s.nextGaussian() * 1000000)); mix(s.doubleCalls);
        mix(s.calls); mix(s.trace); emit("gaussian-super-double");
    }
    public static void main(String[] args) {
        digest = 0;
        methods(); inheritance(); rejection(); signedLong(); constructors(); throwsNext(); gaussians();
        System.out.println("RANDOM-DISPATCH DONE");
    }
}
