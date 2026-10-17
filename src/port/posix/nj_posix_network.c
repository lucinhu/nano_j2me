#define _POSIX_C_SOURCE 200809L
#include "nj_posix_network.h"
#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>

/* libcurl owns DNS, TLS and connection setup. Its global reference count is
 * balanced per port; all application-owned handles live in this state. */
struct nj_posix_socket {
    nj_posix_socket *next;
    CURL *easy;
    curl_socket_t fd;
};
typedef struct response {
    unsigned char *body;
    size_t len, cap;
    char *headers;
    size_t headers_len, headers_cap;
} response;
#define BODY_LIMIT (32u * 1024u * 1024u)

nj_status nj_posix_network_init(nj_posix_network *net)
{
    if (!net) return NJ_ERR;
    if (net->initialized) return NJ_OK;
    memset(net, 0, sizeof(*net));
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return NJ_ERR;
    /* NOSIGNAL cannot bound a synchronous DNS resolver. Refuse that build
     * instead of silently hanging beyond the requested deadline. */
    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS)) {
        curl_global_cleanup();
        return NJ_ERR;
    }
    net->initialized = 1;
    net->timeout_ms = 30000;
    return NJ_OK;
}
void nj_posix_socket_close(void *ud, void *handle)
{
    nj_posix_network *net = ud;
    nj_posix_socket **p;
    if (!net || !handle) return;
    for (p = &net->sockets; *p; p = &(*p)->next) {
        if (*p == handle) {
            nj_posix_socket *s = *p;
            *p = s->next;
            curl_easy_cleanup(s->easy);
            free(s);
            return;
        }
    }
}
void nj_posix_network_destroy(nj_posix_network *net)
{
    if (!net || !net->initialized) return;
    while (net->sockets) nj_posix_socket_close(net, net->sockets);
    curl_global_cleanup();
    net->initialized = 0;
}
static void configure(CURL *c, nj_posix_network *net, nj_u4 timeout)
{
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    long bounded = !timeout || timeout > INT_MAX ? INT_MAX : (long)timeout;
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, bounded);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, bounded);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    /* Environment proxy settings must not redirect raw TCP through HTTP. */
    if (net->ca_file) curl_easy_setopt(c, CURLOPT_CAINFO, net->ca_file);
}
static size_t receive_body(char *data, size_t size, size_t count, void *ud)
{
    response *r = ud;
    size_t n = size * count, cap;
    unsigned char *p;
    if (size && n / size != count) return 0;
    if (n > BODY_LIMIT - r->len) return 0;
    if (r->len + n > r->cap) {
        cap = r->cap ? r->cap * 2 : 4096;
        if (cap < r->len + n) cap = r->len + n;
        p = realloc(r->body, cap);
        if (!p) return 0;
        r->body = p;
        r->cap = cap;
    }
    if (n) memcpy(r->body + r->len, data, n);
    r->len += n;
    return n;
}
static size_t receive_header(char *data, size_t size, size_t count, void *ud)
{
    response *r = ud;
    size_t n = size * count, len = n;
    if (size && n / size != count) return 0;
    /* Drop interim/proxy response headers on each new status line. */
    if (n >= 5 && !memcmp(data, "HTTP/", 5)) {
        r->headers_len = 0;
        if (r->headers_cap) r->headers[0] = 0;
        return n;
    }
    while (len && (data[len-1] == '\r' || data[len-1] == '\n')) --len;
    if (len && memchr(data, ':', len) && r->headers_cap > r->headers_len + len + 1) {
        memcpy(r->headers + r->headers_len, data, len);
        r->headers_len += len;
        r->headers[r->headers_len++] = '\n';
        r->headers[r->headers_len] = 0;
    }
    return n;
}
nj_status nj_posix_http_request(void *ud, const char *url, const char *method,
    const char *headers, const nj_u1 *body, nj_u4 body_len, int *status,
    char *rheaders, nj_size rheaders_cap, nj_u1 **out_body, nj_u4 *out_len,
    void *(*alloc)(void *, nj_u4), void *aud)
{
    nj_posix_network *net = ud;
    CURL *c;
    CURLcode code;
    struct curl_slist *list = NULL;
    response r;
    long http_status = 0;
    nj_status result = NJ_ERR;
    const char *p;
    if (!status || !out_body || !out_len) return NJ_ERR;
    *status = 0; *out_body = NULL; *out_len = 0;
    if (rheaders && rheaders_cap) rheaders[0] = 0;
    if (!net || !net->initialized || !url || !method || !alloc ||
        (body_len && !body) || (!rheaders && rheaders_cap)) return NJ_ERR;
    c = curl_easy_init();
    if (!c) return NJ_ERR_NOMEM;
    memset(&r, 0, sizeof(r));
    r.headers = rheaders; r.headers_cap = rheaders_cap;
    for (p = headers; p && *p;) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end-p) : strlen(p);
        char *line;
        struct curl_slist *next;
        if (n && p[n-1] == '\r') --n;
        line = malloc(n+1);
        if (!line) goto done;
        memcpy(line, p, n); line[n] = 0;
        next = curl_slist_append(list, line);
        free(line);
        if (!next) goto done;
        list = next;
        p = end ? end+1 : p+strlen(p);
    }
    configure(c, net, net->timeout_ms);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(c, CURLOPT_NOBODY, !strcmp(method, "HEAD") ? 1L : 0L);
    if (body_len || !strcmp(method, "POST")) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? (const char *)body : "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    }
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, receive_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r);
    code = curl_easy_perform(c);
    if (code != CURLE_OK) goto done;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status < 100 || http_status > 599) goto done;
    if (r.len) {
        *out_body = alloc(aud, (nj_u4)r.len);
        if (!*out_body) { result = NJ_ERR_NOMEM; goto done; }
        memcpy(*out_body, r.body, r.len);
    }
    *out_len = (nj_u4)r.len;
    *status = (int)http_status;
    result = NJ_OK;
