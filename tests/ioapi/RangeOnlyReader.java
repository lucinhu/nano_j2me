package ioapi;

import java.io.IOException;
import java.io.Reader;

/*
 * A Reader subclass that overrides ONLY read(char[],int,int).  CLDC's
 * Reader.read() and Reader.skip() are concrete defaults that must dispatch to
 * that override; treating the inherited lock field as an InputStream would be
 * wrong even though it happens to be one for InputStreamReader.
 */
public final class RangeOnlyReader extends Reader {
    private int calls;
    private boolean first = true;

    public RangeOnlyReader() {
        super();
    }

    public int calls() {
        return calls;
    }

    public static int inheritedRead() throws IOException {
        RangeOnlyReader r = new RangeOnlyReader();
        return r.read();
    }

    public static int inheritedReadTwice() throws IOException {
        RangeOnlyReader r = new RangeOnlyReader();
        int a = r.read();
        int b = r.read();
        return (a << 8) + b;
    }

    public static int inheritedSkip() throws IOException {
        RangeOnlyReader r = new RangeOnlyReader();
        long n = r.skip(1L);
        return (int) n;
    }

    /*
     * CLDC's Reader.ready() has a default body too: a reader with no stream of
     * its own is never "ready" and answers false.  It must not try to talk to
     * the lock object as if it were an InputStream; InputStreamReader is the
     * one that overrides this with a real available() probe.  200 = inherited
     * default returned false, 201 = true, 40 = it threw.
     */
    public static int inheritedReady() {
        try {
            RangeOnlyReader r = new RangeOnlyReader();
            return r.ready() ? 201 : 200;
        } catch (Throwable t) {
            return 40;
        }
    }

    /*
     * Deliberately unrelated to Reader: it is legal for a subclass to add a
     * method called available(), and the inherited Reader.ready() default must
     * still answer false.  If the default were implemented by probing the lock
     * object as an InputStream, this method would turn ready() into true; the
     * desktop oracle says it stays false.
     */
    public int available() {
        return 5;
    }

    public int read(char[] c, int off, int len) throws IOException {
        calls++;
        if (!first) {
            return -1;
        }
        first = false;
        c[off] = 'Q';
        return 1;
    }

    public void close() {
    }
}
