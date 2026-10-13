/*
 * nano_j2me - PlatformHttp.java
 *
 * The platform's HTTP stack, behind the port's connection hook.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * MIDP 2.0 makes javax.microedition.io.HttpConnection mandatory, and a J2ME
 * title uses it for two things above all: a high-score table that is POSTed
 * when a game ends, and an advert or "more games" banner fetched from a URL
 * baked into the JAR.  Of the 436 titles in tests/games, 99 reference
 * javax/microedition/io/HttpConnection.  A port that cannot answer that
 * class does not fail loudly - it fails as a score that never uploads and a
 * banner that never appears - so those calls are answered here, from the
 * platform's own stack.
 *
 * The runtime has no network stack of its own and no business growing one:
 * Android already has HTTP, with the device's proxies, its DNS and its TLS
 * trust store, and HttpURLConnection is what reaches it.  No third-party
 * client, no androidx, nothing to keep in step with a library.
 *
 * WHY THE WHOLE RESPONSE IS BUFFERED
 * ----------------------------------
 * The port hook is synchronous: the C side calls request() on the VM thread,
 * in the middle of a MIDlet's open()/getResponseCode(), and what it gets
 * back is bytes, not a stream.  So a request runs to completion here and the
 * whole body is read into one array that the C side copies across JNI.  That
 * is also why there is a cap: the runtime allocates the whole body in its
 * own heap, so a server that never stops sending is a device that runs out
 * of memory.  A response past 4 MiB is refused - one log line, answered with
 * 0 - rather than buffered and then thrown away.
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Six statics, called on the VM thread: request() performs a whole exchange
 * and answers an id, status()/headers()/body() read that id back, and
 * close() releases it.  The ids come from a counter that only goes up, so a
 * stale id answers "nothing" instead of answering as a later request.
 * headers() is a text block, one "Name: value" line per value, each line
 * ending in '\n', because that is the shape the runtime's reader wants; the
 * status line is not part of it, because status() carries the code.  A
 * failure is 0, 0, null, null - a request that did not happen, never an
 * exception.
 *
 * NOTHING HERE THROWS
 * -------------------
 * Every entry point catches Throwable and logs at most one line per kind,
 * and the network work runs under a ten-second connect timeout and a
 * twenty-second read timeout.  A Java exception crossing JNI would take the
 * VM down, and the JAR that provoked it is untrusted input; a game whose
 * high-score POST fails must get a failed connection and carry on playing.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

final class PlatformHttp {

    private static final String TAG = "nj2me";

    /* What the port calls itself when the game does not say. */
    private static final String DEFAULT_USER_AGENT = "nano_j2me/0.1";

    /*
     * A high-score POST and a banner fetch are both small and a phone is not
     * always on a good network.  Ten seconds to reach the server and twenty
     * to hear the rest of it is a player who gives up, not a hang; what
     * matters on the VM thread is that neither is infinite, and the
     * platform's own defaults are not a promise.
     */
    private static final int CONNECT_TIMEOUT_MS = 10000;
    private static final int READ_TIMEOUT_MS = 20000;

    /*
     * The runtime allocates the whole body in its own heap (see the header),
     * and the title asking for it is untrusted, so a response is buffered
     * only up to this.  4 MiB is already past any high-score table or advert;
     * the cap exists so a broken or hostile server cannot ask the device for
     * an allocation the game never meant to make.
     */
    private static final int BODY_CAP = 4 * 1024 * 1024;

    /* Per read, so the response size never decides the scratch allocation. */
    private static final int COPY_CHUNK = 16 * 1024;

    /* See logOnce(). */
    private static final int LOG_ONCE_MAX = 8;
    private static final Set<String> LOGGED =
            Collections.synchronizedSet(new HashSet<String>());

    /*
     * The application context, never an Activity: a static field that held an
     * Activity would keep a destroyed screen (and its whole view tree) alive
     * for the life of the process.  HttpURLConnection needs no context of its
     * own - the field is what makes init() meaningful rather than
     * decorative, and refusing to work before init() turns a forgotten call
     * into one clear log line instead of a namespace that silently has no
     * network.
     */
    private static Context appContext;

    /*
     * The responses the C side has not read yet, keyed by an id from a
     * counter that only goes up.  Nothing here caps the table: the runtime
     * asks for a connection, reads it and closes it, so close() is what frees
     * a body, and an id is never reused - a stale one answers "nothing"
     * rather than answering as somebody else's request.
     */
    private static final Map<Long, Entry> RESPONSES = new HashMap<Long, Entry>();
    private static long nextId = 0L;

    private PlatformHttp() {
    }

    /* Called once from the Activity, before the VM can ask for a connection. */
    static synchronized void init(Context context) {
        try {
            if (context != null) {
                appContext = context.getApplicationContext();
            }
        } catch (Throwable ignored) {
            /* nothing to bind */
        }
    }

    /**
     * One whole HTTP exchange, performed now on this thread, answered as an
     * id - or 0, which the C side reads as "no connection was made".
     *
     * <p>{@code method} defaults to GET when it is null or empty.
     * {@code headers} is a text block of "Name: value" lines and may be null;
     * {@code body} is written as the request body when it is not null.
     *
     * <p>The response is buffered whole: see the header for why, and
     * {@link #BODY_CAP} for the one size that is refused.
     */
    static synchronized long request(String url, String method, String headers,
                                     byte[] body) {
        if (!ready()) {
            return 0L;
        }
        HttpURLConnection conn = null;
        try {
            /*
             * The cast is the contract: openConnection() on an http/https URL
             * answers an HttpURLConnection.  A URL of some other scheme a game
             * made up fails here as a ClassCastException, and that is the
             * right answer - this port speaks HTTP.
             */
            conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setRequestMethod(method == null || method.length() == 0
                    ? "GET" : method);
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);

            sendHeaders(conn, headers);

            if (body != null) {
                conn.setDoOutput(true);
                OutputStream out = conn.getOutputStream();
                try {
                    out.write(body);
                } finally {
                    try {
                        out.close();
                    } catch (Throwable ignored) {
                        /* the bytes are written; a close that fails has
                         * nothing left to say */
                    }
                }
            }

            /*
             * getResponseCode() is what performs the request: by the time it
             * returns, the connection is open, the request is sent and the
             * status line has been read.
             */
            int code = conn.getResponseCode();

            /*
             * A status past 400 is not a failure to the runtime - it is a
             * response with a code, and servers put the useful part of a
             * refusal in its body - so the error stream is read when the
             * platform has one.  getInputStream() must not be called for it:
             * the platform answers that with FileNotFoundException, which
             * would throw the page away.
             */
            byte[] payload;
            if (code >= 400) {
                InputStream err = conn.getErrorStream();
                payload = err == null ? new byte[0] : readBody(err);
            } else {
                payload = readBody(conn.getInputStream());
            }
            if (payload == null) {
                return 0L;      /* past the cap; readBody() has logged why */
            }

            long id = ++nextId;
            RESPONSES.put(Long.valueOf(id),
                    new Entry(code, headerBlock(conn), payload));
            return id;
        } catch (Throwable t) {
            logOnce("request:" + t.getClass().getName(),
                    "the request failed (" + t + ")");
            return 0L;
        } finally {
            /*
             * The body is in memory by now, so the socket has nothing left to
             * give: release it here rather than park a connection for a game
             * that may never ask for another one.  A disconnect that fails
             * cannot change the result.
             */
            if (conn != null) {
                try {
                    conn.disconnect();
                } catch (Throwable ignored) {
                    /* nothing left to release */
                }
            }
        }
    }

    /* The status code of a response id, or 0 when there is no such response. */
    static synchronized int status(long id) {
        try {
            Entry entry = RESPONSES.get(Long.valueOf(id));
            return entry == null ? 0 : entry.status;
        } catch (Throwable t) {
            logLookup(t);
            return 0;
        }
    }

    /**
     * The response header block - "Name: value\n" per line, one line per
     * value, each line complete - or null when the response had no headers.
     */
    static synchronized String headers(long id) {
        try {
            Entry entry = RESPONSES.get(Long.valueOf(id));
            return entry == null ? null : entry.headers;
        } catch (Throwable t) {
            logLookup(t);
            return null;
        }
    }

    /* The whole response body; an empty array when the body was empty, null
     * when there is no such response. */
    static synchronized byte[] body(long id) {
        try {
            Entry entry = RESPONSES.get(Long.valueOf(id));
            return entry == null ? null : entry.body;
        } catch (Throwable t) {
            logLookup(t);
            return null;
        }
    }

    /* Release one response.  Closing an id twice, or one that never existed,
     * is not an error. */
    static synchronized void close(long id) {
        try {
            RESPONSES.remove(Long.valueOf(id));
        } catch (Throwable t) {
            logOnce("close:" + t.getClass().getName(),
                    "releasing a response failed (" + t + ")");
        }
    }

    /*
     * Is the port wired up?  Called before anything else so a forgotten
     * init() is one log line, not a game that silently has no network.
     */
    private static boolean ready() {
        if (appContext != null) {
            return true;
        }
        logOnce("no-init", "PlatformHttp.init() was never called; refusing to"
                + " connect until the Activity hands over an application"
                + " context");
        return false;
    }

    /*
     * The request header block: one "Name: value" per line, which is exactly
     * what the C side collected from HttpConnection.setRequestProperty().
     * The first ':' separates the two, so a value may contain as many colons
     * as it likes (a URL, a timestamp).  A line with no name, and a header
     * the platform refuses (a reserved name, a value with a control character
     * in it), cost that one header and not the request: a high-score POST
     * with a bad extra header is still a high-score POST.
     */
    private static void sendHeaders(HttpURLConnection conn, String headers) {
        boolean userAgent = false;
        if (headers != null) {
            int start = 0;
            while (start < headers.length()) {
                int end = headers.indexOf('\n', start);
                if (end < 0) {
                    end = headers.length();
                }
                String line = headers.substring(start, end).trim();
                start = end + 1;
                if (line.length() == 0) {
                    continue;
                }
                int colon = line.indexOf(':');
                if (colon <= 0) {
                    logOnce("header-line", "ignoring a request header with no"
                            + " name and value in it");
                    continue;
                }
                String name = line.substring(0, colon).trim();
                String value = line.substring(colon + 1).trim();
                if (name.length() == 0) {
                    continue;
                }
                if ("User-Agent".equalsIgnoreCase(name)) {
                    userAgent = true;
                }
                try {
                    conn.setRequestProperty(name, value);
                } catch (Throwable t) {
                    logOnce("header:" + t.getClass().getName(),
                            "the platform refused the request header " + name);
                }
            }
        }
        if (!userAgent) {
            conn.setRequestProperty("User-Agent", DEFAULT_USER_AGENT);
        }
    }

    /*
     * The whole body, or null when it is past BODY_CAP - which is a refusal
     * and not a truncation: half a high-score table is worse than none.  Both
     * ends of the stream are closed here, in a finally, so no path leaves a
     * connection pinned under a half-read response.
     */
    private static byte[] readBody(InputStream in) throws Exception {
        if (in == null) {
            return new byte[0];
        }
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream(COPY_CHUNK);
            byte[] chunk = new byte[COPY_CHUNK];
            int n = in.read(chunk);
            while (n > 0) {
                if (out.size() + n > BODY_CAP) {
                    logOnce("cap", "refusing a response larger than " + BODY_CAP
                            + " bytes: the runtime allocates the whole body in"
                            + " its own heap");
                    return null;
                }
                out.write(chunk, 0, n);
                n = in.read(chunk);
            }
            return out.toByteArray();
        } finally {
            try {
                in.close();
            } catch (Throwable ignored) {
                /* nothing left to read */
            }
        }
    }

    /*
     * The response header block: one "Name: value" line per value, every line
     * terminated with '\n', so a reader that splits on the newline never sees
     * a partial line at the end.  The status line arrives under a null key
     * and is skipped - status() is where the code lives - and a field with no
     * values contributes nothing rather than an empty line.
     *
     * The platform hands the fields over in a sorted, case-insensitive map,
     * and the block is built once and stored, so its order is fixed for the
     * life of the entry.
     */
    private static String headerBlock(HttpURLConnection conn) {
        Map<String, List<String>> fields = conn.getHeaderFields();
        if (fields == null) {
            return null;
        }
        StringBuilder out = new StringBuilder();
        for (Map.Entry<String, List<String>> field : fields.entrySet()) {
            String name = field.getKey();
            if (name == null) {
                continue;
            }
            List<String> values = field.getValue();
            if (values == null) {
                continue;
            }
            for (int i = 0; i < values.size(); i++) {
                String value = values.get(i);
                if (value == null) {
                    continue;
                }
                out.append(name).append(": ").append(value).append('\n');
            }
        }
        return out.length() == 0 ? null : out.toString();
    }

    private static void logLookup(Throwable t) {
        logOnce("lookup:" + t.getClass().getName(),
                "the response table lookup failed (" + t + ")");
    }

    /*
     * A game that cannot reach its high-score server retries - on every game
     * over, every menu redraw - so a failure is logged once per kind, never
     * once per call.  The set is capped because a hostile JAR must not be
     * able to grow a static field one distinct failure at a time.
     */
    private static void logOnce(String kind, String detail) {
        synchronized (LOGGED) {
            if (LOGGED.size() >= LOG_ONCE_MAX || !LOGGED.add(kind)) {
                return;
            }
        }
        Log.w(TAG, "http: " + detail);
    }

    /*
     * One response the C side has not read yet: the code, the header block
     * and the body, all of them already in memory by the time the id is
     * handed out.  Nothing here touches the connection again, so the entry
     * outlives it - which is what lets the JNI side read the three parts
     * whenever it likes between request() and close().
     */
    private static final class Entry {

        final int status;
        final String headers;
        final byte[] body;

        Entry(int status, String headers, byte[] body) {
            this.status = status;
            this.headers = headers;
            this.body = body;
        }
    }
}
