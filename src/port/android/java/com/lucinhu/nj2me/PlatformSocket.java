/*
 * nano_j2me - PlatformSocket.java
 *
 * The platform's stream sockets, behind the port's socket hooks.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * javax.microedition.io.SocketConnection is the one network class a J2ME game
 * uses when it wants to talk to its own server rather than a web page: a
 * lobby, a high-score daemon, a two-player game over the internet.  Four of
 * the 436 titles in tests/games open one.  The runtime has no sockets of its
 * own - a TCP stack is not something to grow inside a VM - so the connection
 * is made here, on the platform's stack, and the runtime only sees bytes.
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Statics called on the VM thread, all of them answering values rather than
 * throwing:
 *
 *   connect(url, timeoutMs)  -> id, or 0.  The URL is what the MIDlet passed
 *                               to Connector.open() - "socket://host:port".
 *   address(id) / port(id) / localAddress(id) / localPort(id)
 *                            -> what the MIDlet reads back through
 *                               getAddress()/getPort()/getLocalAddress()/
 *                               getLocalPort().
 *   read(id, buf, len, timeoutMs)
 *                            -> > 0 bytes read, 0 nothing arrived in time,
 *                               -1 the peer closed, -2 the socket failed.
 *                               A timeout is NOT an error: a game polling its
 *                               socket should be able to tell "nothing yet"
 *                               from "gone".
 *   write(id, buf, len)      -> len, or -1.
 *   close(id)                -> releases it.
 *
 * The ids come from a counter that only goes up, so a closed connection's id
 * answers "nothing" instead of answering as a later socket.
 *
 * NOTHING HERE THROWS
 * -------------------
 * Every entry point catches Throwable and logs at most one line per kind; a
 * Java exception crossing JNI would take the VM down, and the game that
 * provoked it is untrusted input.  A host that does not resolve, a port with
 * nothing listening and a server that hangs up mid-read all end as a return
 * value the runtime turns into the MIDlet-visible answer.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.HashMap;

/** Stream sockets for javax.microedition.io.SocketConnection. */
final class PlatformSocket {
    private static final String TAG = "nj2me";

    /*
     * The Android per-connection cap as seen from the runtime's read loop.
     * A MIDlet's read() blocks with no timeout of its own, so the C side asks
     * for a slice and comes back; 30 s is a hung server, not a slow one.
     */
    private static final int MAX_BYTES = 1 << 20;

    private static final class Entry {
        Socket socket;
        InputStream in;
        OutputStream out;
    }

    private static final HashMap<Long, Entry> OPEN = new HashMap<Long, Entry>();
    private static long next = 1;
    private static boolean loggedFailure;

    private PlatformSocket() {
    }

    /** Kept for symmetry with the other bridges: nothing to hold here yet. */
    static void init(Context context) {
        if (context != null) {
            /* A socket does not need a Context; the call exists so that the
             * C side resolves one name for every bridge it talks to. */
            Log.i(TAG, "socket: platform sockets ready");
        }
    }

