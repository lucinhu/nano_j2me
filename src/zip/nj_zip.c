/*
 * nano_j2me - nj_zip.c
 *
 * Minimal read-only ZIP/JAR reader.
 *
 * Design notes
 * ------------
 *  - No mutable global state: the module owns no file-scope variables at all.
 *    The archive descriptor and its entry array come from the allocator hooks
 *    the caller passes to nj_zip_open() (malloc/free when the hooks are NULL
 *    and NJ_USE_LIBC is enabled).
 *  - The archive image is never copied and never modified; every field is
 *    assembled from individually bounds-checked byte reads, so an unaligned
 *    or hostile image cannot make the reader touch memory it does not own.
 *  - Offsets and sizes found in the file are always validated against the
 *    archive bounds *before* they are used to index anything.  All arithmetic
 *    that could wrap is expressed as a range test instead of an addition.
 *  - Only the central directory is authoritative: the local header is read
 *    solely to locate the payload, because its name/extra lengths (and, for
 *    streamed entries, its sizes) may differ from the central directory.
 */

#include "nj2me/nj_zip.h"
#include "nj2me/nj_inflate.h"   /* deflated entries are inflated in place */

#include <string.h>   /* memcpy/memset; <string.h> is available freestanding */

#if NJ_USE_LIBC
#  include <stdlib.h> /* malloc/free for the default allocator */
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define NJ_ZIP_SIG_LFH   0x04034b50u   /* local file header            */
#define NJ_ZIP_SIG_CDFH  0x02014b50u   /* central directory file header */
#define NJ_ZIP_SIG_EOCD  0x06054b50u   /* end of central directory     */

#define NJ_ZIP_LFH_SIZE  30u           /* local file header body size  */
#define NJ_ZIP_CDH_SIZE  46u           /* central dir header body size */
#define NJ_ZIP_EOCD_SIZE 22u           /* EOCD without the comment     */
#define NJ_ZIP_EOCD_MAX  (NJ_ZIP_EOCD_SIZE + 65535u)

#define NJ_ZIP_ZIP64_SENTINEL 0xFFFFFFFFu

/* ------------------------------------------------------------------ */
/* Archive descriptor (allocated by the caller's allocator)            */
/* ------------------------------------------------------------------ */

struct nj_zip {
    const nj_u1  *data;        /* borrowed archive image          */
    nj_u4         size;
    int           count;       /* usable (non-directory) entries  */
    nj_zip_entry *entries;
    void         *(*mem_alloc)(void *ud, nj_size n);
    void          (*mem_free)(void *ud, void *p);
    void          *mem_ud;

    /*
     * The decompressor, when the port supplied one (nj_zip_set_inflate).  A
     * JAR is mostly compressed entries, so this is the hot path of loading a
     * game: a platform with zlib in ROM gets to use it, and a build with
     * -DNJ_ENABLE_INFLATE=0 has nothing else.
     */
    nj_status    (*inflate)(void *ud, const nj_u1 *in, nj_u4 in_len,
                            nj_u1 *out, nj_u4 out_len);
    void          *inflate_ud;
};

/* ------------------------------------------------------------------ */
/* Bounds-checked little-endian readers                                */
/* ------------------------------------------------------------------ */

/* True when [off, off+len) lies inside [0, size); never overflows. */
static int zr_has(nj_u4 size, nj_u4 off, nj_u4 len)
{
    return (off <= size) && (len <= size - off);
}

/* The callers below must have established the range with zr_has() first. */
static nj_u2 zr_u2(const nj_u1 *d, nj_u4 off)
{
    return (nj_u2)((nj_u2)d[off] | ((nj_u2)d[off + 1u] << 8));
}

static nj_u4 zr_u4(const nj_u1 *d, nj_u4 off)
{
    return (nj_u4)d[off]
         | ((nj_u4)d[off + 1u] << 8)
         | ((nj_u4)d[off + 2u] << 16)
         | ((nj_u4)d[off + 3u] << 24);
}

/* ------------------------------------------------------------------ */
/* End of central directory                                            */
/* ------------------------------------------------------------------ */

