#!/usr/bin/env python3
"""Hermetic desktop backend test: HTTP, trusted/untrusted TLS, raw TCP.
All listeners bind loopback ephemeral ports; no game or external endpoint.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import pathlib
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_HEAD(self):
        self.reply(b"")

    def do_POST(self):
        assert self.headers.get("X-Probe") == "loopback"
        self.reply(self.rfile.read(int(self.headers["Content-Length"])), 201)

    def do_GET(self):
        if self.path == "/stall":
            time.sleep(0.3)
            return
        if self.path == "/chunked":
            self.send_response(200)
            self.send_header("X-Reply", "loopback")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            self.wfile.write(b"7\r\nchunked\r\n5\r\n-body\r\n0\r\n\r\n")
            return
        self.reply(b"missing" if self.path == "/missing" else b"hello",
                   404 if self.path == "/missing" else 200)

    def reply(self, data, status=200):
        self.send_response(status)
        self.send_header("X-Reply", "loopback")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main():
    if not shutil.which("openssl"):
        print("SKIP: openssl required for local TLS certificate")
        return 77
    errors = []
    closed = threading.Event()
    with tempfile.TemporaryDirectory(prefix="nj-network-") as tmp:
        cert = pathlib.Path(tmp) / "cert.pem"
        key = pathlib.Path(tmp) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048",
                        "-nodes", "-days", "1", "-subj", "/CN=localhost",
                        "-addext", "subjectAltName=IP:127.0.0.1",
                        "-keyout", str(key), "-out", str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        http = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        https = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        https.socket = tls.wrap_socket(https.socket, server_side=True)
        tcp = socket.socket()
        tcp.bind(("127.0.0.1", 0))
        tcp.listen(2)
        tcp.settimeout(10)

        def echo():
            try:
                conn, _ = tcp.accept()
                with conn:
                    conn.settimeout(8)
                    # Small chunks and delayed receiving exercise partial sends.
                    time.sleep(0.1)
                    total = 0
                    while total < 4 * 1024 * 1024:
                        data = conn.recv(4096)
                        assert data and data == b"x" * len(data)
                        total += len(data)
                    conn.sendall(str(total).encode())
                conn, _ = tcp.accept()
                with conn:
                    conn.settimeout(3)
                    assert conn.recv(1) == b"", "shutdown leaked an open TCP handle"
                closed.set()
                conn, _ = tcp.accept()
                with conn:
                    # The final client must hit its write deadline under backpressure.
                    time.sleep(0.4)
            except Exception as exc:
                errors.append(repr(exc))

        threads = [threading.Thread(target=http.serve_forever, daemon=True),
                   threading.Thread(target=https.serve_forever, daemon=True),
                   threading.Thread(target=echo, daemon=True)]
        for thread in threads:
            thread.start()
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
        try:
            result = subprocess.run([sys.argv[1],
                f"http://127.0.0.1:{http.server_port}",
                f"https://127.0.0.1:{https.server_port}",
                f"socket://127.0.0.1:{tcp.getsockname()[1]}", str(cert)], env=env, timeout=15)
            if result.returncode:
                return result.returncode
            if not closed.wait(2) or errors:
                print("TCP cleanup fixture failed:", errors)
                return 1
            return 0
        finally:
            http.shutdown()
            https.shutdown()
            http.server_close()
            https.server_close()
            tcp.close()
            for thread in threads:
                thread.join(timeout=1)


if __name__ == "__main__":
    sys.exit(main())
