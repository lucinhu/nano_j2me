/*
 * nano_j2me - nj_mp4.c
 *
 * See nj_mp4.h.  The parser is a straight walk of the box tree; the only
 * state that survives between calls is the sample-table cursor in nj_mp4, and
 * the only writes are to that caller-owned struct.  Nothing is allocated: the
 * file stays where the caller put it and samples are described by absolute
 * offsets into it.
 *
 * The one thing worth spelling out is how a sample is located.  stsc is a
 * run-length table over chunk numbers: entry i says chunks [first_chunk,
 * next_first_chunk) hold samples_per_chunk samples each.  Within a chunk the
 * samples are laid out back to back in sample order, at the offsets in stsz.
 * stts supplies decode times the same way, as runs of a constant delta.  The
 * cursor advances one chunk at a time so the cost is linear in the number of
 * samples, not quadratic, and tables are never copied into memory.
 */
#include "nj_mp4.h"

#if NJ_ENABLE_VIDEO

#include <string.h>

#define NJ_MP4_FOURCC(a, b, c, d) \
    (((nj_u4)(a) << 24) | ((nj_u4)(b) << 16) | ((nj_u4)(c) << 8) | (nj_u4)(d))

#define NJ_BOX_FTYP NJ_MP4_FOURCC('f', 't', 'y', 'p')
#define NJ_BOX_MOOV NJ_MP4_FOURCC('m', 'o', 'o', 'v')
#define NJ_BOX_MDAT NJ_MP4_FOURCC('m', 'd', 'a', 't')
#define NJ_BOX_TRAK NJ_MP4_FOURCC('t', 'r', 'a', 'k')
#define NJ_BOX_TKHD NJ_MP4_FOURCC('t', 'k', 'h', 'd')
#define NJ_BOX_MDIA NJ_MP4_FOURCC('m', 'd', 'i', 'a')
#define NJ_BOX_MDHD NJ_MP4_FOURCC('m', 'd', 'h', 'd')
#define NJ_BOX_HDLR NJ_MP4_FOURCC('h', 'd', 'l', 'r')
#define NJ_BOX_MINF NJ_MP4_FOURCC('m', 'i', 'n', 'f')
#define NJ_BOX_STBL NJ_MP4_FOURCC('s', 't', 'b', 'l')
#define NJ_BOX_STSD NJ_MP4_FOURCC('s', 't', 's', 'd')
#define NJ_BOX_STTS NJ_MP4_FOURCC('s', 't', 't', 's')
#define NJ_BOX_STSC NJ_MP4_FOURCC('s', 't', 's', 'c')
#define NJ_BOX_STSZ NJ_MP4_FOURCC('s', 't', 's', 'z')
#define NJ_BOX_STCO NJ_MP4_FOURCC('s', 't', 'c', 'o')
#define NJ_BOX_CO64 NJ_MP4_FOURCC('c', 'o', '6', '4')
#define NJ_BOX_STSS NJ_MP4_FOURCC('s', 't', 's', 's')
#define NJ_BOX_VIDE NJ_MP4_FOURCC('v', 'i', 'd', 'e')

/* A longer table than this is not a J2ME clip, and refusing it keeps every
 * index times an entry size inside 32 bits. */
#define NJ_MP4_MAX_SAMPLES 1000000u
#define NJ_MP4_MAX_ENTRIES 1000000u

static nj_u4 nj_mp4_rd32(const nj_u1 *p)
{
    return ((nj_u4)p[0] << 24) | ((nj_u4)p[1] << 16) |
           ((nj_u4)p[2] << 8) | (nj_u4)p[3];
}

static nj_u4 nj_mp4_rd16(const nj_u1 *p)
{
    return ((nj_u4)p[0] << 8) | (nj_u4)p[1];
}

/*
 * One box header at absolute offset `off`, with the parent's [off, end) range.
 * Returns 1 and sets type/header length/total size.  A 64-bit size whose high
 * half is nonzero is a file this address space cannot describe, and a size of
 * zero means "to the end of the parent" (legal for the last box).  Every other
 * impossible size is refused here so no caller has to think about it.
 */
