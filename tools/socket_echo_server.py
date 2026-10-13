#!/usr/bin/env python3
"""The server the socket fixture talks to.

A line echo with one extra rule: the line "BYE" is answered with "bye" and
then the connection is closed, which is how the fixture gets to see an end of
stream (-1) instead of a timeout.

    python3 tools/socket_echo_server.py [port]     # default 8080, on 127.0.0.1

An Android emulator reaches the host's loopback as 10.0.2.2, which is the URL
the fixture is built with.
"""
import socket
import sys
import threading

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080


def serve(conn: socket.socket) -> None:
    with conn:
        f = conn.makefile("rwb")
        while True:
            line = f.readline()
            if not line:
                return
            text = line.decode("utf-8", "replace").rstrip("\r\n")
            if text == "BYE":
                f.write(b"bye\n")
                f.flush()
                return
            f.write(("echo:" + text + "\n").encode("utf-8"))
            f.flush()


def main() -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.listen(4)
    print(f"socket echo server on 127.0.0.1:{PORT}", flush=True)
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    main()
