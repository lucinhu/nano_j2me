/*
 * nano_j2me - nj_zip.h
 *
 * Minimal read-only ZIP/JAR reader: central-directory parsing, stored +
 * deflated entries, no allocation beyond the descriptor array the reader
 * keeps for the lifetime of the archive.
 *
 * The archive bytes stay owned by the caller (a memory-mapped file, a flash
 * partition, or a C array); nj_zip never copies the archive.
 */
#ifndef NJ_ZIP_H
#define NJ_ZIP_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NJ_ZIP_NAME_MAX 128

typedef struct nj_zip_entry {
    char   name[NJ_ZIP_NAME_MAX];
    nj_u4  comp_size;
    nj_u4  uncomp_size;
    nj_u2  method;        /* 0 = stored, 8 = deflate */
    nj_u4  local_offset;
    nj_u2  dos_time;
    nj_u2  dos_date;
} nj_zip_entry;

typedef struct nj_zip nj_zip;

/*
 * Allocator hook.
 *
 * The runtime owns no mutable global state, so nj_zip never keeps a static
 * entry pool: the caller supplies the memory strategy.  Pass NULL to use
 * malloc/free (hosted builds), or a pool/arena-backed allocator on an MCU.
 * `free` may be NULL when the caller uses a bump arena and never frees
 * individual blocks.
 */
typedef struct nj_zip_alloc {
    void *(*alloc)(void *ud, nj_size n);
    void  (*free)(void *ud, void *p);
    void   *ud;
} nj_zip_alloc;

/* `data` must remain valid and unchanged until nj_zip_close(). */
nj_zip *nj_zip_open(const void *data, nj_u4 size, const nj_zip_alloc *alloc);
void    nj_zip_close(nj_zip *z);

int                   nj_zip_count(const nj_zip *z);
const nj_zip_entry   *nj_zip_entry_at(const nj_zip *z, int index);
const nj_zip_entry   *nj_zip_find(const nj_zip *z, const char *name);
/* Case-insensitive lookup fallback (JAR entry names are case sensitive, but
 * manifests in the wild are not consistent). */
const nj_zip_entry   *nj_zip_find_ci(const nj_zip *z, const char *name);

/*
 * Extract `e` into `buf` (capacity `buf_size`).
 *   - When `buf` is NULL: returns the uncompressed size and writes nothing.
 *   - When `buf` is too small: returns NJ_ERR_OOM.
 *   - On success returns the number of bytes written (>= 0).
 */
nj_s4 nj_zip_extract(const nj_zip *z, const nj_zip_entry *e,
                     void *buf, nj_u4 buf_size);

/* Convenience: allocate with `za` (or malloc when NULL), extract,
 * NUL-terminate, return buffer.  Caller frees with the same allocator.
 * `*out_len` receives the byte count excluding the terminator. */
void *nj_zip_extract_alloc(const nj_zip *z, const nj_zip_entry *e,
                           const nj_zip_alloc *za,
                           nj_u4 *out_len);

/*
 * Tiny JAR manifest reader.  Scans `data` (a META-INF/MANIFEST.MF payload) for
 * the given attribute in the *main* section, e.g. "MIDlet-1",
 * "MIDlet-Name", "MicroEdition-Profile".
 * Writes a NUL-terminated value into `out` (capacity `out_size`).
 * Returns NJ_OK or NJ_ERR.
 */
nj_status nj_manifest_get(const char *data, nj_u4 len,
                          const char *attr, char *out, nj_u4 out_size);

#ifdef __cplusplus
}
#endif

#endif /* NJ_ZIP_H */