static int nj_mp4_box(const nj_u1 *b, nj_u4 size, nj_u4 off, nj_u4 end,
                      nj_u4 *type, nj_u4 *hdr, nj_u4 *total)
{
    nj_u4 s, t;
    nj_u8 big;

    if (off > end || end > size || end - off < 8u) {
        return 0;
    }
    s = nj_mp4_rd32(b + off);
    t = nj_mp4_rd32(b + off + 4u);
    if (s == 1u) {
        if (end - off < 16u) {
            return 0;
        }
        big = ((nj_u8)nj_mp4_rd32(b + off + 8u) << 32) |
              (nj_u8)nj_mp4_rd32(b + off + 12u);
        if (big < 16u || big > (nj_u8)(end - off)) {
            return 0;
        }
        *hdr = 16u;
        *total = (nj_u4)big;
    } else if (s == 0u) {
        *hdr = 8u;
        *total = end - off;
    } else {
        if (s < 8u || s > end - off) {
            return 0;
        }
        *hdr = 8u;
        *total = s;
    }
    *type = t;
    return 1;
}

/* The H.263 and MPEG-4 sample entries the iterator can name.  A file whose
 * video entry is none of these is refused even if it is otherwise valid. */
static int nj_mp4_codec_known(nj_u4 codec)
{
    return codec == NJ_MP4_CODEC_S263 || codec == NJ_MP4_CODEC_H263 ||
           codec == NJ_MP4_CODEC_H263LC || codec == NJ_MP4_CODEC_MP4V;
}

static int nj_mp4_codec_is_h263(nj_u4 codec)
{
    return codec == NJ_MP4_CODEC_S263 || codec == NJ_MP4_CODEC_H263 ||
           codec == NJ_MP4_CODEC_H263LC;
}

/*
 * One track's fields, gathered while walking its boxes.  This lives on the
 * stack during nj_mp4_open(); the selected one is copied into nj_mp4.  A table
 * location of zero means the box was absent, which is why "duplicate stsz" is
 * visible as a nonzero location and can be refused.
 */
typedef struct nj_mp4_cand {
    int  video_handler;
    nj_u4 codec;              /* stsd entry fourcc, 0 when absent          */
    nj_u4 vw, vh;             /* stsd visual dimensions                    */
    nj_u4 tkhd_w, tkhd_h;     /* tkhd display dimensions                   */
    nj_u4 timescale;
    nj_u4 duration;
    nj_u4 stsd_count;
    nj_u4 stsz_at, stsz_count, stsz_size;
    nj_u4 stsc_at, stsc_count;
    nj_u4 stco_at, stco_count;
    int   stco64;
    nj_u4 stts_at, stts_count;
    nj_u4 stss_at, stss_count;
} nj_mp4_cand;

static void nj_mp4_cand_zero(nj_mp4_cand *c)
{
    memset(c, 0, sizeof(*c));
}

/* stsd: exactly one visual sample entry.  Its width/height are preferred over
 * tkhd's, because tkhd is display geometry and may be letterboxed. */
static int nj_mp4_parse_stsd(nj_mp4_cand *c, const nj_u1 *b, nj_u4 size,
                             nj_u4 body, nj_u4 blen)
{
    nj_u4 count, eoff, ehdr, etotal, etype, ebody;

    if (blen < 8u) {
        return 0;
    }
    count = nj_mp4_rd32(b + body + 4u);
    if (count != 1u) {
        /* Multiple descriptions would need per-sample switching; refusing is
         * the same choice nj_amr_mp4 makes. */
        return 0;
    }
    eoff = body + 8u;
    if (!nj_mp4_box(b, size, eoff, body + blen, &etype, &ehdr, &etotal)) {
        return 0;
    }
    if (etotal < ehdr + 28u) {
        return 0;
    }
    ebody = eoff + ehdr;
    c->codec = etype;
    c->stsd_count = count;
    c->vw = nj_mp4_rd16(b + ebody + 24u);
    c->vh = nj_mp4_rd16(b + ebody + 26u);
    return 1;
}

/* stbl: the five tables the walk needs.  Any table appearing twice is a
 * malformed file; the sample count and all table lengths are checked here, and
 * cross-table consistency is checked by nj_mp4_select() below. */
