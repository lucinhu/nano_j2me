/* Caller-owned desktop network state; no dependency on SDL or a VM. */
#ifndef NJ_POSIX_NETWORK_H
#define NJ_POSIX_NETWORK_H
#include "nj2me/nj_port.h"
typedef struct nj_posix_socket nj_posix_socket;
typedef struct nj_posix_network {
    nj_posix_socket *sockets;
    int initialized;
    nj_u4 timeout_ms;           /* HTTP total and TCP write deadline, default 30s */
    const char *ca_file;         /* optional caller-owned trusted CA file */
} nj_posix_network;
/* Zero-initialize before first init; repeated init is a no-op. One owning
 * thread per state; destroy after users/VM have stopped using its handles. */
nj_status nj_posix_network_init(nj_posix_network *net);
void nj_posix_network_destroy(nj_posix_network *net);
nj_status nj_posix_http_request(void *ud, const char *url, const char *method,
    const char *headers, const nj_u1 *body, nj_u4 body_len, int *status,
    char *rheaders, nj_size rheaders_cap, nj_u1 **out_body, nj_u4 *out_len,
    void *(*alloc)(void *, nj_u4), void *aud);
void *nj_posix_socket_connect(void *ud, const char *url, nj_u4 timeout_ms,
    nj_socket_info *info, char *err, nj_size err_cap);
int nj_posix_socket_read(void *ud, void *handle, nj_u1 *buf, int len, nj_u4 timeout_ms);
int nj_posix_socket_available(void *ud, void *handle);
int nj_posix_socket_write(void *ud, void *handle, const nj_u1 *buf, int len);
void nj_posix_socket_close(void *ud, void *handle);
#endif