/*
 * Scan the last 64 KB + 22 bytes backwards for the EOCD signature.  A
 * "PK\005\006" sequence inside the archive comment is skipped because the
 * candidate must also carry a comment length that is consistent with the end
 * of the file; the strict pass runs first, then a lenient pass tolerates
 * trailing bytes some archivers append.
 */
static int zip_find_eocd(const nj_u1 *d, nj_u4 size, nj_u4 *out)
{
    nj_u4 lo;
    nj_u4 off;
    int pass;

    if (size < NJ_ZIP_EOCD_SIZE) {
        return 0;
    }
    lo = (size > NJ_ZIP_EOCD_MAX) ? (size - NJ_ZIP_EOCD_MAX) : 0u;
    for (pass = 0; pass < 2; pass++) {
        off = size - NJ_ZIP_EOCD_SIZE;
        for (;;) {
            if (d[off] == 0x50u && d[off + 1u] == 0x4Bu &&
                d[off + 2u] == 0x05u && d[off + 3u] == 0x06u) {
                nj_u4 clen = (nj_u4)zr_u2(d, off + 20u);
                nj_u4 rest = size - off - NJ_ZIP_EOCD_SIZE;
                if (clen == rest || (pass == 1 && clen <= rest)) {
                    *out = off;
                    return 1;
                }
            }
            if (off == lo) {
                break;
            }
            off--;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Central directory iteration                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    nj_u4 name_off;      /* offset of the raw name in the archive  */
    nj_u2 name_len;
    nj_u2 method;
    nj_u2 dos_time;
    nj_u2 dos_date;
    nj_u4 comp_size;
    nj_u4 uncomp_size;
    nj_u4 local_off;
    int   is_dir;        /* raw name ends with '/'                 */
} nj_cd_rec;

typedef struct {
    nj_u4 off;           /* cursor inside the central directory    */
    nj_u4 end;           /* one past the central directory         */
    int   left;          /* records still expected (EOCD count)    */
} nj_cd_iter;

/* Fetch the next central directory record.
 * Returns 1 = record produced, 0 = done, -1 = malformed. */
static int cd_next(const nj_u1 *d, nj_u4 size, nj_cd_iter *it, nj_cd_rec *r)
{
    nj_u4 o = it->off;
    nj_u2 nlen, elen, clen;
    nj_u4 total;

    if (it->left <= 0) {
        return 0;
    }
    if (!zr_has(size, o, NJ_ZIP_CDH_SIZE) || zr_u4(d, o) != NJ_ZIP_SIG_CDFH) {
        return -1;
    }
    nlen = zr_u2(d, o + 28u);
    elen = zr_u2(d, o + 30u);
    clen = zr_u2(d, o + 32u);
    total = (nj_u4)NJ_ZIP_CDH_SIZE + (nj_u4)nlen + (nj_u4)elen + (nj_u4)clen;
    if (!zr_has(size, o, total) || o + total > it->end) {
        return -1;
    }

    r->name_off    = o + NJ_ZIP_CDH_SIZE;
    r->name_len    = nlen;
    r->method      = zr_u2(d, o + 10u);
    r->dos_time    = zr_u2(d, o + 12u);
    r->dos_date    = zr_u2(d, o + 14u);
    r->comp_size   = zr_u4(d, o + 20u);
    r->uncomp_size = zr_u4(d, o + 24u);
    r->local_off   = zr_u4(d, o + 42u);
    r->is_dir      = (nlen != 0u && d[r->name_off + (nj_u4)nlen - 1u] == '/');

    it->off = o + total;
    it->left--;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Allocator plumbing                                                  */
/* ------------------------------------------------------------------ */

#if NJ_USE_LIBC
static void *zip_libc_alloc(void *ud, nj_size n)
{
    (void)ud;
    return malloc((size_t)n);
}

static void zip_libc_free(void *ud, void *p)
{
    (void)ud;
    free(p);
}
#endif

/* Resolve the effective allocator: caller hooks, else malloc/free. */
static int zip_alloc_init(const nj_zip_alloc *za,
                          void *(**fn_alloc)(void *, nj_size),
                          void (**fn_free)(void *, void *),
                          void **ud)
{
    if (za != NULL && za->alloc != NULL) {
        *fn_alloc = za->alloc;
        *fn_free = za->free;      /* NULL is legal: an arena never frees */
        *ud = za->ud;
        return 1;
    }
#if NJ_USE_LIBC
    *fn_alloc = zip_libc_alloc;
    *fn_free = zip_libc_free;
    *ud = NULL;
    return 1;
#else
    (void)fn_alloc;
    (void)fn_free;
    (void)ud;
    return 0;   /* no allocator available in a freestanding build */
#endif
}

/* ------------------------------------------------------------------ */
/* Open / close                                                        */
/* ------------------------------------------------------------------ */

nj_zip *nj_zip_open(const void *data, nj_u4 size, const nj_zip_alloc *alloc)
{
    const nj_u1 *d = (const nj_u1 *)data;
    void *(*fn_alloc)(void *, nj_size);
    void (*fn_free)(void *, void *);
    void *ud;
    nj_u4 eocd = 0;
    nj_u4 cd_off, cd_size, cd_end;
    int entries_total;
    int usable = 0;
    nj_cd_iter it;
    nj_cd_rec rec;
    nj_zip *z;
    int rc;

    if (d == NULL || size < NJ_ZIP_EOCD_SIZE) {
        return NULL;
    }
    if (!zip_find_eocd(d, size, &eocd)) {
        return NULL;
    }
    entries_total = (int)zr_u2(d, eocd + 10u);
    cd_size = zr_u4(d, eocd + 12u);
    cd_off = zr_u4(d, eocd + 16u);
    if (cd_off == NJ_ZIP_ZIP64_SENTINEL || cd_size == NJ_ZIP_ZIP64_SENTINEL) {
        return NULL;                     /* ZIP64: not supported */
    }
    if (!zr_has(size, cd_off, cd_size)) {
        return NULL;                     /* directory outside the image */
    }
    cd_end = cd_off + cd_size;           /* safe: bounded by zr_has() */
    if (cd_end > eocd) {
        return NULL;                     /* directory overlaps the EOCD */
    }

    /* Pass 1: validate the directory and count usable entries. */
    it.off = cd_off;
    it.end = cd_end;
    it.left = entries_total;
    for (;;) {
        rc = cd_next(d, size, &it, &rec);
        if (rc == 0) {
            break;
        }
        if (rc < 0) {
            return NULL;
        }
        if (rec.name_len == 0u || rec.is_dir) {
            continue;                    /* nameless or directory entry */
        }
        usable++;
    }

    if (!zip_alloc_init(alloc, &fn_alloc, &fn_free, &ud)) {
        return NULL;
    }
    z = (nj_zip *)fn_alloc(ud, (nj_size)sizeof(*z));
    if (z == NULL) {
        return NULL;
    }
    memset(z, 0, sizeof(*z));
    z->data = d;
    z->size = size;
    z->mem_alloc = fn_alloc;
    z->mem_free = fn_free;
    z->mem_ud = ud;

    z->entries = (nj_zip_entry *)fn_alloc(
        ud, (nj_size)sizeof(nj_zip_entry) * (nj_size)(usable > 0 ? usable : 1));
    if (z->entries == NULL) {
        if (fn_free != NULL) {
            fn_free(ud, z);
        }
        return NULL;
    }

    /* Pass 2: fill the entry array (directory entries are dropped). */
    it.off = cd_off;
    it.end = cd_end;
    it.left = entries_total;
    for (;;) {
        nj_u4 copy_len;
        nj_zip_entry *e;

        rc = cd_next(d, size, &it, &rec);
        if (rc == 0) {
            break;
        }
        if (rc < 0 || rec.name_len == 0u || rec.is_dir) {
            continue;                    /* cannot happen after pass 1 */
        }
        e = &z->entries[z->count];
        copy_len = (nj_u4)rec.name_len;
        if (copy_len > (nj_u4)(NJ_ZIP_NAME_MAX - 1)) {
            copy_len = (nj_u4)(NJ_ZIP_NAME_MAX - 1);   /* truncate safely */
        }
        memcpy(e->name, d + rec.name_off, (size_t)copy_len);
        e->name[copy_len] = '\0';
        e->comp_size    = rec.comp_size;
        e->uncomp_size  = rec.uncomp_size;
        e->method       = rec.method;
        e->local_offset = rec.local_off;
        e->dos_time     = rec.dos_time;
        e->dos_date     = rec.dos_date;
        z->count++;
    }
    return z;
}

void nj_zip_close(nj_zip *z)
{
    void (*fn_free)(void *, void *);
    void *ud;

    if (z == NULL) {
        return;
    }
    fn_free = z->mem_free;
    ud = z->mem_ud;
    if (fn_free != NULL) {
        fn_free(ud, z->entries);
        fn_free(ud, z);
    }
}

void nj_zip_set_inflate(nj_zip *z,
                        nj_status (*fn)(void *ud, const nj_u1 *in,
                                        nj_u4 in_len, nj_u1 *out,
                                        nj_u4 out_len),
                        void *ud)
{
    if (z == NULL) {
        return;
    }
    z->inflate = fn;
    z->inflate_ud = ud;
}

/*
 * Inflate one entry's DEFLATE stream.
 *
 * The port's hook goes first when there is one: it is the only decompressor a
 * build with -DNJ_ENABLE_INFLATE=0 has, and on a platform that has zlib it is
 * both smaller and faster than ours.  The uncompressed length is known from
 * the directory either way, so the destination is always big enough and
 * neither path has to report a size back.
 */
static nj_status zip_inflate(const nj_zip *z, const void *src, nj_u4 src_len,
                             void *dst, nj_u4 dst_len)
{
    if (z->inflate != NULL) {
        return z->inflate(z->inflate_ud, (const nj_u1 *)src, src_len,
                          (nj_u1 *)dst, dst_len);
    }
    return nj_inflate_raw(src, src_len, dst, &dst_len);
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

int nj_zip_count(const nj_zip *z)
{
    return (z != NULL) ? z->count : 0;
}

const nj_zip_entry *nj_zip_entry_at(const nj_zip *z, int index)
{
    if (z == NULL || index < 0 || index >= z->count) {
        return NULL;
    }
    return &z->entries[index];
}

/* ASCII comparison helpers; no locale, no <ctype.h> dependency. */
static char name_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int name_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;                     /* both NUL: equal */
}

static int name_eq_ci(const char *a, const char *b)
{
    while (*a != '\0' && name_lower(*a) == name_lower(*b)) {
        a++;
        b++;
    }
    return name_lower(*a) == name_lower(*b);
}

const nj_zip_entry *nj_zip_find(const nj_zip *z, const char *name)
{
    int i;

    if (z == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < z->count; i++) {
        if (name_eq(z->entries[i].name, name)) {
            return &z->entries[i];
        }
    }
    return NULL;
}

const nj_zip_entry *nj_zip_find_ci(const nj_zip *z, const char *name)
{
    int i;

    if (z == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < z->count; i++) {
        if (name_eq_ci(z->entries[i].name, name)) {
            return &z->entries[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Extraction                                                          */
/* ------------------------------------------------------------------ */

/*
 * Locate the payload of `e` by reading its *local* header: the local name and
 * extra lengths are the ones that matter for finding the data, and they are
 * allowed to differ from the central directory copies.  Sizes always come
 * from the central directory.
 *
 * Returns 1 and writes the payload offset, or 0 when the archive is malformed.
 */
static int entry_payload(const nj_zip *z, const nj_zip_entry *e, nj_u4 *out_off)
{
    const nj_u1 *d = z->data;
    nj_u4 lo = e->local_offset;
    nj_u4 off;
    nj_u2 nlen, elen;

    if (!zr_has(z->size, lo, NJ_ZIP_LFH_SIZE)) {
        return 0;
    }
    if (zr_u4(d, lo) != NJ_ZIP_SIG_LFH) {
        return 0;
    }
    nlen = zr_u2(d, lo + 26u);
    elen = zr_u2(d, lo + 28u);
    off = lo + NJ_ZIP_LFH_SIZE;          /* safe: lo + 30 <= size */
    if (!zr_has(z->size, off, (nj_u4)nlen + (nj_u4)elen)) {
        return 0;
    }
    off += (nj_u4)nlen + (nj_u4)elen;
    if (!zr_has(z->size, off, e->comp_size)) {
        return 0;                        /* payload runs past the image */
    }
    *out_off = off;
    return 1;
}

nj_s4 nj_zip_extract(const nj_zip *z, const nj_zip_entry *e,
                     void *buf, nj_u4 buf_size)
{
    nj_u4 off;

    if (z == NULL || e == NULL) {
        return NJ_ERR;
    }
    if (e->uncomp_size > 0x7FFFFFFFu) {
        return NJ_ERR_FORMAT;            /* cannot be reported as nj_s4 */
    }
    if (buf == NULL) {
        return (nj_s4)e->uncomp_size;    /* size query needs no payload */
    }
    if (e->method != 0u && e->method != 8u) {
        return NJ_ERR_UNSUPPORTED;
    }
    if (!entry_payload(z, e, &off)) {
        return NJ_ERR_FORMAT;
    }
    if (e->method == 0u) {
        if (e->comp_size != e->uncomp_size) {
            return NJ_ERR_FORMAT;        /* stored entries must agree */
        }
        if (buf_size < e->uncomp_size) {
            return NJ_ERR_OOM;
        }
        if (e->uncomp_size != 0u) {
            memcpy(buf, z->data + off, (size_t)e->uncomp_size);
        }
        return (nj_s4)e->uncomp_size;
    }
    {
        nj_u4 produced = buf_size;
        nj_status st;

        if (buf_size < e->uncomp_size) {
            return NJ_ERR_OOM;
        }
        st = zip_inflate(z, z->data + off, e->comp_size, buf, e->uncomp_size);
        if (st == NJ_ERR_OOM) {
            return NJ_ERR_OOM;
        }
        if (st != NJ_OK) {
            return NJ_ERR_FORMAT;
        }
        /*
         * A port hook fills exactly the size it was given; the built-in one
         * reports what it produced, and anything but the directory's number
         * means the directory lied.
         */
        if (z->inflate == NULL && produced != e->uncomp_size) {
            return NJ_ERR_FORMAT;        /* central directory lied */
        }
        return (nj_s4)e->uncomp_size;
    }
}

void *nj_zip_extract_alloc(const nj_zip *z, const nj_zip_entry *e,
                           const nj_zip_alloc *za, nj_u4 *out_len)
{
    void *(*fn_alloc)(void *, nj_size);
    void (*fn_free)(void *, void *);
    void *ud;
    void *buf;
    nj_u4 off;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (z == NULL || e == NULL || e->uncomp_size == NJ_ZIP_ZIP64_SENTINEL) {
        return NULL;
    }
    if (!zip_alloc_init(za, &fn_alloc, &fn_free, &ud)) {
        return NULL;
    }

    /*
     * Validate everything before allocating: this module cannot release a
     * buffer on the caller's behalf, so a failure after the allocation would
     * leak.  For deflated entries the stream is walked once with a counting
     * destination, which both validates it and yields the exact size.
     */
    if (e->method != 0u && e->method != 8u) {
        return NULL;
    }
    if (!entry_payload(z, e, &off)) {
        return NULL;
    }
    if (e->method == 0u) {
        if (e->comp_size != e->uncomp_size) {
            return NULL;
        }
    } else if (z->inflate == NULL) {
        /*
         * A dry run of the built-in decompressor validates the stream and
         * confirms the directory's length before anything is allocated.  A
         * port hook cannot be asked to measure without producing output, so
         * with one installed the check is left to nj_zip_extract below, which
         * fails the same way and frees the buffer on its way out.
         */
        nj_u4 need = 0;
        nj_status st = nj_inflate_raw(z->data + off, e->comp_size, NULL, &need);
        if (st != NJ_OK && st != NJ_ERR_OOM) {
            return NULL;                 /* corrupt deflate stream */
        }
        if (need != e->uncomp_size) {
            return NULL;                 /* central directory lied */
        }
    }

    buf = fn_alloc(ud, (nj_size)e->uncomp_size + 1u);
    if (buf == NULL) {
        return NULL;
    }
    if (nj_zip_extract(z, e, buf, e->uncomp_size) < 0) {
        if (fn_free != NULL) {
            fn_free(ud, buf);
        }
        return NULL;
    }
    ((char *)buf)[e->uncomp_size] = '\0';
    if (out_len != NULL) {
        *out_len = e->uncomp_size;
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/* Manifest reader                                                     */
/* ------------------------------------------------------------------ */

static nj_u4 str_len(const char *s)
{
    nj_u4 n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static char char_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Compare a manifest attribute name (not NUL terminated) case-insensitively. */
static int attr_eq(const char *name, nj_u4 name_len, const char *attr, nj_u4 attr_len)
{
    nj_u4 i;

    if (name_len != attr_len) {
        return 0;
    }
    for (i = 0; i < name_len; i++) {
        if (char_lower(name[i]) != char_lower(attr[i])) {
            return 0;
        }
    }
    return 1;
}

/*
 * JAR manifest subset: "Name: value" lines, continuation lines starting with a
 * single space, CRLF or LF endings, an empty line ending the main section.
 * Only the main section is searched, as required for MIDlet attributes.
 */
nj_status nj_manifest_get(const char *data, nj_u4 len,
                          const char *attr, char *out, nj_u4 out_size)
{
    nj_u4 attr_len;
    nj_u4 i = 0;
    nj_u4 used = 0;
    int found = 0;

    if (data == NULL || attr == NULL || out == NULL || out_size == 0u) {
        return NJ_ERR;
    }
    out[0] = '\0';
    attr_len = str_len(attr);

    while (i < len) {
        nj_u4 ls = i;                    /* line start (inclusive) */
        nj_u4 le;                        /* line end (exclusive)   */
        int is_cont;

        while (i < len && data[i] != '\n') {
            i++;
        }
        le = i;
        if (i < len) {
            i++;                         /* step over '\n' */
        }
        if (le > ls && data[le - 1u] == '\r') {
            le--;                        /* strip CR of a CRLF pair */
        }

        if (le == ls) {
            break;                       /* blank line: main section over */
        }
        is_cont = (data[ls] == ' ');
        if (is_cont) {
            if (found) {
                nj_u4 k;
                for (k = ls + 1u; k < le; k++) {
                    if (used + 1u < out_size) {
                        out[used] = data[k];
                        used++;
                    }
                }
            }
            continue;                    /* continuation of an earlier value */
        }

        if (found) {
            break;                       /* next attribute: value is complete */
        }

        /*
         * Split "Name: value".  The name runs up to the colon (trailing blanks
         * are tolerated); the value starts after it and is left-trimmed.
         */
        {
            nj_u4 ns = ls;
            nj_u4 ne = ls;
            nj_u4 vs;

            while (ne < le && data[ne] != ':') {
                ne++;
            }
            if (ne >= le) {
                continue;                /* no colon: not an attribute line */
            }
            while (ne > ns && (data[ne - 1u] == ' ' || data[ne - 1u] == '\t')) {
                ne--;
            }
            if (!attr_eq(data + ns, ne - ns, attr, attr_len)) {
                continue;
            }
            found = 1;
            vs = ne + 1u;
            while (vs < le && (data[vs] == ' ' || data[vs] == '\t')) {
                vs++;                    /* trim leading blanks of the value */
            }
            while (vs < le) {
                if (used + 1u < out_size) {
                    out[used] = data[vs];
                    used++;
                }
                vs++;
            }
        }
    }

    out[used] = '\0';
    return found ? NJ_OK : NJ_ERR;
}
