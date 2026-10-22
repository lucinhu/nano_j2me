package jitperf;

/** Fixed-workload compute kernels; no display, audio, clock, I/O, or RMS. */
public final class JitPerf {
    private JitPerf() {}

    public static int locals(int iterations) {
        int a = 0x1234;
        int b = 0x5678;
        for (int i = 0; i < iterations; ++i) {
            a = (a + b) ^ (i << 1);
            b = (b ^ a) + 3;
        }
        return a ^ b;
    }

    public static int constants(int iterations) {
        int value = 0x12345678;
        for (int i = 0; i < iterations; ++i) {
            value = value * 1664525 + 1013904223;
        }
        return value;
    }

    private static final class Box {
        int value;
    }

    public static int fields(int iterations) {
        Box box = new Box();
        box.value = 7;
        for (int i = 0; i < iterations; ++i) {
            box.value = box.value + i;
            box.value = box.value ^ (i << 3);
        }
        return box.value;
    }

    public static int arrays(int iterations) {
        int[] values = new int[64];
        for (int i = 0; i < values.length; ++i) {
            values[i] = i * 17 + 3;
        }
        int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            int index = i & 63;
            values[index] = values[index] + i;
            sum ^= values[index];
        }
        return sum ^ values[iterations & 63];
    }

    public static int branches(int iterations) {
        int state = 0x13579bdf;
        int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            state = state * 1103515245 + 12345;
            if ((state & 7) < 3) {
                sum += state;
            } else if ((state & 1) == 0) {
                sum ^= state >>> 5;
            } else {
                sum -= state & 1023;
            }
        }
        return sum ^ state;
    }

    /** Optional independent desktop-JVM checksum cross-check. */
    public static void main(String[] args) {
        int iterations = args.length == 0 ? 20000 : Integer.parseInt(args[0]);
        System.out.println("locals=" + Integer.toHexString(locals(iterations)));
        System.out.println("constants=" + Integer.toHexString(constants(iterations)));
        System.out.println("fields=" + Integer.toHexString(fields(iterations)));
        System.out.println("arrays=" + Integer.toHexString(arrays(iterations)));
        System.out.println("branches=" + Integer.toHexString(branches(iterations)));
    }
}
