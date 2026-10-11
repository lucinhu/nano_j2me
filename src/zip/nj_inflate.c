/*
 * nano_j2me - nj_inflate.c
 *
 * Self-contained RFC 1951 (DEFLATE) and RFC 1950 (zlib) decompressor.
 *
 * Design notes
 * ------------
 *  - Zero heap usage and zero mutable global state: every code table is either
 *    a read-only `static const` literal or is built on the caller's stack.
 *    The three Huffman descriptors plus the code-length scratch space fit in
 *    roughly 2.5 KB of stack.
 *  - The API decompresses a whole buffer in one call, so no 32 KB sliding
 *    window is required: back references are resolved directly against the
 *    bytes already produced in `dst`.  Matches are copied one byte at a time,
 *    which makes overlapping matches (distance < length, e.g. run-length
 *    encoding with distance 1) come out right for free.
 *  - When `dst` overflows we keep decoding but stop storing: the number of
 *    bytes the stream *would* have produced is then reported exactly, so the
 *    caller can grow-and-retry (see the nj_inflate_raw() contract).
 *  - Every input read is bounds checked.  A malformed or truncated stream
 *    yields NJ_ERR_FORMAT instead of touching memory outside the caller's
 *    buffers: this parser is fed untrusted game JARs.
 *
 * The Huffman decoder is written from scratch and uses the canonical
 * (per-length count + sorted symbol) representation, which needs no 32 KB
 * lookup table and therefore no allocation.
 */

#include "nj2me/nj_inflate.h"

#include <string.h>   /* memcpy only; <string.h> is available freestanding */

/* ------------------------------------------------------------------ */
/* Limits and constant tables (read-only, .rodata)                     */
/* ------------------------------------------------------------------ */

#define NJ_HUFF_MAXBITS 15   /* longest DEFLATE code              */
#define NJ_LITLEN_MAX   288  /* literal/length alphabet size      */
#define NJ_DIST_MAX     30   /* distance alphabet size (0..29)    */
#define NJ_CLEN_MAX     19   /* code-length alphabet size         */
#define NJ_LEN_SYMS     29   /* length symbols 257..285           */

/* Length codes 257..285: base length and number of extra bits. */
static const nj_u2 s_len_base[NJ_LEN_SYMS] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const nj_u1 s_len_extra[NJ_LEN_SYMS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

/* Distance codes 0..29: base distance and number of extra bits. */
static const nj_u2 s_dist_base[NJ_DIST_MAX] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};

static const nj_u1 s_dist_extra[NJ_DIST_MAX] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Order in which the code-length code lengths are stored (RFC 1951 3.2.7). */
static const nj_u1 s_clen_order[NJ_CLEN_MAX] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/*
 * CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), 256-entry
 * slice-by-one table.  Precomputed so the module keeps no writable state:
 * a lazily initialised table would live in .data/.bss.
 */
static const nj_u4 s_crc_table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu,
    0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u,
    0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
    0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u,
    0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu,
    0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu,
    0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u,
    0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
    0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu,
    0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u,
    0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u,
    0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu,
    0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du,
    0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au,
    0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u,
    0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u,
    0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
    0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u,
    0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu,
    0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u,
    0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu,
    0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
    0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u,
    0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u,
    0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u,
    0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu,
    0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
    0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u,
    0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u,
    0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu,
    0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u,
    0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
    0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u,
    0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u,
    0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u,
    0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u,
    0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu,
    0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u,
    0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au,
    0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u,
    0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
    0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u,
    0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu,
    0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu,
    0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u,
    0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
    0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu,
    0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u,
    0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u,
    0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu,
    0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
    0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

/* ------------------------------------------------------------------ */
/* Bit reader                                                          */
/* ------------------------------------------------------------------ */

/*
 * DEFLATE packs bits into bytes starting at the least significant bit.  The
 * reader keeps at most 23 bits live (15 bits of lookahead plus the reload
 * granularity of 8 bits), so a 32-bit accumulator is always enough.
 */