    /**
     * Connect, and answer the id the other statics take.  The URL is a
     * "socket://host:port" URL exactly as the MIDlet wrote it.
     */
    static synchronized long connect(String url, int timeoutMs) {
        String host = null;
        int port = -1;
        try {
            String rest = url;
            int scheme = rest.indexOf("://");
            if (scheme >= 0) {
                rest = rest.substring(scheme + 3);
            }
            int slash = rest.indexOf('/');
            if (slash >= 0) {
                rest = rest.substring(0, slash);
            }
            int colon = rest.lastIndexOf(':');
            if (colon <= 0 || colon + 1 >= rest.length()) {
                Log.w(TAG, "socket: cannot read a host and port out of " + url);
                return 0;
            }
            host = rest.substring(0, colon);
            port = Integer.parseInt(rest.substring(colon + 1));
        } catch (Throwable t) {
            Log.w(TAG, "socket: cannot read a host and port out of " + url + ": " + t);
            return 0;
        }
        if (port <= 0 || port > 65535) {
            Log.w(TAG, "socket: port " + port + " is not a port");
            return 0;
        }

        try {
            Socket s = new Socket();
            /*
             * TCP_NODELAY: a J2ME game sends small messages and waits for an
             * answer, which is the case Nagle's algorithm turns into a delay.
             */
            s.setTcpNoDelay(true);
            s.setKeepAlive(true);
            s.connect(new InetSocketAddress(host, port), timeoutMs > 0 ? timeoutMs : 30000);
            Entry e = new Entry();
            e.socket = s;
            e.in = s.getInputStream();
            e.out = s.getOutputStream();
            long id;
            synchronized (PlatformSocket.class) {
                id = next++;
                OPEN.put(Long.valueOf(id), e);
            }
            Log.i(TAG, "socket: connected to " + host + ":" + port
                    + " from " + s.getLocalAddress().getHostAddress() + ":" + s.getLocalPort());
            return id;
        } catch (Throwable t) {
            if (!loggedFailure) {
                loggedFailure = true;
                Log.w(TAG, "socket: cannot connect to " + host + ":" + port + ": " + t);
            }
            return 0;
        }
    }

    static synchronized String address(long id) {
        Entry e = OPEN.get(Long.valueOf(id));
        if (e == null || e.socket == null) {
            return null;
        }
        try {
            return e.socket.getInetAddress().getHostAddress();
        } catch (Throwable t) {
            return null;
        }
    }

    static synchronized int port(long id) {
        Entry e = OPEN.get(Long.valueOf(id));
        return e == null ? 0 : e.socket.getPort();
    }

    static synchronized String localAddress(long id) {
        Entry e = OPEN.get(Long.valueOf(id));
        if (e == null || e.socket == null) {
            return null;
        }
        try {
            return e.socket.getLocalAddress().getHostAddress();
        } catch (Throwable t) {
            return null;
        }
    }

    static synchronized int localPort(long id) {
        Entry e = OPEN.get(Long.valueOf(id));
        return e == null ? 0 : e.socket.getLocalPort();
    }

    /**
     * One read.  The C side allocates the buffer and passes its length, which
     * is the size of the MIDlet's own buffer, so a game that reads four bytes
     * at a time costs four bytes a call.
     */
    static synchronized int read(long id, byte[] buf, int len, int timeoutMs) {
        Entry e = OPEN.get(Long.valueOf(id));
        if (e == null || e.in == null) {
            return -2;
        }
        if (buf == null || len <= 0) {
            return 0;
        }
        if (len > MAX_BYTES) {
            len = MAX_BYTES;
        }
        try {
            e.socket.setSoTimeout(timeoutMs > 0 ? timeoutMs : 30000);
            int n = e.in.read(buf, 0, len);
            return n;   /* -1 is the peer's end of stream, and is passed on */
        } catch (SocketTimeoutException te) {
            return 0;   /* nothing arrived: not an error, and not an end */
        } catch (Throwable t) {
            return -2;
        }
    }

    static synchronized int write(long id, byte[] buf, int len) {
        Entry e = OPEN.get(Long.valueOf(id));
        if (e == null || e.out == null || buf == null) {
            return -1;
        }
        if (len <= 0) {
            return 0;
        }
        try {
            e.out.write(buf, 0, len);
            e.out.flush();
            return len;
        } catch (Throwable t) {
            return -1;
        }
    }

    static synchronized void close(long id) {
        Entry e = OPEN.remove(Long.valueOf(id));
        if (e == null) {
            return;
        }
        try {
            if (e.socket != null) {
                e.socket.close();
            }
        } catch (Throwable t) {
            /* Closing a socket that is already gone is not worth a line. */
        }
        e.in = null;
        e.out = null;
        e.socket = null;
    }
}
