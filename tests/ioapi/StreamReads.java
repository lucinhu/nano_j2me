package ioapi;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStream;

/* Real bytecode probes for the InputStream defaults and BAIS native bulk path.
 * Every public probe has descriptor ()I. Digests include counts, state and
 * untouched bytes; IoApiDesktop prints the permanent desktop-JVM oracle.
 * Constructor windows always fit the array: overlong windows are deliberately
 * outside this regression's CLDC/SE compatibility decision. */
public final class StreamReads {
    private StreamReads() {
    }

    public static final class ChildIOException extends IOException {
        public ChildIOException() {
            super();
        }
    }

    /* Only read() is implemented. Fail once after failAt successful bytes, then
     * resume, so a swallowed IOException must also leave the VM usable. */
    public static final class FaultInput extends InputStream {
        int pos;
        int calls;
        final int limit;
        final int failAt;
        final int kind;
        boolean failed;

        FaultInput(int limit, int failAt, int kind) {
            this.limit = limit;
            this.failAt = failAt;
            this.kind = kind;
        }

        public int read() throws IOException {
            calls++;
            if (!failed && pos == failAt) {
                failed = true;
                if (kind == 1) throw new IOException();
                if (kind == 2) throw new EOFException();
                if (kind == 3) throw new ChildIOException();
                if (kind == 4) throw new IllegalArgumentException();
                throw new Error();
            }
            return pos == limit ? -1 : 128 + pos++;
        }
    }

    /* The inherited read(byte[]) must dispatch to the range override. */
    public static final class RangeInput extends InputStream {
        int singleCalls;
        int rangeCalls;

        public int read() {
            singleCalls++;
            return -1;
        }

        public int read(byte[] dst, int off, int len) {
            rangeCalls++;
            if (len == 0) return 0;
            dst[off] = (byte)231;
            return 1;
        }
    }

    private static int add(int h, int v) {
        return h * 31 + v;
    }

    private static int addLong(int h, long v) {
        return add(add(h, (int)(v >>> 32)), (int)v);
    }

    private static int array(int h, byte[] b) {
        for (int i = 0; i < b.length; i++) h = add(h, b[i] & 255);
        return h;
    }

    private static byte[] source() {
        return new byte[] {0, 1, 127, (byte)128, (byte)254, (byte)255, 6, 7};
    }

    private static byte[] sentinels() {
        return new byte[] {85, 85, 85, 85, 85, 85};
    }

    /* Distinguish exception classes, including the Java-defined IO subclass. */
    private static int readResult(InputStream in, byte[] b, int off, int len)
            throws IOException {
        try {
            return in.read(b, off, len);
        } catch (ChildIOException e) {
            return -103;
        } catch (EOFException e) {
            return -102;
        } catch (IOException e) {
            return -101;
        } catch (IllegalArgumentException e) {
            return -104;
        } catch (Error e) {
            return -105;
        }
    }

    private static int fault(int failAt, int kind, boolean all)
            throws IOException {
        FaultInput in = new FaultInput(5, failAt, kind);
        byte[] b = sentinels();
        int result;
        if (all) {
            try {
                result = ((InputStream)in).read(b);
            } catch (IOException e) {
                result = -101;
            }
        } else {
            result = readResult(in, b, 1, 4);
        }
        int h = array(add(17, result), b);
        h = add(h, in.pos);
        h = add(h, in.calls);
        h = add(h, in.read());
        return add(h, in.calls);
    }

    public static int defaultBulkRead() throws IOException {
        FaultInput in = new FaultInput(3, -1, 0);
        byte[] b = sentinels();
        int h = add(17, in.read(b, 1, 2));
        h = array(h, b);
        h = add(h, in.read(b, 2, 3));
        h = array(h, b);
        h = add(h, in.read(b, 0, 1));
        h = add(h, in.read(b, b.length, 0));
        h = add(h, in.read(new byte[0]));
        h = add(h, in.calls);
        return add(h, in.pos);
    }

    public static int partialIOException() throws IOException {
        return fault(2, 1, false);
    }

    public static int partialEOFException() throws IOException {
        return fault(2, 2, false);
    }

    public static int partialChildIOException() throws IOException {
        return fault(2, 3, false);
    }

    public static int partialInheritedRead() throws IOException {
        return fault(2, 1, true);
    }

    public static int firstIOException() throws IOException {
        int h = 17;
        for (int kind = 1; kind <= 3; kind++) h = add(h, fault(0, kind, false));
        return h;
    }

    public static int runtimeException() throws IOException {
        return add(fault(0, 4, false), fault(2, 4, false));
    }

    public static int nonIoError() throws IOException {
        return add(fault(0, 5, false), fault(2, 5, false));
    }

    private static int rangeResult(InputStream in, byte[] b, int off, int len)
            throws IOException {
        try {
            return in.read(b, off, len);
        } catch (NullPointerException e) {
            return -201;
        } catch (IndexOutOfBoundsException e) {
            return -202;
        }
    }

    private static int bounds(InputStream in) throws IOException {
        byte[] b = sentinels();
        int[] offsets = {-1, 0, 6, 7, Integer.MAX_VALUE, 1,
                         Integer.MAX_VALUE, 0};
        int[] lengths = {1, -1, 1, 0, Integer.MAX_VALUE, Integer.MAX_VALUE,
                         0, Integer.MAX_VALUE};
        int h = add(17, rangeResult(in, null, -1, -1));
        h = add(h, rangeResult(in, null, 0, 0));
        for (int i = 0; i < offsets.length; i++) {
            h = add(h, rangeResult(in, b, offsets[i], lengths[i]));
        }
        h = add(h, rangeResult(in, b, b.length, 0));
        return array(h, b);
    }

