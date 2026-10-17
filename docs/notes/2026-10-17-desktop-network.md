# Desktop networking and socket polling (2026-10-17)

Downloaded network clients exposed two separate gaps: desktop ports supplied no HTTP/TCP callbacks, and `SocketInputStream.available()` always returned zero. A client that polls before reading could therefore wait indefinitely even after data arrived.

## Changes

- The POSIX backend supplies HTTP/HTTPS and `socket://` TCP through libcurl. SDL2 enables it at port initialization; headless stays offline by default and opts in with `--network` or `nj_port_headless_enable_network()`.
- `socket_available` is an optional port hook. A port can report queued bytes without consuming them; ports without it retain a conservative zero. Closed streams/connections and backend errors throw `IOException`.
- Host requests and socket operations have deadlines. TLS peer/hostname verification remains enabled. Socket writes retain the port's all-bytes-or-error contract; partial writes must never appear successful.
- Shutdown closes tracked sockets. Repeated headless enable calls preserve existing connections.
- `NJ_BUILD_POSIX_NETWORK=OFF` removes the libcurl backend and dependency. Missing libcurl >= 7.85 development files also leaves the optional backend disabled. An asynchronous DNS resolver is required so DNS does not defeat configured request deadlines.
- In this x86-64 Release build the backend object adds 5,641 bytes of text, zero data and zero BSS. This excludes wrappers and libcurl's shared dependencies. HTTP buffers are allocated per request with a 32 MiB body limit; callers with small heaps still need to account for native temporary buffers as well as Java heap usage.

## Validation

The controlled loopback suite (`test_posix_network`) exercises HTTP GET/POST/HEAD, 404 bodies, chunked transfer, request/response headers, TLS trust rejection and a trusted local CA, request timeout, 4 MiB TCP transfer, EOF/reopen, shutdown cleanup, repeated initialization, and write timeout against a peer that does not read. It also checks headless's offline default and explicit activation. A VM-level loop crosses `Connector.open()` → `HttpConnection` → input/output streams against the real local HTTP backend, checking request headers, POST body delivery, 200/201/404 status codes, response bytes, EOF and close.

`test_socket_api` calls the Java Connector/InputStream APIs with a deterministic backend: repeated `available()` returns the same byte count without a read, reading changes the count, zero means no queued bytes, negative backend results throw, and both stream and connection closure are respected. `check_socket` continues to test the offline headless case.

Integrated validation: all three tests passed in the main Release build (2.66 seconds total). The integrated Release build subsequently passed 83/83 CTest cases in 48.46 seconds, including the camera and native text-input regressions. The initial Python fixture name-shadowing failure was fixed before these successful runs.

## Scope

This proves local transport behavior. It does not establish that historical game servers, WAP gateways, login services, accounts, or billing systems still work. Network games must be evaluated with their actual requests and UI progression. `socket://` is TCP; datagram, secure socket, Bluetooth, SMS and game-specific service protocols are separate capabilities.