static int nj_mp4_parse_stbl(nj_mp4_cand *c, const nj_u1 *b, nj_u4 size,
                             nj_u4 off, nj_u4 end)
{
    while (off < end) {
        nj_u4 t, hdr, total, body, blen;

        if (!nj_mp4_box(b, size, off, end, &t, &hdr, &total)) {
            return 0;
        }
        body = off + hdr;
        blen = total - hdr;
        if (t == NJ_BOX_STSD) {
            if (c->codec != 0u || !nj_mp4_parse_stsd(c, b, size, body, blen)) {
                return 0;
            }
        } else if (t == NJ_BOX_STTS) {
            nj_u4 n;
            if (c->stts_at != 0u || blen < 8u) {
                return 0;
            }
            n = nj_mp4_rd32(b + body + 4u);
            if (n > NJ_MP4_MAX_ENTRIES || (nj_u8)8u + (nj_u8)n * 8u > blen) {
                return 0;
            }
            c->stts_at = body + 8u;
            c->stts_count = n;
        } else if (t == NJ_BOX_STSC) {
            nj_u4 n;
            if (c->stsc_at != 0u || blen < 8u) {
                return 0;
            }
            n = nj_mp4_rd32(b + body + 4u);
            if (n > NJ_MP4_MAX_ENTRIES || (nj_u8)8u + (nj_u8)n * 12u > blen) {
                return 0;
            }
            c->stsc_at = body + 8u;
            c->stsc_count = n;
        } else if (t == NJ_BOX_STSZ) {
            nj_u4 s, n;
            if (c->stsz_count != 0u || blen < 12u) {
                return 0;
            }
            s = nj_mp4_rd32(b + body + 4u);
            n = nj_mp4_rd32(b + body + 8u);
            if (n > NJ_MP4_MAX_SAMPLES) {
                return 0;
            }
            if (s == 0u) {
                if ((nj_u8)12u + (nj_u8)n * 4u > blen) {
                    return 0;
                }
                c->stsz_at = body + 12u;
            }
            c->stsz_size = s;
            c->stsz_count = n;
        } else if (t == NJ_BOX_STCO || t == NJ_BOX_CO64) {
            nj_u4 n, esz;
            if (c->stco_at != 0u || blen < 8u) {
                return 0;
            }
            n = nj_mp4_rd32(b + body + 4u);
            esz = (t == NJ_BOX_CO64) ? 8u : 4u;
            if (n > NJ_MP4_MAX_ENTRIES || (nj_u8)8u + (nj_u8)n * esz > blen) {
                return 0;
            }
            c->stco_at = body + 8u;
            c->stco_count = n;
            c->stco64 = (t == NJ_BOX_CO64);
        } else if (t == NJ_BOX_STSS) {
            nj_u4 n;
            if (c->stss_at != 0u || blen < 8u) {
                return 0;
            }
            n = nj_mp4_rd32(b + body + 4u);
            if (n > NJ_MP4_MAX_SAMPLES || (nj_u8)8u + (nj_u8)n * 4u > blen) {
                return 0;
            }
            c->stss_at = body + 8u;
            c->stss_count = n;
        }
        off += total;
    }
    return 1;
}