    public static int defaultBounds() throws IOException {
        FaultInput in = new FaultInput(3, -1, 0);
        int h = bounds(in);
        h = add(h, in.calls);
        return add(h, in.read());
    }

    public static int inheritedRangeDispatch() throws IOException {
        RangeInput in = new RangeInput();
        byte[] b = sentinels();
        int h = array(add(17, ((InputStream)in).read(b)), b);
        h = add(h, ((InputStream)in).read(new byte[0]));
        h = add(h, in.rangeCalls);
        return add(h, in.singleCalls);
    }

    public static int baisBulkRead() throws IOException {
        InputStream in = new ByteArrayInputStream(source(), 2, 4);
        byte[] b = sentinels();
        int h = add(17, in.read(b, 1, 2));
        h = array(h, b);
        h = add(h, in.available());
        h = add(h, in.read(b, 2, 4));
        h = array(h, b);
        h = add(h, in.available());
        h = add(h, in.read(b, 0, 1));
        return add(h, in.read());
    }

    public static int baisInheritedRead() throws IOException {
        InputStream in = new ByteArrayInputStream(source(), 2, 4);
        byte[] b = sentinels();
        int h = array(add(17, in.read(b)), b);
        h = add(h, in.available());
        return add(h, in.read(b));
    }

    public static int baisZeroLength() throws IOException {
        ByteArrayInputStream in = new ByteArrayInputStream(new byte[] {42});
        DataInputStream data = new DataInputStream(in);
        byte[] b = sentinels();
        int h = add(17, in.read(b, b.length, 0));
        h = add(h, in.read(new byte[0]));
        h = add(h, data.read(b, b.length, 0));
        h = add(h, data.read(new byte[0]));
        h = add(h, in.available());
        h = add(h, data.read());
        h = add(h, in.read(b, b.length, 0));
        h = add(h, in.read(new byte[0]));
        h = add(h, data.read(b, b.length, 0));
        h = add(h, data.read(new byte[0]));
        data.readFully(b, b.length, 0); // readFully's empty request succeeds.
        h = add(h, in.available());
        h = add(h, new ByteArrayInputStream(new byte[0]).read(new byte[0]));
        return array(h, b);
    }

    public static int baisBounds() throws IOException {
        ByteArrayInputStream in = new ByteArrayInputStream(source());
        int h = bounds(in);
        h = add(h, in.available());
        h = add(h, in.read());
        // Validation precedes EOF, including null and overflowing requests.
        h = add(h, bounds(new ByteArrayInputStream(new byte[0])));
        return h;
    }

    private static int alias(int pos, int off, int len) throws IOException {
        byte[] b = source();
        ByteArrayInputStream in = new ByteArrayInputStream(b, pos, b.length - pos);
        int h = add(17, in.read(b, off, len));
        h = array(h, b);
        h = add(h, in.available());
        h = add(h, in.read());
        in.reset();
        return add(h, in.read());
    }

    public static int baisAliasForward() throws IOException {
        // Destination overwrites bytes the former per-byte loop still needed.
        return alias(0, 2, 6);
    }

    public static int baisAliasBackward() throws IOException {
        return alias(2, 0, 6);
    }

    private static int skips(long[] amounts) throws IOException {
        int h = 17;
        for (int i = 0; i < amounts.length; i++) {
            ByteArrayInputStream in = new ByteArrayInputStream(source(), 2, 4);
            h = addLong(h, in.skip(amounts[i]));
            h = add(h, in.available());
            h = add(h, in.read());
            h = addLong(h, in.skip(amounts[i]));
            h = add(h, in.available());
        }
        return h;
    }

    public static int baisNegativeSkip() throws IOException {
        return skips(new long[] {-1L, -4294967295L, -4294967296L,
                                Long.MIN_VALUE, Long.MIN_VALUE + 1L});
    }

    public static int baisWideSkip() throws IOException {
        return skips(new long[] {0L, 1L, Integer.MAX_VALUE, 2147483648L,
                                4294967296L, 4294967297L, Long.MAX_VALUE});
    }

    public static int baisMarkReset() throws IOException {
        ByteArrayInputStream in = new ByteArrayInputStream(source(), 2, 4);
        int h = add(17, in.markSupported() ? 1 : 0);
        h = add(h, in.read());
        in.reset(); // Default mark is the valid constructor window's offset.
        h = add(h, in.read());
        in.mark(1);
        byte[] b = sentinels();
        h = add(h, in.read(b, 1, 2));
        h = array(h, b);
        h = addLong(h, in.skip(Long.MAX_VALUE));
        h = add(h, in.read());
        in.reset();
        h = add(h, in.available());
        h = add(h, in.read());
        in.close();
        in.reset();
        h = add(h, in.read());
        return add(h, in.available());
    }

    public static int dataMarkReset() throws IOException {
        DataInputStream in = new DataInputStream(
                new ByteArrayInputStream(source(), 2, 4));
        int h = add(17, in.markSupported() ? 1 : 0);
        h = add(h, in.readUnsignedByte());
        in.mark(1);
        byte[] b = sentinels();
        h = add(h, in.read(b, 1, 4));
        h = array(h, b);
        h = add(h, in.read(b, 0, 0));
        in.reset();
        h = add(h, in.available());
        h = add(h, in.readUnsignedByte());
        h = add(h, in.skipBytes(Integer.MAX_VALUE));
        in.reset();
        return add(h, in.readUnsignedByte());
    }
}