typedef struct {
    const nj_u1 *src;
    nj_u4        len;
    nj_u4        pos;
    nj_u4        buf;   /* pending bits, LSB = next bit */
    int          cnt;   /* number of valid bits in buf  */
    int          err;   /* sticky: ran off the end of src */
} nj_bits;

/* Make sure at least `need` bits are buffered (missing bytes read as zero and
 * latch the sticky error flag). */
static void bits_fill(nj_bits *b, int need)
{
    while (b->cnt < need) {
        nj_u4 byte = 0u;
        if (b->pos < b->len) {
            byte = b->src[b->pos];
            b->pos++;
        } else {
            b->err = 1;
        }
        b->buf |= byte << b->cnt;
        b->cnt += 8;
    }
}

/* Consume `need` bits (0 <= need <= 16) and return them, LSB first. */
static nj_u4 bits_get(nj_bits *b, int need)
{
    nj_u4 v;
    if (need <= 0) {
        return 0u;
    }
    bits_fill(b, need);
    v = b->buf & ((1u << need) - 1u);
    b->buf >>= need;
    b->cnt -= need;
    return v;
}

/* Discard bits up to the next byte boundary (used by stored blocks). */
static void bits_align(nj_bits *b)
{
    while ((b->cnt & 7) != 0) {
        (void)bits_get(b, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Canonical Huffman decoder                                           */
/* ------------------------------------------------------------------ */

/*
 * A code is described by the number of codes of each bit length plus the
 * symbols ordered by (length, symbol) -- the canonical ordering of RFC 1951
 * 3.2.2.  Decoding walks one bit at a time and compares against the first code
 * of each length, which costs no memory beyond these two small arrays.
 */
typedef struct {
    nj_u2 count[NJ_HUFF_MAXBITS + 1];
    nj_u2 sym[NJ_LITLEN_MAX];
} nj_huff;

/*
 * Build the canonical description from a code-length vector.
 *
 * Returns 0 for a complete code, a positive count of unused codewords for an
 * incomplete one (decodable, but some bit patterns are invalid) and -1 when
 * the lengths are over-subscribed or out of range.  An alphabet whose lengths
 * are all zero is "complete" but decodes nothing; decode() fails cleanly on it.
 *
 * Incompleteness is not automatically an error: the fixed distance code of
 * RFC 1951 3.2.6 (30 codes of 5 bits) is deliberately incomplete, and so is
 * the degenerate single-symbol distance code that encoders emit for blocks
 * without matches.  Each caller applies its own policy.
 */
static int huff_build(nj_huff *h, const nj_u1 *lens, int n)
{
    int len;
    int sym;
    int left;
    nj_u2 offs[NJ_HUFF_MAXBITS + 2];

    for (len = 0; len <= NJ_HUFF_MAXBITS; len++) {
        h->count[len] = 0;
    }
    for (sym = 0; sym < n; sym++) {
        if (lens[sym] > (nj_u1)NJ_HUFF_MAXBITS) {
            return -1;
        }
        h->count[lens[sym]]++;
    }
    if (h->count[0] == (nj_u2)n) {
        return 0;                       /* empty alphabet */
    }

    /* Kraft inequality: over-subscribed codes cannot be decoded. */
    left = 1;
    for (len = 1; len <= NJ_HUFF_MAXBITS; len++) {
        left <<= 1;
        left -= (int)h->count[len];
        if (left < 0) {
            return -1;
        }
    }

    /* Canonical ordering: sort symbols by length, then by symbol value. */
    offs[1] = 0;
    for (len = 1; len < NJ_HUFF_MAXBITS; len++) {
        offs[len + 1] = (nj_u2)(offs[len] + h->count[len]);
    }
    for (sym = 0; sym < n; sym++) {
        if (lens[sym] != 0) {
            h->sym[offs[lens[sym]]] = (nj_u2)sym;
            offs[lens[sym]]++;
        }
    }
    return left;
}

/*
 * An incomplete alphabet is acceptable when every symbol it does define has a
 * one-bit code (at most two codes): that is the "no distances used" case of
 * RFC 1951 and the shape real encoders emit.
 */
static int huff_incomplete_ok(const nj_huff *h, int n)
{
    return n == (int)h->count[0] + (int)h->count[1];
}

/* Decode one symbol, or -1 when the bits match no code. */
static int huff_decode(nj_bits *b, const nj_huff *h)
{
    int len;
    int code = 0;
    int first = 0;
    int index = 0;

    for (len = 1; len <= NJ_HUFF_MAXBITS; len++) {
        int count;
        code |= (int)bits_get(b, 1);
        if (b->err) {
            return -1;
        }
        count = (int)h->count[len];
        if (code - first < count) {
            return (int)h->sym[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Block decoder                                                       */
/* ------------------------------------------------------------------ */

/* Output state: `out` counts every byte the stream produces, `cap` is the
 * caller's capacity.  Once out >= cap we stop storing but keep counting, so
 * the overflow report is exact. */
typedef struct {
    nj_bits  b;
    nj_u1   *dst;
    nj_u4    cap;
    nj_u4    out;
} nj_dec;

static void put_byte(nj_dec *d, nj_u1 v)
{
    if (d->out < d->cap) {
        d->dst[d->out] = v;
    }
    d->out++;
}

/* Copy a back reference byte by byte: with dist < len the bytes written by
 * this very loop become the source, exactly as RFC 1951 requires. */
static int copy_match(nj_dec *d, nj_u4 dist, nj_u4 len)
{
    if (dist == 0u || dist > d->out) {
        return -1;                      /* reference before the output start */
    }
    if (d->out > 0xFFFFFFFFu - len) {
        return -1;                      /* would wrap the 32-bit size counter */
    }
    while (len-- != 0u) {
        if (d->out >= d->cap) {
            d->out++;                   /* overflow: count only */
            continue;
        }
        d->dst[d->out] = d->dst[d->out - dist];
        d->out++;
    }
    return 0;
}

/* Decode one stored (uncompressed) block. */
static int block_stored(nj_dec *d)
{
    nj_u4 len, nlen;

    bits_align(&d->b);
    len = bits_get(&d->b, 16);
    nlen = bits_get(&d->b, 16);
    if (d->b.err) {
        return -1;
    }
    if ((len ^ 0xFFFFu) != nlen) {
        return -1;                      /* LEN/NLEN mismatch */
    }
    if (d->out > 0xFFFFFFFFu - len) {
        return -1;
    }
    if (d->b.cnt == 0) {
        /* Nothing buffered: the payload can be copied straight across. */
        nj_u4 room;
        if (d->b.len - d->b.pos < len) {
            return -1;                  /* truncated */
        }
        room = (d->out < d->cap) ? (d->cap - d->out) : 0u;
        if (room > len) {
            room = len;
        }
        if (room != 0u) {
            memcpy(d->dst + d->out, d->b.src + d->b.pos, (size_t)room);
        }
        d->b.pos += len;
        d->out += len;
        return 0;
    }
    while (len-- != 0u) {
        nj_u4 v = bits_get(&d->b, 8);
        if (d->b.err) {
            return -1;
        }
        put_byte(d, (nj_u1)v);
    }
    return 0;
}

/* Decode a Huffman-coded block (fixed or dynamic tables). */
static int block_codes(nj_dec *d, const nj_huff *lit, const nj_huff *dist)
{
    for (;;) {
        int sym = huff_decode(&d->b, lit);
        if (sym < 0) {
            return -1;
        }
        if (sym < 256) {
            if (d->out == 0xFFFFFFFFu) {
                return -1;
            }
            put_byte(d, (nj_u1)sym);
        } else if (sym == 256) {
            return 0;                   /* end of block */
        } else {
            nj_u4 len, dd;
            int dsym;

            sym -= 257;
            if (sym >= NJ_LEN_SYMS) {
                return -1;              /* symbols 286/287 are invalid */
            }
            len = (nj_u4)s_len_base[sym] + bits_get(&d->b, (int)s_len_extra[sym]);
            if (d->b.err) {
                return -1;
            }
            dsym = huff_decode(&d->b, dist);
            if (dsym < 0 || dsym >= NJ_DIST_MAX) {
                return -1;              /* bad or missing distance code */
            }
            dd = (nj_u4)s_dist_base[dsym] + bits_get(&d->b, (int)s_dist_extra[dsym]);
            if (d->b.err) {
                return -1;
            }
            if (copy_match(d, dd, len) != 0) {
                return -1;
            }
        }
    }
}

/* Fixed (pre-agreed) Huffman tables of RFC 1951 3.2.6.  The literal/length
 * code is complete; the distance code intentionally is not (30 of the 32
 * five-bit patterns are used), so its build result is ignored on purpose. */
static void tables_fixed(nj_huff *lit, nj_huff *dist)
{
    nj_u1 lens[NJ_LITLEN_MAX];
    int i;

    for (i = 0; i < 144; i++) {
        lens[i] = 8;
    }
    for (; i < 256; i++) {
        lens[i] = 9;
    }
    for (; i < 280; i++) {
        lens[i] = 7;
    }
    for (; i < 288; i++) {
        lens[i] = 8;
    }
    (void)huff_build(lit, lens, NJ_LITLEN_MAX);

    for (i = 0; i < NJ_DIST_MAX; i++) {
        lens[i] = 5;
    }
    (void)huff_build(dist, lens, NJ_DIST_MAX);
}

/* Read the dynamic block header and build both tables. */
static int tables_dynamic(nj_bits *b, nj_huff *lit, nj_huff *dist)
{
    nj_u1 lens[NJ_LITLEN_MAX + NJ_DIST_MAX];
    nj_u1 clens[NJ_CLEN_MAX];
    nj_huff cl;
    int hlit, hdist, hclen, i, n, left;

    hlit = (int)bits_get(b, 5) + 257;
    hdist = (int)bits_get(b, 5) + 1;
    hclen = (int)bits_get(b, 4) + 4;
    if (b->err) {
        return -1;
    }
    if (hlit > 286 || hdist > NJ_DIST_MAX) {
        return -1;                      /* alphabet larger than RFC 1951 allows */
    }
    for (i = 0; i < NJ_CLEN_MAX; i++) {
        clens[i] = 0;
    }
    for (i = 0; i < hclen; i++) {
        clens[s_clen_order[i]] = (nj_u1)bits_get(b, 3);
    }
    if (b->err) {
        return -1;
    }
    left = huff_build(&cl, clens, NJ_CLEN_MAX);
    if (left < 0 || (left > 0 && !huff_incomplete_ok(&cl, NJ_CLEN_MAX))) {
        return -1;
    }

    /* Expand the run-length coded length vector. */
    n = hlit + hdist;
    for (i = 0; i < n; ) {
        int sym = huff_decode(b, &cl);
        if (sym < 0) {
            return -1;
        }
        if (sym < 16) {
            lens[i] = (nj_u1)sym;
            i++;
        } else {
            int rep;
            nj_u1 fill;
            if (sym == 16) {
                if (i == 0) {
                    return -1;          /* repeat with no previous length */
                }
                rep = 3 + (int)bits_get(b, 2);
                fill = lens[i - 1];
            } else if (sym == 17) {
                rep = 3 + (int)bits_get(b, 3);
                fill = 0;
            } else {
                rep = 11 + (int)bits_get(b, 7);
                fill = 0;
            }
            if (b->err || rep > n - i) {
                return -1;
            }
            while (rep-- != 0) {
                lens[i] = fill;
                i++;
            }
        }
    }

    /* Symbol 256 must exist or the block could never terminate. */
    if (lens[256] == 0) {
        return -1;
    }
    left = huff_build(lit, lens, hlit);
    if (left < 0 || (left > 0 && !huff_incomplete_ok(lit, hlit))) {
        return -1;
    }
    left = huff_build(dist, lens + hlit, hdist);
    if (left < 0 || (left > 0 && !huff_incomplete_ok(dist, hdist))) {
        return -1;                      /* a block without matches is fine */
    }
    return 0;
}

/*
 * Decode a whole raw DEFLATE stream into `dst` (capacity `cap`).
 * `*produced` always receives the exact number of bytes the stream describes.
 * Returns NJ_OK even when the output did not fit: the caller compares
 * `*produced` against `cap` to raise NJ_ERR_OOM.
 */
static nj_status inflate_core(const nj_u1 *src, nj_u4 src_len,
                              nj_u1 *dst, nj_u4 cap, nj_u4 *produced)
{
    nj_dec d;
    nj_huff lit;
    nj_huff dist;
    int last = 0;

    d.b.src = src;
    d.b.len = src_len;
    d.b.pos = 0;
    d.b.buf = 0;
    d.b.cnt = 0;
    d.b.err = 0;
    d.dst = dst;
    d.cap = cap;
    d.out = 0;

    while (last == 0) {
        int type;

        last = (int)bits_get(&d.b, 1);
        type = (int)bits_get(&d.b, 2);
        if (d.b.err) {
            return NJ_ERR_FORMAT;       /* truncated before the block header */
        }
        if (type == 0) {
            if (block_stored(&d) != 0) {
                return NJ_ERR_FORMAT;
            }
        } else if (type == 1 || type == 2) {
            if (type == 1) {
                tables_fixed(&lit, &dist);
            } else if (tables_dynamic(&d.b, &lit, &dist) != 0) {
                return NJ_ERR_FORMAT;
            }
            if (block_codes(&d, &lit, &dist) != 0) {
                return NJ_ERR_FORMAT;
            }
        } else {
            return NJ_ERR_FORMAT;       /* reserved block type 3 */
        }
    }
    *produced = d.out;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Checksums                                                           */
/* ------------------------------------------------------------------ */

nj_u4 nj_crc32(const void *data, nj_u4 len, nj_u4 seed)
{
    const nj_u1 *p = (const nj_u1 *)data;
    nj_u4 c;

    if (p == NULL || len == 0u) {
        return seed;    /* folding zero bytes leaves a running CRC untouched */
    }
    c = seed ^ 0xFFFFFFFFu;
    while (len-- != 0u) {
        c = s_crc_table[(c ^ *p) & 0xFFu] ^ (c >> 8);
        p++;
    }
    return c ^ 0xFFFFFFFFu;
}

nj_u4 nj_adler32(const void *data, nj_u4 len)
{
    const nj_u1 *p = (const nj_u1 *)data;
    nj_u4 s1 = 1u;
    nj_u4 s2 = 0u;

    if (p == NULL) {
        return 1u;
    }
    while (len != 0u) {
        /* 4096 bytes keep s2 far away from 2^32 even for maximal input. */
        nj_u4 n = (len > 4096u) ? 4096u : len;
        len -= n;
        while (n-- != 0u) {
            s1 += *p;
            p++;
            s2 += s1;
        }
        s1 %= 65521u;
        s2 %= 65521u;
    }
    return (s2 << 16) | s1;
}

/* ------------------------------------------------------------------ */
/* zlib wrapper (RFC 1950)                                             */
/* ------------------------------------------------------------------ */

/* Result of looking at the two magic bytes. */
#define NJ_ZL_RAW   0    /* not a zlib header: treat as raw DEFLATE */
#define NJ_ZL_HDR   2    /* valid header, skip 2 bytes              */
#define NJ_ZL_DICT  (-1) /* header asks for a preset dictionary     */

/*
 * RFC 1950: CMF/FLG with CM == 8 (deflate), CINFO <= 7 (window <= 32 KB) and
 * (CMF * 256 + FLG) % 31 == 0.  The common headers 0x78 0x01 / 0x9C / 0xDA
 * (and the fast/level variants 0x78 0x5E) are all accepted.
 *
 * A well formed raw DEFLATE stream can only collide with this test in the
 * narrow case of a non-final stored block whose first two bytes happen to
 * satisfy the checksum; nj_inflate_zlib() therefore retries as raw DEFLATE
 * whenever the zlib interpretation fails to decode.
 */
static int zlib_header(const nj_u1 *p, nj_u4 len)
{
    nj_u4 h;

    if (len < 2u) {
        return NJ_ZL_RAW;
    }
    if ((p[0] & 0x0Fu) != 8u || (p[0] >> 4) > 7u) {
        return NJ_ZL_RAW;
    }
    h = ((nj_u4)p[0] << 8) | (nj_u4)p[1];
    if (h % 31u != 0u) {
        return NJ_ZL_RAW;
    }
    if ((p[1] & 0x20u) != 0u) {
        return NJ_ZL_DICT;  /* FDICT: needs a dictionary we do not have */
    }
    return NJ_ZL_HDR;
}

nj_status nj_inflate_raw(const void *src, nj_u4 src_len,
                         void *dst, nj_u4 *dst_len)
{
    nj_u4 cap;
    nj_u4 produced = 0;
    nj_status st;

    if (dst_len == NULL) {
        return NJ_ERR;
    }
    cap = *dst_len;
    *dst_len = 0;
    if (src == NULL || src_len == 0u) {
        return NJ_ERR_FORMAT;
    }
    if (dst == NULL && cap != 0u) {
        return NJ_ERR;
    }
    st = inflate_core((const nj_u1 *)src, src_len, (nj_u1 *)dst, cap, &produced);
    *dst_len = produced;
    if (st != NJ_OK) {
        return st;
    }
    return (produced > cap) ? NJ_ERR_OOM : NJ_OK;
}

nj_status nj_inflate_zlib(const void *src, nj_u4 src_len,
                          void *dst, nj_u4 *dst_len)
{
    const nj_u1 *p = (const nj_u1 *)src;
    nj_u4 cap;
    nj_u4 produced = 0;
    nj_status st;
    int hdr;

    if (dst_len == NULL) {
        return NJ_ERR;
    }
    cap = *dst_len;
    *dst_len = 0;
    if (p == NULL || src_len == 0u) {
        return NJ_ERR_FORMAT;
    }
    if (dst == NULL && cap != 0u) {
        return NJ_ERR;
    }
    hdr = zlib_header(p, src_len);
    if (hdr == NJ_ZL_HDR) {
        produced = 0;
        st = inflate_core(p + 2u, src_len - 2u, (nj_u1 *)dst, cap, &produced);
        if (st == NJ_ERR_FORMAT) {
            /* Two bytes that merely look like a zlib header: retry raw. */
            produced = 0;
            st = inflate_core(p, src_len, (nj_u1 *)dst, cap, &produced);
        }
    } else {
        /* Raw DEFLATE, or a zlib stream needing a preset dictionary (which
         * cannot be decoded here): the raw interpretation is the only one
         * left to try, and it reports NJ_ERR_FORMAT on failure. */
        st = inflate_core(p, src_len, (nj_u1 *)dst, cap, &produced);
    }
    *dst_len = produced;
    if (st != NJ_OK) {
        return st;
    }
    return (produced > cap) ? NJ_ERR_OOM : NJ_OK;
}

nj_status nj_inflate_zlib_size(const void *src, nj_u4 src_len, nj_u4 *out_size)
{
    const nj_u1 *p = (const nj_u1 *)src;
    nj_u4 produced = 0;
    nj_status st;
    int hdr;

    if (p == NULL || src_len == 0u || out_size == NULL) {
        return NJ_ERR_FORMAT;
    }
    hdr = zlib_header(p, src_len);
    /* Decoding with a zero-capacity destination counts without storing. */
    if (hdr == NJ_ZL_HDR) {
        produced = 0;
        st = inflate_core(p + 2u, src_len - 2u, NULL, 0u, &produced);
        if (st == NJ_ERR_FORMAT) {
            produced = 0;
            st = inflate_core(p, src_len, NULL, 0u, &produced);
        }
    } else {
        st = inflate_core(p, src_len, NULL, 0u, &produced);
    }
    if (st != NJ_OK) {
        return st;
    }
    *out_size = produced;
    return NJ_OK;
}