/* mdia: mdhd timescale, hdlr type, minf/stbl tables. */
static int nj_mp4_parse_mdia(nj_mp4_cand *c, const nj_u1 *b, nj_u4 size,
                             nj_u4 off, nj_u4 end)
{
    int have_mdhd = 0;

    while (off < end) {
        nj_u4 t, hdr, total, body, blen;

        if (!nj_mp4_box(b, size, off, end, &t, &hdr, &total)) {
            return 0;
        }
        body = off + hdr;
        blen = total - hdr;
        if (t == NJ_BOX_MDHD) {
            nj_u4 ver;
            if (have_mdhd || blen < 20u) {
                return 0;
            }
            ver = b[body];
            if (ver == 0u) {
                if (blen < 20u) {
                    return 0;
                }
                c->timescale = nj_mp4_rd32(b + body + 12u);
                c->duration = nj_mp4_rd32(b + body + 16u);
            } else if (ver == 1u) {
                nj_u8 dur;
                if (blen < 32u) {
                    return 0;
                }
                c->timescale = nj_mp4_rd32(b + body + 20u);
                dur = ((nj_u8)nj_mp4_rd32(b + body + 24u) << 32) |
                      (nj_u8)nj_mp4_rd32(b + body + 28u);
                c->duration = (dur > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (nj_u4)dur;
            } else {
                return 0;
            }
            if (c->timescale == 0u) {
                return 0;
            }
            have_mdhd = 1;
        } else if (t == NJ_BOX_HDLR) {
            nj_u4 htype;
            if (blen < 12u) {
                return 0;
            }
            htype = nj_mp4_rd32(b + body + 8u);
            if (htype == NJ_BOX_VIDE) {
                c->video_handler = 1;
            }
        } else if (t == NJ_BOX_MINF) {
            nj_u4 i = body, iend = body + blen;
            while (i < iend) {
                nj_u4 mt, mh, mtot, mbody, mblen;
                if (!nj_mp4_box(b, size, i, iend, &mt, &mh, &mtot)) {
                    return 0;
                }
                mbody = i + mh;
                mblen = mtot - mh;
                if (mt == NJ_BOX_STBL) {
                    if (!nj_mp4_parse_stbl(c, b, size, mbody, mbody + mblen)) {
                        return 0;
                    }
                }
                i += mtot;
            }
        }
        off += total;
    }
    return 1;
}

/* trak: tkhd dimensions plus mdia. */
static int nj_mp4_parse_trak(nj_mp4_cand *c, const nj_u1 *b, nj_u4 size,
                             nj_u4 off, nj_u4 end)
{
    while (off < end) {
        nj_u4 t, hdr, total, body, blen;

        if (!nj_mp4_box(b, size, off, end, &t, &hdr, &total)) {
            return 0;
        }
        body = off + hdr;
        blen = total - hdr;
        if (t == NJ_BOX_TKHD) {
            if (blen < 4u) {
                return 0;
            }
            if (b[body] == 0u) {
                if (blen < 84u) {
                    return 0;
                }
                c->tkhd_w = nj_mp4_rd32(b + body + 76u) >> 16;
                c->tkhd_h = nj_mp4_rd32(b + body + 80u) >> 16;
            } else if (b[body] == 1u) {
                if (blen < 96u) {
                    return 0;
                }
                c->tkhd_w = nj_mp4_rd32(b + body + 88u) >> 16;
                c->tkhd_h = nj_mp4_rd32(b + body + 92u) >> 16;
            } else {
                return 0;
            }
        } else if (t == NJ_BOX_MDIA) {
            if (!nj_mp4_parse_mdia(c, b, size, body, body + blen)) {
                return 0;
            }
        }
        off += total;
    }
    return 1;
}

/* Table readers.  nj_mp4_select() has already proved count * entry_size fits
 * inside the file, and counts are capped, so these arithmetic operations
 * cannot wrap. */
static nj_u4 nj_mp4_tab32(const nj_mp4 *m, nj_u4 at, nj_u4 idx)
{
    return nj_mp4_rd32(m->data + at + idx * 4u);
}

static nj_u4 nj_mp4_chunk_at(const nj_mp4 *m, nj_u4 idx)
{
    if (m->stco64) {
        const nj_u1 *p = m->data + m->stco_at + idx * 8u;
        nj_u4 hi = nj_mp4_rd32(p);
        if (hi != 0u) {
            return 0xFFFFFFFFu;
        }
        return nj_mp4_rd32(p + 4u);
    }
    return nj_mp4_tab32(m, m->stco_at, idx);
}

static nj_u4 nj_mp4_stsc_first(const nj_mp4 *m, nj_u4 idx)
{
    return nj_mp4_tab32(m, m->stsc_at, idx * 3u);
}

static nj_u4 nj_mp4_stsc_spc(const nj_mp4 *m, nj_u4 idx)
{
    return nj_mp4_tab32(m, m->stsc_at, idx * 3u + 1u);
}

static nj_u4 nj_mp4_stsc_sdi(const nj_mp4 *m, nj_u4 idx)
{
    return nj_mp4_tab32(m, m->stsc_at, idx * 3u + 2u);
}

static nj_u4 nj_mp4_sample_size(const nj_mp4 *m, nj_u4 idx)
{
    if (m->stsz_size != 0u) {
        return m->stsz_size;
    }
    return nj_mp4_tab32(m, m->stsz_at, idx);
}

/* Is [off, off+need) inside one 'mdat' payload?  A sample that is not is not
 * in this file's media data, so reading it would be reading something else. */
static int nj_mp4_in_mdat(const nj_mp4 *m, nj_u4 off, nj_u4 need)
{
    int i;

    if (need == 0u || off > m->size || need > m->size - off) {
        return 0;
    }
    for (i = 0; i < m->mdat_count; i++) {
        nj_u4 at = m->mdat_at[i], end = m->mdat_end[i];
        if (off >= at && off <= end && need <= end - off) {
            return 1;
        }
    }
    return 0;
}

/*
 * Cross-check the tables and copy the chosen track into the iterator.  The
 * critical invariant is that stsc describes exactly the samples stsz claims:
 * if the chunk walk ends with a different count, a table is lying and no
 * sample from this file can be trusted.
 */
static int nj_mp4_select(nj_mp4 *m, const nj_mp4_cand *c)
{
    nj_u8 total = 0;
    nj_u4 i, chunk, entry;

    if (!c->video_handler || c->codec == 0u || !nj_mp4_codec_known(c->codec)) {
        return 0;
    }
    if (c->stsd_count != 1u || c->stsz_count == 0u ||
        (c->stsz_at == 0u && c->stsz_size == 0u)) {
        return 0;
    }
    if (c->stsz_count > NJ_MP4_MAX_SAMPLES || c->timescale == 0u) {
        return 0;
    }
    if (c->stsc_count == 0u || c->stco_count == 0u || c->stts_count == 0u) {
        return 0;
    }

    /* The readers below work from the iterator, so install the table
     * locations first.  A later failure leaves the struct unspecified, which
     * nj_mp4_open() documents. */
    m->timescale = c->timescale;
    m->duration = c->duration;
    m->codec = c->codec;
    m->sample_count = c->stsz_count;
    m->stsz_at = c->stsz_at;
    m->stsz_count = c->stsz_count;
    m->stsz_size = c->stsz_size;
    m->stsc_at = c->stsc_at;
    m->stsc_count = c->stsc_count;
    m->stco_at = c->stco_at;
    m->stco_count = c->stco_count;
    m->stco64 = c->stco64;
    m->stts_at = c->stts_at;
    m->stts_count = c->stts_count;
    m->stss_at = c->stss_at;
    m->stss_count = c->stss_count;

    /* stts runs must cover every sample exactly. */
    total = 0;
    for (i = 0; i < c->stts_count; i++) {
        nj_u4 n = nj_mp4_rd32(m->data + c->stts_at + i * 8u);
        if (n == 0u) {
            return 0;
        }
        total += n;
        if (total > c->stsz_count) {
            return 0;
        }
    }
    if (total != c->stsz_count) {
        return 0;
    }

    /* stsc: first entry starts at chunk 1, each later run starts after the
     * previous one, and no run may claim to start past the last chunk. */
    if (nj_mp4_stsc_first(m, 0u) != 1u) {
        return 0;
    }
    for (i = 0; i < c->stsc_count; i++) {
        nj_u4 first = nj_mp4_stsc_first(m, i);
        if (nj_mp4_stsc_spc(m, i) == 0u || nj_mp4_stsc_sdi(m, i) != 1u) {
            return 0;
        }
        if (first == 0u || first > c->stco_count) {
            return 0;
        }
        if (i > 0u && first <= nj_mp4_stsc_first(m, i - 1u)) {
            return 0;
        }
    }

    /* Chunk offsets must be inside the file; co64 values above 4 GiB cannot be
     * addressed by this runtime. */
    for (i = 0; i < c->stco_count; i++) {
        nj_u4 at = nj_mp4_chunk_at(m, i);
        if (at == 0xFFFFFFFFu || at >= m->size) {
            return 0;
        }
    }

    /* stss numbers are 1-based and increasing; absent means all sync. */
    for (i = 0; i < c->stss_count; i++) {
        nj_u4 n = nj_mp4_tab32(m, c->stss_at, i);
        if (n == 0u || n > c->stsz_count) {
            return 0;
        }
        if (i > 0u && n <= nj_mp4_tab32(m, c->stss_at, i - 1u)) {
            return 0;
        }
    }
    /* A sync-sample table that stops before the end is legal (later samples
     * can be non-sync), so only the values are checked. */

    /* Walk the chunk runs and require exactly stsz_count samples. */
    total = 0;
    entry = 0;
    for (chunk = 1u; chunk <= c->stco_count; chunk++) {
        while (entry + 1u < c->stsc_count &&
               nj_mp4_stsc_first(m, entry + 1u) <= chunk) {
            entry++;
        }
        total += nj_mp4_stsc_spc(m, entry);
        if (total > c->stsz_count) {
            return 0;
        }
    }
    if (total != c->stsz_count) {
        return 0;
    }

    m->width = c->vw != 0u ? c->vw : c->tkhd_w;
    m->height = c->vh != 0u ? c->vh : c->tkhd_h;
    if (m->width == 0u || m->height == 0u || m->width > 4096u ||
        m->height > 4096u) {
        return 0;
    }

    /* Average frame rate: sample_count / (duration / timescale).  This is
     * what a J2ME Player needs for its frame clock; a variable-rate track is
     * reported by its average and each sample still carries its own dts. */
    if (m->duration != 0u) {
        nj_u8 fps = ((nj_u8)m->timescale * m->sample_count +
                     (nj_u8)m->duration / 2u) / (nj_u8)m->duration;
        m->fps = fps > 1000u ? 1000u : (nj_u4)fps;
    } else if (m->stts_count != 0u) {
        nj_u4 delta = nj_mp4_tab32(m, m->stts_at, 1u);
        m->fps = delta != 0u ? ((m->timescale + delta / 2u) / delta) : 0u;
    }
    return 1;
}

/*
 * Top level: require ftyp first (the sniffer in nj_lib_media.c uses the same
 * marker), collect 'mdat' payload ranges, and find the one 'moov'.  A file
 * with no 'mdat' has nowhere for a sample to live, so it is refused before any
 * table is believed.
 */
int nj_mp4_open(nj_mp4 *m, const void *data, nj_u4 size)
{
    const nj_u1 *b = (const nj_u1 *)data;
    nj_u4 off, end, type, hdr, total;
    nj_u4 moov_off = 0, moov_end = 0;
    nj_mp4_cand best, mp4v;
    int have_best = 0, have_mp4v = 0, have_ftyp = 0;

    if (m == NULL || data == NULL || size < 8u) {
        return NJ_MP4_ERR_FORMAT;
    }
    memset(m, 0, sizeof(*m));
    m->data = b;
    m->size = size;

    off = 0u;
    end = size;
    while (off < end) {
        if (!nj_mp4_box(b, size, off, end, &type, &hdr, &total)) {
            return NJ_MP4_ERR_FORMAT;
        }
        if (off == 0u && type != NJ_BOX_FTYP) {
            return NJ_MP4_ERR_FORMAT;
        }
        if (type == NJ_BOX_FTYP) {
            have_ftyp = 1;
        } else if (type == NJ_BOX_MDAT) {
            if (m->mdat_count >= NJ_MP4_MDAT_MAX || total <= hdr) {
                return NJ_MP4_ERR_FEATURE;
            }
            m->mdat_at[m->mdat_count] = off + hdr;
            m->mdat_end[m->mdat_count] = off + total;
            m->mdat_count++;
        } else if (type == NJ_BOX_MOOV) {
            if (moov_off != 0u) {
                return NJ_MP4_ERR_FORMAT;
            }
            moov_off = off + hdr;
            moov_end = off + total;
        }
        off += total;
    }
    if (!have_ftyp || moov_off == 0u || m->mdat_count == 0) {
        return NJ_MP4_ERR_FORMAT;
    }

    /* First video track that is H.263 wins; an MPEG-4 Part 2 track is kept only
     * as a fallback so the provider can name the format it cannot decode. */
    off = moov_off;
    end = moov_end;
    while (off < end) {
        if (!nj_mp4_box(b, size, off, end, &type, &hdr, &total)) {
            return NJ_MP4_ERR_FORMAT;
        }
        if (type == NJ_BOX_TRAK) {
            nj_mp4_cand c;
            nj_mp4_cand_zero(&c);
            if (nj_mp4_parse_trak(&c, b, size, off + hdr, off + total)) {
                if (c.video_handler && nj_mp4_codec_is_h263(c.codec)) {
                    if (!have_best) {
                        best = c;
                        have_best = 1;
                    }
                } else if (c.video_handler && c.codec == NJ_MP4_CODEC_MP4V) {
                    if (!have_mp4v) {
                        mp4v = c;
                        have_mp4v = 1;
                    }
                }
            }
        }
        off += total;
    }

    if (have_best) {
        if (!nj_mp4_select(m, &best)) {
            return NJ_MP4_ERR_FORMAT;
        }
    } else if (have_mp4v) {
        if (!nj_mp4_select(m, &mp4v)) {
            return NJ_MP4_ERR_FORMAT;
        }
    } else {
        return NJ_MP4_ERR_NO_VIDEO;
    }
    /* A parsed file is ready to hand out its first sample; a caller that wants
     * to start over later can still call nj_mp4_rewind() itself. */
    nj_mp4_rewind(m);
    return NJ_MP4_OK;
}

void nj_mp4_rewind(nj_mp4 *m)
{
    nj_u4 count;

    m->next = 0u;
    m->chunk = 0u;
    m->in_chunk = 0u;
    m->chunk_used = 0u;
    m->chunk_at = 0u;
    m->spc = 0u;
    m->stsc_idx = 0u;
    m->dts = 0u;
    m->delta = 0u;
    m->stts_idx = 0u;
    m->stts_left = 0u;
    m->stss_idx = 0u;

    count = nj_mp4_rd32(m->data + m->stts_at);
    m->stts_left = count;
    if (count != 0u) {
        m->delta = nj_mp4_rd32(m->data + m->stts_at + 4u);
    }
}

int nj_mp4_next(nj_mp4 *m, nj_mp4_sample *s)
{
    nj_u4 size, off;

    if (s == NULL || m->next >= m->sample_count) {
        return 0;
    }

    if (m->chunk == 0u) {
        m->chunk = 1u;
        m->stsc_idx = 0u;
        m->spc = nj_mp4_stsc_spc(m, 0u);
        m->chunk_at = nj_mp4_chunk_at(m, 0u);
        m->in_chunk = 0u;
        m->chunk_used = 0u;
    } else if (m->in_chunk >= m->spc) {
        if (m->chunk >= m->stco_count) {
            return NJ_MP4_ERR_FORMAT;
        }
        m->chunk++;
        while (m->stsc_idx + 1u < m->stsc_count &&
               nj_mp4_stsc_first(m, m->stsc_idx + 1u) <= m->chunk) {
            m->stsc_idx++;
        }
        m->spc = nj_mp4_stsc_spc(m, m->stsc_idx);
        m->chunk_at = nj_mp4_chunk_at(m, m->chunk - 1u);
        m->in_chunk = 0u;
        m->chunk_used = 0u;
    }

    size = nj_mp4_sample_size(m, m->next);
    if (size == 0u || m->chunk_at > m->size ||
        m->chunk_used > m->size - m->chunk_at) {
        return NJ_MP4_ERR_FORMAT;
    }
    off = m->chunk_at + m->chunk_used;
    if (!nj_mp4_in_mdat(m, off, size)) {
        return NJ_MP4_ERR_FORMAT;
    }

    s->offset = off;
    s->size = size;
    s->dts = m->dts;
    if (m->stts_left == 0u) {
        return NJ_MP4_ERR_FORMAT;
    }
    s->duration = m->delta;
    if (m->stss_count == 0u) {
        s->key = 1u;
    } else {
        nj_u4 n = m->next + 1u;
        if (m->stss_idx < m->stss_count &&
            nj_mp4_tab32(m, m->stss_at, m->stss_idx) == n) {
            s->key = 1u;
            m->stss_idx++;
        } else {
            s->key = 0u;
        }
    }

    m->next++;
    m->in_chunk++;
    m->chunk_used += size;
    m->dts += m->delta;
    m->stts_left--;
    if (m->stts_left == 0u && m->stts_idx + 1u < m->stts_count) {
        m->stts_idx++;
        m->stts_left = nj_mp4_rd32(m->data + m->stts_at + m->stts_idx * 8u);
        m->delta = nj_mp4_rd32(m->data + m->stts_at + m->stts_idx * 8u + 4u);
    }
    return 1;
}

#endif /* NJ_ENABLE_VIDEO */
