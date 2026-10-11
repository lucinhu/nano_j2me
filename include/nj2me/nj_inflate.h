/*
 * nano_j2me - nj_inflate.h
 *
 * Self-contained DEFLATE (RFC 1951) and zlib wrapper (RFC 1950) decompressor.
 * No allocation, no libc beyond memcpy/memset: the caller supplies both the
 * source and the destination buffer.
 */
#ifndef NJ_INFLATE_H
#define NJ_INFLATE_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decompress a raw DEFLATE stream.
 *
 *   src, src_len : compressed bytes
 *   dst          : destination buffer
 *   dst_len      : in  = capacity of dst
 *                  out = bytes actually written (on success)
 *                  out = bytes required       (on NJ_ERR_OOM)
 *
 * Returns NJ_OK, or NJ_ERR_FORMAT for corrupt input, or NJ_ERR_OOM when
 * `dst` was too small (so the caller can grow and retry).
 */
nj_status nj_inflate_raw(const void *src, nj_u4 src_len,
                         void *dst, nj_u4 *dst_len);

/*
 * Same, but accepts a zlib stream (2-byte header + adler32 trailer) as well as
 * a raw DEFLATE stream; `src[0]` decides.  Used by the PNG decoder and by any
 * .gz content.
 */
nj_status nj_inflate_zlib(const void *src, nj_u4 src_len,
                          void *dst, nj_u4 *dst_len);

/*
 * Query only the uncompressed size of a zlib stream without producing output
 * (walk the block headers).  Returns NJ_OK and writes *out_size.
 */
nj_status nj_inflate_zlib_size(const void *src, nj_u4 src_len, nj_u4 *out_size);

/* CRC-32 (IEEE) and Adler-32 helpers, used by zip/png validation. */
nj_u4 nj_crc32(const void *data, nj_u4 len, nj_u4 seed);
nj_u4 nj_adler32(const void *data, nj_u4 len);

#ifdef __cplusplus
}
#endif

#endif /* NJ_INFLATE_H */