done:
    free(r.body);
    curl_slist_free_all(list);
    curl_easy_cleanup(c);
    return result;
}
static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int wait_fd(curl_socket_t fd, short events, long long deadline)
{
    struct pollfd p;
    p.fd = fd; p.events = events; p.revents = 0;
    for (;;) {
        long long left = deadline ? deadline - now_ms() : INT_MAX;
        int rc;
        if (left <= 0) return 0;
        rc = poll(&p, 1, left > INT_MAX ? INT_MAX : (int)left);
        if (rc < 0 && errno == EINTR) continue;
        return rc;
    }
}
void *nj_posix_socket_connect(void *ud, const char *url, nj_u4 timeout_ms,
    nj_socket_info *info, char *err, nj_size err_cap)
{
    nj_posix_network *net = ud;
    nj_posix_socket *s;
    CURLcode code;
    char *address = NULL;
    long port = 0;
    char target[1024];
    if (err && err_cap) err[0] = 0;
    if (!net || !net->initialized || !url || strncmp(url, "socket://", 9) ||
        strlen(url+9) + 8 >= sizeof(target) || strpbrk(url+9, "/?#@")) {
        if (err && err_cap) snprintf(err, err_cap, "invalid TCP socket URL");
        return NULL;
    }
    snprintf(target, sizeof(target), "http://%s", url+9);
    s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->easy = curl_easy_init();
    if (!s->easy) { free(s); return NULL; }
    configure(s->easy, net, timeout_ms);
    curl_easy_setopt(s->easy, CURLOPT_PROXY, "");
    curl_easy_setopt(s->easy, CURLOPT_URL, target);
    curl_easy_setopt(s->easy, CURLOPT_CONNECT_ONLY, 1L);
    code = curl_easy_perform(s->easy);
    if (code != CURLE_OK || curl_easy_getinfo(s->easy, CURLINFO_ACTIVESOCKET, &s->fd) != CURLE_OK || s->fd == CURL_SOCKET_BAD) {
        if (err && err_cap) snprintf(err, err_cap, "TCP connect: %s", curl_easy_strerror(code));
        curl_easy_cleanup(s->easy); free(s); return NULL;
    }
    if (info) {
        memset(info, 0, sizeof(*info));
        curl_easy_getinfo(s->easy, CURLINFO_PRIMARY_IP, &address);
        if (address) snprintf(info->address, sizeof(info->address), "%s", address);
        curl_easy_getinfo(s->easy, CURLINFO_LOCAL_IP, &address);
        if (address) snprintf(info->local_address, sizeof(info->local_address), "%s", address);
        curl_easy_getinfo(s->easy, CURLINFO_LOCAL_PORT, &port);
        info->local_port = (int)port;
        curl_easy_getinfo(s->easy, CURLINFO_PRIMARY_PORT, &port);
        info->port = (int)port;
    }
    s->next = net->sockets; net->sockets = s;
    return s;
}
int nj_posix_socket_read(void *ud, void *handle, nj_u1 *buf, int len, nj_u4 timeout_ms)
{
    nj_posix_socket *s = handle;
    long long deadline = timeout_ms ? now_ms() + timeout_ms : 0;
    (void)ud;
    if (!s || len < 0 || (!buf && len)) return -1;
    if (!len) return 0;
    for (;;) {
        size_t n = 0;
        CURLcode code = curl_easy_recv(s->easy, buf, (size_t)len, &n);
        int ready;
        if (code == CURLE_OK) return n ? (int)n : -1;
        if (code != CURLE_AGAIN) return -1;
        ready = wait_fd(s->fd, POLLIN, deadline);
        if (ready <= 0) return ready;
    }
}
int nj_posix_socket_available(void *ud, void *handle)
{
    nj_posix_socket *s = handle;
    int n = 0;
    (void)ud;
    if (!s || ioctl(s->fd, FIONREAD, &n) < 0) return -1;
    return n;
}
int nj_posix_socket_write(void *ud, void *handle, const nj_u1 *buf, int len)
{
    nj_posix_network *net = ud;
    nj_posix_socket *s = handle;
    size_t sent = 0;
    long long deadline;
    if (!net || !s || len < 0 || (!buf && len)) return -1;
    deadline = net->timeout_ms ? now_ms() + net->timeout_ms : 0;
    while (sent < (size_t)len) {
        size_t n = 0;
        CURLcode code = curl_easy_send(s->easy, buf+sent, (size_t)len-sent, &n);
        if (code != CURLE_OK && code != CURLE_AGAIN) return -1;
        sent += n;
        if (sent < (size_t)len && wait_fd(s->fd, POLLOUT, deadline) <= 0) return -1;
    }
    return len;
}
