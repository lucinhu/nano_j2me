package jitperf;

/** Fixed-workload compute kernels; no display, audio, clock, I/O, or RMS. */
public final class JitPerf {
    private JitPerf() {}

    /* Static state for the bytes and matrix kernels. Both tables are filled
       once during class initialisation, before any measured call. */
    private static final byte[] tiles = new byte[1024];
    private static int tileSum;
    private static final int[][] grid = new int[32][32];

    static {
        for (int i = 0; i < tiles.length; ++i) {
            tiles[i] = (byte) (i * 31 + 7);
        }
        for (int row = 0; row < 32; ++row) {
            for (int column = 0; column < 32; ++column) {
                grid[row][column] = row * 37 + column * 11 + 5;
            }
        }
    }

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

    /** Field and int-array traffic in one loop: exercises native getfield,
     *  putfield, iaload and iastore in the same block. */
    public static int mixed(int iterations) {
        Box box = new Box();
        int[] values = new int[32];
        int sum = 0;
        for (int i = 0; i < 32; ++i) {
            values[i] = i * 7 + 1;
        }
        for (int i = 0; i < iterations; ++i) {
            int index = i & 31;
            box.value = box.value + i;
            values[index] = values[index] + box.value;
            sum ^= values[index] + box.value;
            box.value = box.value ^ (i << 2);
        }
        return sum ^ box.value ^ values[iterations & 31];
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

    /** Static byte table with getstatic/putstatic and baload/bastore. */
    public static int bytes(int iterations) {
        int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            int index = (i * 7 + sum) & 1023;
            int value = tiles[index];
            value = (byte) (value + (i & 31));
            tiles[index] = (byte) value;
            sum = (sum + value) ^ i;
            tileSum = tileSum + sum;
        }
        return sum ^ tileSum;
    }

    /** Static int[][] traversal: aaload of the row, then iaload/iastore. */
    public static int matrix(int iterations) {
        int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            int[] row = grid[(i * 5 + sum) & 31];
            int column = (i * 11 + sum) & 31;
            int value = row[column];
            value = (value + (i & 0xffff)) ^ (sum >>> 3);
            row[column] = value;
            sum += value;
        }
        return sum;
    }

    /** Reads the static tables without mutating them.  The benchmark harness
     *  calls this once before the timed region so the class initialiser, which
     *  fills both tables, never runs inside a measured call. */
    public static int warm(int ignored) {
        return grid.length + tiles.length + tileSum + ignored;
    }

    /** Optional independent desktop-JVM checksum cross-check. */
    public static void main(String[] args) {
        int iterations = args.length == 0 ? 20000 : Integer.parseInt(args[0]);
        System.out.println("locals=" + Integer.toHexString(locals(iterations)));
        System.out.println("constants=" + Integer.toHexString(constants(iterations)));
        System.out.println("fields=" + Integer.toHexString(fields(iterations)));
        System.out.println("arrays=" + Integer.toHexString(arrays(iterations)));
        System.out.println("mixed=" + Integer.toHexString(mixed(iterations)));
        System.out.println("branches=" + Integer.toHexString(branches(iterations)));
        System.out.println("bytes=" + Integer.toHexString(bytes(iterations)));
        System.out.println("matrix=" + Integer.toHexString(matrix(iterations)));
    }
}
