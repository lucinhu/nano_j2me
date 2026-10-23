package ioapi;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.UnsupportedEncodingException;

/* Real Java calls exercise named constructors, virtual Reader/Writer methods,
 * and decoder/encoder state across calls.  This class is also a deliberately
 * fragmented InputStream: every bulk read yields at most one byte. */
public final class StreamEncoding extends InputStream {
    private final byte[] data;
    private int pos;

    private StreamEncoding(byte[] data) {
        this.data = data;
    }

    public int read() {
        return pos < data.length ? data[pos++] & 255 : -1;
    }

    public int read(byte[] dst, int off, int len) {
        if (len == 0) {
            return 0;
        }
        int b = read();
        if (b < 0) {
            return -1;
        }
        dst[off] = (byte) b;
        return 1;
    }

    public int available() {
        return data.length - pos;
    }

    private static byte[] bytes(int... values) {
        byte[] b = new byte[values.length];
        for (int i = 0; i < values.length; i++) {
            b[i] = (byte) values[i];
        }
        return b;
    }

    private static InputStreamReader reader(String name, int... values)
            throws IOException {
        return new InputStreamReader(new StreamEncoding(bytes(values)), name);
    }

    private static int byteDigest(byte[] b) {
        int h = 17;
        for (int i = 0; i < b.length; i++) {
            h = h * 31 + (b[i] & 255);
        }
        return h;
    }

    private static int readDigest(InputStreamReader in) throws IOException {
        int h = 17;
        int c;
        while ((c = in.read()) != -1) {
            h = h * 31 + c;
        }
        in.close();
        return h;
    }

    public static int defaultLatin1Read() throws IOException {
        return readDigest(new InputStreamReader(new ByteArrayInputStream(
                bytes(0, 0x7f, 0x80, 0xe9, 0xff))));
    }

