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
     * Bound each Java read allocation/work request. The runtime can retry
     * partial reads; timeoutMs controls the separate blocking-time limit.
     */
    private static final int MAX_BYTES = 1 << 20;

    private static final class Entry {
        final Socket socket;
        final InputStream in;
        final OutputStream out;
        final Object readLock = new Object();
        final Object writeLock = new Object();

        Entry(Socket socket) throws java.io.IOException {
            this.socket = socket;
            in = socket.getInputStream();
            out = socket.getOutputStream();
        }
    }

    /* Never hold the map monitor during blocking I/O: reads must not prevent
     * a writer, another connection, or close() from making progress. */
    private static synchronized Entry lookup(long id) {
        return OPEN.get(Long.valueOf(id));
    }

    private static final HashMap<Long, Entry> OPEN = new HashMap<Long, Entry>();
    private static long next = 1;
    private static boolean loggedFailure;

    private PlatformSocket() {
    }

    /** 1 = the game may use the network, 0 = the player's setting says no. */
    private static volatile int allowed = 1;

    /** The player's per-game network setting; see PlatformHttp.setAllowed(). */
    static synchronized void setAllowed(boolean on) {
        allowed = on ? 1 : 0;
        Log.i(TAG, "socket: network " + (on ? "allowed" : "refused")
                + " by this game's settings");
    }

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
    static long connect(String url, int timeoutMs) {
        if (allowed == 0) {
            Log.i(TAG, "socket: " + url + " refused - this game's settings say"
                    + " no network");
            return 0L;
        }
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
            int parameters = rest.indexOf(';');
            if (parameters >= 0) {
                rest = rest.substring(0, parameters);
            }
            int colon;
            if (rest.startsWith("[")) {
                int bracket = rest.indexOf(']');
                if (bracket <= 1 || bracket + 1 >= rest.length()
                        || rest.charAt(bracket + 1) != ':') {
                    return 0;
                }
                host = rest.substring(1, bracket);
                colon = bracket + 1;
            } else {
                colon = rest.indexOf(':');
                if (colon != rest.lastIndexOf(':')) {
                    return 0; // IPv6 literals require brackets.
                }
                host = colon > 0 ? rest.substring(0, colon) : null;
            }
            if (colon <= 0 || colon + 1 >= rest.length()) {
                Log.w(TAG, "socket: cannot read a host and port out of " + url);
                return 0;
            }
            port = Integer.parseInt(rest.substring(colon + 1));
        } catch (Throwable t) {
            Log.w(TAG, "socket: cannot read a host and port out of " + url + ": " + t);
            return 0;
        }
        if (port <= 0 || port > 65535) {
            Log.w(TAG, "socket: port " + port + " is not a port");
            return 0;
        }

        Socket s = null;
        try {
            s = new Socket();
            /*
             * TCP_NODELAY: a J2ME game sends small messages and waits for an
             * answer, which is the case Nagle's algorithm turns into a delay.
             */
            s.setTcpNoDelay(true);
            s.setKeepAlive(true);
            s.connect(new InetSocketAddress(host, port), timeoutMs > 0 ? timeoutMs : 30000);
            Entry e = new Entry(s);
            Log.i(TAG, "socket: connected to " + host + ":" + port
                    + " from " + s.getLocalAddress().getHostAddress() + ":" + s.getLocalPort());
            synchronized (PlatformSocket.class) {
                long id = next++;
                OPEN.put(Long.valueOf(id), e);
                return id;
            }
        } catch (Throwable t) {
            if (s != null) {
                try { s.close(); } catch (Throwable ignored) { }
            }
            synchronized (PlatformSocket.class) {
                if (!loggedFailure) {
                    loggedFailure = true;
                    Log.w(TAG, "socket: cannot connect to " + host + ":" + port + ": " + t);
                }
            }
            return 0;
        }
    }

    static String address(long id) {
        Entry e = lookup(id);
        if (e == null || e.socket == null) {
            return null;
        }
        try {
            return e.socket.getInetAddress().getHostAddress();
        } catch (Throwable t) {
            return null;
        }
    }

    static int port(long id) {
        Entry e = lookup(id);
        return e == null ? 0 : e.socket.getPort();
    }

    static String localAddress(long id) {
        Entry e = lookup(id);
        if (e == null || e.socket == null) {
            return null;
        }
        try {
            return e.socket.getLocalAddress().getHostAddress();
        } catch (Throwable t) {
            return null;
        }
    }

    static int localPort(long id) {
        Entry e = lookup(id);
        return e == null ? 0 : e.socket.getLocalPort();
    }

    /**
     * One read.  The C side allocates the buffer and passes its length, which
     * is the size of the MIDlet's own buffer, so a game that reads four bytes
     * at a time costs four bytes a call.
     */
    static int read(long id, byte[] buf, int len, int timeoutMs) {
        Entry e = lookup(id);
        if (e == null || buf == null || len < 0 || len > buf.length) {
            return -2;
        }
        if (len == 0) {
            return 0;
        }
        len = Math.min(len, MAX_BYTES);
        synchronized (e.readLock) {
            try {
                e.socket.setSoTimeout(timeoutMs > 0 ? timeoutMs : 30000);
                return e.in.read(buf, 0, len); // -1 is EOF.
            } catch (SocketTimeoutException te) {
                return 0; // A timeout is neither EOF nor failure.
            } catch (Throwable t) {
                return -2;
            }
        }
    }

    static int write(long id, byte[] buf, int len) {
        Entry e = lookup(id);
        if (e == null || buf == null || len < 0 || len > buf.length) {
            return -1;
        }
        if (len == 0) {
            return 0;
        }
        synchronized (e.writeLock) {
            try {
                e.out.write(buf, 0, len);
                e.out.flush();
                return len;
            } catch (Throwable t) {
                return -1;
            }
        }
    }

    /** Nonblocking count; do not wait for a pending read's monitor. */
    static int available(long id) {
        Entry e = lookup(id);
        if (e == null) {
            return -1;
        }
        try {
            return e.in.available();
        } catch (Throwable t) {
            return -1;
        }
    }

    static void close(long id) {
        Entry e;
        synchronized (PlatformSocket.class) {
            e = OPEN.remove(Long.valueOf(id));
        }
        if (e == null) {
            return;
        }
        // Never acquire the I/O monitors: Socket.close interrupts blocked I/O.
        try {
            e.socket.close();
        } catch (Throwable t) {
            /* Closing a socket that is already gone is not worth a line. */
        }
    }
}
