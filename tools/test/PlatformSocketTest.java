package com.lucinhu.nj2me;

import java.io.File;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;

/** Real loopback regression, runnable on a JDK or Android app_process. */
public final class PlatformSocketTest {
    private static final ExecutorService POOL = Executors.newCachedThreadPool(new ThreadFactory() {
        public Thread newThread(Runnable r) {
            Thread t = new Thread(r, "socket-regression");
            t.setDaemon(true);
            return t;
        }
    });

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static final class Pair implements AutoCloseable {
        final ServerSocket server;
        final Socket peer;
        final long id;
        Pair(String host) throws Exception {
            server = new ServerSocket(0, 8, InetAddress.getByName(host));
            String authority = host.indexOf(':') >= 0 ? "[" + host + "]" : host;
            id = PlatformSocket.connect("socket://" + authority + ":" + server.getLocalPort()
                    + ";keepalive=true/", 1000);
            check(id != 0, "connect " + host);
            server.setSoTimeout(2000);
            peer = server.accept();
            peer.setSoTimeout(2000);
        }
        public void close() throws Exception {
            PlatformSocket.close(id);
            peer.close();
            server.close();
        }
    }

    private static Future<Integer> pendingRead(final long id, final int timeout) throws Exception {
        final java.util.concurrent.atomic.AtomicReference<Thread> reader =
                new java.util.concurrent.atomic.AtomicReference<Thread>();
        Future<Integer> f = POOL.submit(new Callable<Integer>() {
            public Integer call() {
                reader.set(Thread.currentThread());
                return PlatformSocket.read(id, new byte[8], 8, timeout);
            }
        });
        // Wait until read owns readLock; a scheduler delay must not make the test pass.
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (System.nanoTime() < deadline) {
            Thread t = reader.get();
            if (t != null) {
                for (StackTraceElement frame : t.getStackTrace()) {
                    String name = frame.getClassName();
                    if ((name.contains("SocketInputStream") || name.contains("NioSocketImpl"))
                            && !f.isDone()) return f;
                }
            }
            Thread.yield();
        }
        throw new AssertionError("read did not enter blocking socket I/O");
    }

    private static void concurrentReadWrite() throws Exception {
        try (Pair a = new Pair("127.0.0.1"); Pair b = new Pair("127.0.0.1")) {
            Future<Integer> read = pendingRead(a.id, 5000);
            Future<Integer> write = POOL.submit(new Callable<Integer>() {
                public Integer call() { return PlatformSocket.write(a.id, new byte[] {42}, 1); }
            });
            check(write.get(2, TimeUnit.SECONDS) == 1, "write must progress during read");
            check(a.peer.getInputStream().read() == 42, "peer received write");
            b.peer.getOutputStream().write(7);
            check(PlatformSocket.read(b.id, new byte[1], 1, 500) == 1,
                    "other connection must progress during read");
            check(PlatformSocket.available(a.id) == 0, "available must not wait for read");
            a.peer.getOutputStream().write(8);
            check(read.get(2, TimeUnit.SECONDS) == 1, "pending read received response");
        }
        System.out.println("PASS concurrent read/write and independent connections");
    }

    private static void closeInterruptsRead() throws Exception {
        try (Pair p = new Pair("127.0.0.1")) {
            Future<Integer> read = pendingRead(p.id, 30000);
            Future<?> close = POOL.submit(new Runnable() {
                public void run() { PlatformSocket.close(p.id); }
            });
            close.get(2, TimeUnit.SECONDS);
            check(read.get(2, TimeUnit.SECONDS) == -2, "close must interrupt read as failure");
            check(PlatformSocket.available(p.id) == -1, "closed available");
            check(PlatformSocket.write(p.id, new byte[1], 1) == -1, "closed write");
            PlatformSocket.close(p.id);
        }
        System.out.println("PASS close interrupts blocked read and removes handle");
    }

    private static void timeoutEofAvailable(String host) throws Exception {
        try (Pair p = new Pair(host)) {
            check(PlatformSocket.port(p.id) == p.server.getLocalPort(), "remote port");
            check(PlatformSocket.localPort(p.id) > 0, "local port");
            check(PlatformSocket.address(p.id) != null, "address");
            long start = System.nanoTime();
            check(PlatformSocket.read(p.id, new byte[4], 4, 100) == 0, "timeout return");
            long elapsed = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
            check(elapsed >= 50 && elapsed < 2000, "timeout duration " + elapsed);
            check(PlatformSocket.available(p.id) == 0, "empty available");
            p.peer.getOutputStream().write(new byte[] {10, 11, 12});
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
            while (PlatformSocket.available(p.id) != 3 && System.nanoTime() < deadline) Thread.yield();
            check(PlatformSocket.available(p.id) == 3, "queued available");
            byte[] data = new byte[3];
            check(PlatformSocket.read(p.id, data, 3, 500) == 3 && data[2] == 12, "queued bytes");
            check(PlatformSocket.read(p.id, data, 4, 500) == -2, "invalid read bounds");
            check(PlatformSocket.write(p.id, data, 4) == -1, "invalid write bounds");
            p.peer.shutdownOutput();
            check(PlatformSocket.read(p.id, data, 3, 500) == -1, "EOF");
        }
        System.out.println("PASS timeout, EOF, available and URL parameters " + host);
    }

    private static int descriptorCount() {
        String[] descriptors = new File("/proc/self/fd").list();
        return descriptors == null ? -1 : descriptors.length;
    }

    private static void failedConnectCleanup() throws Exception {
        int port;
        try (ServerSocket server = new ServerSocket(0, 8, InetAddress.getByName("127.0.0.1"))) {
            port = server.getLocalPort();
        }
        String url = "socket://127.0.0.1:" + port;
        for (int i = 0; i < 8; i++) check(PlatformSocket.connect(url, 100) == 0, "warmup refusal");
        int before = descriptorCount();
        for (int i = 0; i < 128; i++) check(PlatformSocket.connect(url, 100) == 0, "refused connect");
        int after = descriptorCount();
        check(before < 0 || after <= before + 2, "failed connect leaked descriptors: " + before + " -> " + after);
        String[] malformed = {null, "socket://:80", "socket://[::1:80", "socket://[::1]80",
            "socket://::1:80", "socket://127.0.0.1:0", "socket://127.0.0.1:65536"};
        for (String value : malformed) check(PlatformSocket.connect(value, 100) == 0, "bad URL " + value);
        Field open = PlatformSocket.class.getDeclaredField("OPEN");
        open.setAccessible(true);
        check(((Map<?, ?>) open.get(null)).isEmpty(), "failed connect leaked map entries");
        System.out.println("PASS failed connect cleanup (fd " + before + " -> " + after + ") and invalid URLs");
    }

    public static void main(String[] args) throws Exception {
        try {
            concurrentReadWrite();
            closeInterruptsRead();
            timeoutEofAvailable("127.0.0.1");
            timeoutEofAvailable("::1");
            failedConnectCleanup();
            System.out.println("PlatformSocket regression: PASS");
        } finally {
            POOL.shutdownNow();
        }
    }
}