    public static int defaultLatin1Write() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out);
        w.write(new char[] { 0, 0x7f, 0x80, 0xe9, 0xff });
        w.close();
        return byteDigest(out.toByteArray());
    }

    public static int latin1AliasesRead() throws IOException {
        String[] names = { "ISO-8859-1", "iSo8859-1", "ISO_8859_1",
                           "iso8859_1", "LaTiN1" };
        int h = 17;
        for (int i = 0; i < names.length; i++) {
            h = h * 31 + readDigest(reader(names[i], 0, 0x80, 0xe9, 0xff));
        }
        return h;
    }

    public static int latin1AliasesWrite() throws IOException {
        String[] names = { "ISO-8859-1", "iSo8859-1", "ISO_8859_1",
                           "iso8859_1", "LaTiN1" };
        int h = 17;
        for (int i = 0; i < names.length; i++) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            OutputStreamWriter w = new OutputStreamWriter(out, names[i]);
            w.write(new char[] { 0, 0x80, 0xe9, 0xff });
            w.close();
            h = h * 31 + byteDigest(out.toByteArray());
        }
        return h;
    }

    public static int unknownReader() throws IOException {
        int mask = 0;
        try {
            reader("nano-j2me-no-such-encoding");
        } catch (UnsupportedEncodingException e) {
            mask |= 1;
        }
        try {
            reader("");
        } catch (UnsupportedEncodingException e) {
            mask |= 2;
        }
        return mask;
    }

    public static int unknownWriter() throws IOException {
        int mask = 0;
        try {
            new OutputStreamWriter(new ByteArrayOutputStream(),
                                   "nano-j2me-no-such-encoding");
        } catch (UnsupportedEncodingException e) {
            mask |= 1;
        }
        try {
            new OutputStreamWriter(new ByteArrayOutputStream(), "");
        } catch (UnsupportedEncodingException e) {
            mask |= 2;
        }
        return mask;
    }

    public static int nullReader() throws IOException {
        try {
            reader(null);
            return 0;
        } catch (NullPointerException e) {
            return 1;
        }
    }

    public static int nullWriter() throws IOException {
        try {
            new OutputStreamWriter(new ByteArrayOutputStream(), (String) null);
            return 0;
        } catch (NullPointerException e) {
            return 1;
        }
    }

    public static int closedReader() throws IOException {
        InputStreamReader r = new InputStreamReader(
                new ByteArrayInputStream(bytes(65)));
        r.close();
        r.close();
        int mask = 0;
        try { r.read(); } catch (IOException e) { mask |= 1; }
        try { r.read(new char[1], 0, 1); }
        catch (IOException e) { mask |= 2; }
        try { r.ready(); } catch (IOException e) { mask |= 4; }
        try { r.skip(1); } catch (IOException e) { mask |= 8; }
        return mask;
    }

    public static int closedWriter() throws IOException {
        OutputStreamWriter w = new OutputStreamWriter(new ByteArrayOutputStream());
        w.close();
        w.close();
        int mask = 0;
        try { w.write(65); } catch (IOException e) { mask |= 1; }
        try { w.write(new char[] { 65 }, 0, 1); }
        catch (IOException e) { mask |= 2; }
        try { w.write("A"); } catch (IOException e) { mask |= 4; }
        try { w.write("XA", 1, 1); } catch (IOException e) { mask |= 8; }
        try { w.flush(); } catch (IOException e) { mask |= 16; }
        return mask;
    }

    public static int utf8Aliases() throws IOException {
        String[] names = { "UTF-8", "UTF8", "uTf-8", "uTf8" };
        int h = 17;
        for (int i = 0; i < names.length; i++) {
            h = h * 31 + readDigest(reader(names[i], 0xc2, 0xa2));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            OutputStreamWriter w = new OutputStreamWriter(out, names[i]);
            w.write(0xa2);
            w.close();
            h = h * 31 + byteDigest(out.toByteArray());
        }
        return h;
    }

    public static int utf8Read() throws IOException {
        return readDigest(reader("UTF-8", 0, 65, 0xc2, 0xa2, 0xe2, 0x82,
                0xac, 0xe4, 0xb8, 0xad, 0xf0, 0x9f, 0x98, 0x80, 0x7f));
    }

    public static int splitSupplementary() throws IOException {
        InputStreamReader r = reader("UTF8", 0xf0, 0x9f, 0x98, 0x80, 65);
        char[] c = { 'x', 'y', 'z' };
        int h = 17;
        for (int i = 0; i < 3; i++) {
            h = h * 31 + r.read(c, 1, 1);
            h = h * 31 + c[1];
            h = h * 31 + c[0];
            h = h * 31 + c[2];
        }
        h = h * 31 + r.read(c, 1, 1);
        r.close();
        return h;
    }

    public static int readyPendingLow() throws IOException {
        InputStreamReader r = reader("UTF-8", 0xf0, 0x9f, 0x98, 0x80);
        int h = 17 * 31 + r.read();
        h = h * 31 + (r.ready() ? 1 : 0);
        h = h * 31 + r.read();
        h = h * 31 + (r.ready() ? 1 : 0);
        h = h * 31 + r.read();
        r.close();
        return h;
    }

    public static int skipPendingLow() throws IOException {
        InputStreamReader r = reader("UTF-8", 0xf0, 0x9f, 0x98, 0x80, 65);
        int h = 17 * 31 + r.read();
        h = h * 31 + (int) r.skip(1);
        h = h * 31 + r.read();
        h = h * 31 + r.read();
        r.close();
        return h;
    }

    public static int utf8WriteOverloads() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF-8");
        w.write(0);
        w.write(0xa2);
        w.write(new char[] { 'x', 0x20ac, 0xd83d, 0xde00, 'y' }, 1, 3);
        w.write("x\u4e2dy", 1, 1);
        w.write("!");
        w.write(new char[] { 0x7f });
        w.flush();
        w.close();
        return byteDigest(out.toByteArray());
    }

    public static int writerCrossCalls() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF8");
        w.write("\ud83d");
        w.flush();
        int h = 17 * 31 + out.size();
        w.write(0xde00);
        w.flush();
        h = h * 31 + out.size();
        w.write(new char[] { 0xd834 }, 0, 1);
        w.flush();
        h = h * 31 + out.size();
        w.write("x\udd1ey", 1, 1);
        w.close();
        return h * 31 + byteDigest(out.toByteArray());
    }

    public static int closeDanglingHigh() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF-8");
        w.write("A\ud83d");
        w.flush();
        int before = out.size();
        w.close();
        return before * 31 + byteDigest(out.toByteArray());
    }

    /* A real Java delegate exposes failures and callbacks that native
     * ByteArrayOutputStream alone cannot exercise. */
    static final class WriterOutput extends OutputStream {
        private final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        private final IOException flushError = new IOException("fixture flush");
        private boolean failFlush;
        private int flushCalls;
        private int closeCalls;
        private OutputStreamWriter writer;
        private boolean reenter;
        private boolean entered;

        WriterOutput(boolean failFlush) {
            this.failFlush = failFlush;
        }

        public void write(int b) throws IOException {
            bytes.write(b);
            if (reenter && !entered) {
                entered = true;
                writer.write('A');
            }
        }

        public void flush() throws IOException {
            flushCalls++;
            if (failFlush) {
                failFlush = false;
                throw flushError;
            }
        }

        public void close() {
            closeCalls++;
        }
    }

    public static int closedEmptyStringWrite() throws IOException {
        OutputStreamWriter w = new OutputStreamWriter(
                new ByteArrayOutputStream(), "UTF-8");
        w.close();
        try {
            w.write("", 0, 0);
            return 0;
        } catch (IOException e) {
            return 1;
        }
    }

    public static int failedCloseWriter() throws IOException {
        WriterOutput out = new WriterOutput(true);
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF-8");
        w.write(0xd83d);
        int mask = out.bytes.size() == 0 ? 1 : 0;
        try { w.close(); }
        catch (IOException e) { if (e == out.flushError) mask |= 2; }
        if (out.flushCalls == 1) mask |= 4;
        if (out.closeCalls == 1) mask |= 8;
        byte[] b = out.bytes.toByteArray();
        if (b.length == 1 && b[0] == '?') mask |= 16;
        try { w.write('A'); } catch (IOException e) { mask |= 32; }
        try { w.write(new char[] { 'A' }, 0, 1); }
        catch (IOException e) { mask |= 64; }
        try { w.write("A"); } catch (IOException e) { mask |= 128; }
        try { w.write("XA", 1, 1); } catch (IOException e) { mask |= 256; }
        try { w.write("", 0, 0); } catch (IOException e) { mask |= 512; }
        try { w.flush(); } catch (IOException e) { mask |= 1024; }
        try { w.close(); mask |= 2048; } catch (IOException e) { }
        if (out.flushCalls == 1 && out.closeCalls == 1
                && out.bytes.size() == 1) mask |= 4096;
        return mask;
    }

    /* OutputStreamWriter buffering changes callback order on desktop. This
     * local-runtime probe requires one completed pair and one reentrant 'A',
     * with no stale high surrogate producing an extra replacement byte. */
    public static int writerReentrantPendingPair() throws IOException {
        WriterOutput out = new WriterOutput(false);
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF8");
        out.writer = w;
        w.write(0xd83d);
        out.reenter = true;
        w.write(0xde00);
        w.flush();
        w.close();
        byte[] b = out.bytes.toByteArray();
        int mask = out.entered ? 1 : 0;
        if (b.length == 5) mask |= 2;
        int a = 0, lead = 0, second = 0, third = 0, fourth = 0;
        for (int i = 0; i < b.length; i++) {
            switch (b[i] & 255) {
            case 'A': a++; break;
            case 0xf0: lead++; break;
            case 0x9f: second++; break;
            case 0x98: third++; break;
            case 0x80: fourth++; break;
            }
        }
        if (a == 1 && lead == 1 && second == 1 && third == 1 && fourth == 1)
            mask |= 4;
        return mask;
    }

    public static int malformedSurrogatesWrite() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out, "UTF-8");
        w.write(new char[] { 0xd83d, 'A', 0xde00, 0xd83d, 0xd83d, 0xde00 });
        w.close();
        return byteDigest(out.toByteArray());
    }

    public static int asciiAliases() throws IOException {
        String[] names = { "ASCII", "US-ASCII", "aScIi", "uS-aScIi" };
        int h = 17;
        for (int i = 0; i < names.length; i++) {
            h = h * 31 + readDigest(reader(names[i], 0, 65, 0x80));
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            OutputStreamWriter w = new OutputStreamWriter(out, names[i]);
            w.write(new char[] { 0, 65, 0x80 });
            w.close();
            h = h * 31 + byteDigest(out.toByteArray());
        }
        return h;
    }

    public static int asciiRead() throws IOException {
        return readDigest(reader("US-ASCII", 0, 0x7f, 0x80, 0xbf, 0xc0, 0xff, 65));
    }

    public static int asciiWrite() throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        OutputStreamWriter w = new OutputStreamWriter(out, "ASCII");
        w.write(new char[] { 0, 0x7f, 0x80, 0x20ac, 0xd83d, 0xde00, 'A', 0xde00 });
        w.close();
        return byteDigest(out.toByteArray());
    }

    /* Separate methods identify the exact malformed-input category when a
     * desktop/runtime result differs. Digests include the ASCII suffix, so
     * consuming the non-continuation byte by mistake cannot pass. */
    public static int invalidLead() throws IOException {
        return readDigest(reader("UTF-8", 0xff, 0x80, 65));
    }

    public static int overlongTwo() throws IOException {
        return readDigest(reader("UTF-8", 0xc0, 0xaf, 65));
    }

    public static int overlongThree() throws IOException {
        return readDigest(reader("UTF-8", 0xe0, 0x80, 0xaf, 65));
    }

    public static int overlongFour() throws IOException {
        return readDigest(reader("UTF-8", 0xf0, 0x80, 0x80, 0xaf, 65));
    }

    public static int truncatedTwo() throws IOException {
        return readDigest(reader("UTF-8", 0xc2));
    }

    public static int truncatedThree() throws IOException {
        return readDigest(reader("UTF-8", 0xe2, 0x82));
    }

    public static int truncatedFour() throws IOException {
        return readDigest(reader("UTF-8", 0xf0, 0x9f, 0x98));
    }

    public static int badContinuationTwo() throws IOException {
        return readDigest(reader("UTF-8", 0xc2, 65, 0xc2, 0xa2));
    }

    public static int badContinuationThree() throws IOException {
        return readDigest(reader("UTF-8", 0xe2, 0x82, 65, 0xc2, 0xa2));
    }

    public static int badContinuationFour() throws IOException {
        return readDigest(reader("UTF-8", 0xf0, 0x9f, 0x98, 65, 0xc2, 0xa2));
    }

    public static int encodedSurrogates() throws IOException {
        return readDigest(reader("UTF-8", 0xed, 0xa0, 0x80,
                                 0xed, 0xbf, 0xbf, 65));
    }

    public static int aboveUnicodeLimit() throws IOException {
        return readDigest(reader("UTF-8", 0xf4, 0x90, 0x80, 0x80, 65));
    }

    public static int modifiedNulRejected() throws IOException {
        return readDigest(reader("UTF-8", 0xc0, 0x80, 0, 65));
    }

    /* With NJ_ENABLE_STREAM_UTF8=0 both readers and writers must reject every
     * tested non-Latin1 spelling. Desktop accepts these; C registers these
     * contract expectations only in the disabled build. */
    public static int disabledUtf8Rejected() throws IOException {
        String[] names = { "UTF-8", "uTf8" };
        int mask = 0;
        for (int i = 0; i < names.length; i++) {
            try { reader(names[i]); }
            catch (UnsupportedEncodingException e) { mask |= 1 << (i * 2); }
            try { new OutputStreamWriter(new ByteArrayOutputStream(), names[i]); }
            catch (UnsupportedEncodingException e) { mask |= 2 << (i * 2); }
        }
        return mask;
    }

    public static int disabledAsciiRejected() throws IOException {
        String[] names = { "US-ASCII", "aScIi" };
        int mask = 0;
        for (int i = 0; i < names.length; i++) {
            try { reader(names[i]); }
            catch (UnsupportedEncodingException e) { mask |= 1 << (i * 2); }
            try { new OutputStreamWriter(new ByteArrayOutputStream(), names[i]); }
            catch (UnsupportedEncodingException e) { mask |= 2 << (i * 2); }
        }
        return mask;
    }
}
