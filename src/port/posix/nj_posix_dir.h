/* Shared directory callbacks for the headless and SDL2 desktop ports. */
#ifndef NJ_POSIX_DIR_H
#define NJ_POSIX_DIR_H

#include "nj2me/nj_port.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct nj_posix_dir {
    DIR *dir;
    char path[1024];
} nj_posix_dir;

static int nj_posix_dir_open(void *ud, const char *path, void **out_handle)
{
    nj_posix_dir *h;
    (void)ud;
    if (!path || !out_handle || strlen(path) >= sizeof(h->path)) return -1;
    *out_handle = NULL;
    h = (nj_posix_dir *)malloc(sizeof(*h));
    if (!h) return -1;
    h->dir = opendir(path);
    if (!h->dir) { free(h); return -1; }
    strcpy(h->path, path);
    *out_handle = h;
    return 0;
}

static int nj_posix_dir_next(void *ud, void *handle, char *name, nj_size cap,
                             int *is_dir)
{
    nj_posix_dir *h = (nj_posix_dir *)handle;
    struct dirent *entry;
    (void)ud;
    if (!h || !name || cap == 0u) return -1;
    for (;;) {
        size_t n;
        errno = 0;
        entry = readdir(h->dir);
        if (!entry) return errno ? -1 : 0;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        n = strlen(entry->d_name);
        if (n >= cap) return -1;
        memcpy(name, entry->d_name, n + 1u);
        if (is_dir) {
            char full[2048];
            struct stat st;
            int len = snprintf(full, sizeof(full), "%s/%s", h->path, name);
            *is_dir = len > 0 && (size_t)len < sizeof(full) &&
                      stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }
        return 1;
    }
}

static void nj_posix_dir_close(void *ud, void *handle)
{
    nj_posix_dir *h = (nj_posix_dir *)handle;
    (void)ud;
    if (h) {
        closedir(h->dir);
        free(h);
    }
}

#endif
