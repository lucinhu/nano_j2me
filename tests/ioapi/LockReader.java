package ioapi;

import java.io.IOException;
import java.io.Reader;

/*
 * CLDC Java-level regression fixture for the Reader locking contract: a
 * Reader subclass built through protected Reader()/Reader(Object), which
 * reads the protected Reader.lock field and synchronizes on it.
 *
 * This is the shape de/enough/polish/io/StringReader (bundled in
 * 真幻想三国志完整版_12469.jar) has: its constructor is
 *     aload_0; invokespecial java/io/Reader.<init>:()V
 * and close/mark/read/reset/skip all begin with
 *     aload_0; getfield java/io/Reader.lock:Ljava/lang/Object;; monitorenter
 *
 * Every static method returns an int for the C side to compare with the
 * desktop JVM (see IoApiDesktop).
 */
public final class LockReader extends Reader {
    private final char[] data;
    private int pos;
    private int closes;

    public LockReader() {
        super();
        this.data = new char[] { 'a', 'b', 'c' };
    }

    public LockReader(Object lock) {
        super(lock);
        this.data = new char[] { 'x', 'y' };
    }

    public boolean lockIsSelf() {
        return lock == this;
    }

    public boolean lockIs(Object o) {
        return lock == o;
    }

    public int closes() {
        return closes;
    }

    public static int ctorNoArgSelf() {
        LockReader r = new LockReader();
        return r.lockIsSelf() ? 1 : 0;
    }

    public static int ctorExplicitObject() {
        Object o = new Object();
        LockReader r = new LockReader(o);
        return r.lockIs(o) ? 1 : 0;
    }

    public static int ctorNullRejected() {
        try {
            new LockReader(null);
            return 40;
        } catch (NullPointerException e) {
            return 41;
        }
    }

    /* Three synchronized critical sections, then the close count. */
    public static int syncRead() throws IOException {
        LockReader r = new LockReader();
        int a = r.read();
        int b = r.read();
        r.close();
        return (a << 16) + (b << 8) + r.closes();
    }

    public static int readRange() throws IOException {
        LockReader r = new LockReader();
        char[] c = new char[4];
        int n = r.read(c, 1, 2);
        int ch = r.read();
        return (n << 24) + (c[1] << 8) + ch;
    }

    public int read() throws IOException {
        synchronized (lock) {
            if (pos >= data.length) {
                return -1;
            }
            return data[pos++];
        }
    }

    public int read(char[] c, int off, int len) throws IOException {
        synchronized (lock) {
            if (c == null) {
                throw new NullPointerException();
            }
            if (off < 0 || len < 0 || off + len > c.length) {
                throw new IndexOutOfBoundsException();
            }
            int n = 0;
            while (n < len && pos < data.length) {
                c[off + n] = data[pos++];
                n++;
            }
            return n;
        }
    }

    public void close() throws IOException {
        synchronized (lock) {
            closes++;
        }
    }

    public long skip(long n) throws IOException {
        synchronized (lock) {
            long done = 0;
            while (done < n && pos < data.length) {
                pos++;
                done++;
            }
            return done;
        }
    }
}
