/*
 * nano_j2me - nj_amr_mp4.c
 *
 * See nj_amr_mp4.h for what this is and why.  The short version: a JAR may
 * name a file ".amr" and hold an MP4 inside it, and the samples of the 'samr'
 * track are then reachable only through the sample tables.
 *
 * Only the boxes needed to find samples are understood - moov/trak/mdia/minf/
 * stbl, stsd's 'samr' entry with its 'damr' child, stsz, stsc, stco/co64, and
 * the 'mdat' payload ranges.  Everything else in the container is skipped by
 * size, which is the whole point of a box format.  A file that needs anything
 * more (fragmented movies, multiple sample descriptions, external data
 * references, edits) is refused rather than guessed at.
 */
#include "nj_amr_mp4.h"

#if NJ_ENABLE_AMR

#include <string.h>

#define NJ_FOURCC(a, b, c, d) \
    (((nj_u4)(a) << 24) | ((nj_u4)(b) << 16) | ((nj_u4)(c) << 8) | (nj_u4)(d))

#define NJ_BOX_MOOV NJ_FOURCC('m', 'o', 'o', 'v')
#define NJ_BOX_MDAT NJ_FOURCC('m', 'd', 'a', 't')
#define NJ_BOX_TRAK NJ_FOURCC('t', 'r', 'a', 'k')
#define NJ_BOX_MDIA NJ_FOURCC('m', 'd', 'i', 'a')
#define NJ_BOX_MINF NJ_FOURCC('m', 'i', 'n', 'f')
#define NJ_BOX_STBL NJ_FOURCC('s', 't', 'b', 'l')
#define NJ_BOX_STSD NJ_FOURCC('s', 't', 's', 'd')
#define NJ_BOX_STSZ NJ_FOURCC('s', 't', 's', 'z')
#define NJ_BOX_STSC NJ_FOURCC('s', 't', 's', 'c')
#define NJ_BOX_STCO NJ_FOURCC('s', 't', 'c', 'o')
#define NJ_BOX_CO64 NJ_FOURCC('c', 'o', '6', '4')
#define NJ_BOX_SAMR NJ_FOURCC('s', 'a', 'm', 'r')
#define NJ_BOX_DAMR NJ_FOURCC('d', 'a', 'm', 'r')

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
 * One box header at b, with avail bytes left in the parent.  Returns the header
 * length (8 or 16) or zero when the box does not fit.  A 64-bit size whose high
 * half is nonzero describes a file larger than this runtime can address, so it
 * is refused like a truncated one; a size of zero means "to the end of the
 * parent", which is legal for the last box.
 */
static nj_u4 nj_mp4_head(const nj_u1 *b, nj_u4 avail, nj_u4 *type,
                         nj_u4 *total)
{
    nj_u4 size;

    if (avail < 8u) {
        return 0u;
    }
    size = nj_mp4_rd32(b);
    *type = nj_mp4_rd32(b + 4);
    if (size == 1u) {
        nj_u4 hi, lo;
        if (avail < 16u) {
            return 0u;
        }
        hi = nj_mp4_rd32(b + 8);
        lo = nj_mp4_rd32(b + 12);
        if (hi != 0u || lo < 16u || lo > avail) {
            return 0u;
        }
        *total = lo;
        return 16u;
    }
    if (size == 0u) {
        *total = avail;
        return 8u;
    }
    if (size < 8u || size > avail) {
        return 0u;
    }
    *total = size;
    return 8u;
}

/* First child of this type: body and length.  Zero when absent or malformed. */
static int nj_mp4_find(const nj_u1 *b, nj_u4 avail, nj_u4 type,
                       const nj_u1 **body, nj_u4 *blen)
{
    nj_u4 pos = 0u;

    while (pos < avail) {
        nj_u4 t, total, hdr = nj_mp4_head(b + pos, avail - pos, &t, &total);
        if (hdr == 0u) {
            return 0;
        }
        if (t == type) {
            *body = b + pos + hdr;
            *blen = total - hdr;
            return 1;
        }
        pos += total;
    }
    return 0;
}

/* Is [off, off+need) inside one of the file's 'mdat' payloads? */
static int nj_mp4_in_mdat(const nj_amr_mp4 *it, nj_u4 off, nj_u4 need)
{
    int i;

    if (off > it->size || need > it->size - off) {
        return 0;
    }
    for (i = 0; i < it->mdat_count; i++) {
        nj_u4 at = it->mdat_at[i], end = it->mdat_end[i];
        if (off >= at && off <= end && need <= end - off) {
            return 1;
        }
    }
    return 0;
}

static nj_u4 nj_mp4_chunk_offset(const nj_amr_mp4 *it, nj_u4 chunk)
{
    const nj_u1 *p = it->data + it->stco_at;

    if (it->stco64) {
        /* The high half was checked when the table was read. */
        return nj_mp4_rd32(p + (nj_size)chunk * 8u + 4u);
    }
    return nj_mp4_rd32(p + (nj_size)chunk * 4u);
}

/*
 * stsd: exactly one entry, and it must be 'samr'.  Returns 1 when this track is
 * AMR and its 'damr' box says how many frames a sample carries; 0 when the
 * track is something else; -1 when it is AMR but not one this reader accepts,
 * which fails the whole file because there is no second reading to try.
 */
static int nj_mp4_stsd(nj_amr_mp4 *it, const nj_u1 *sd, nj_u4 len)
{
    const nj_u1 *e, *damr;
    nj_u4 count, pos, esize, fmt, elen, dlen;

    if (len < 8u) {
        return -1;
    }
    count = nj_mp4_rd32(sd + 4);
    if (count != 1u) {
        return -1;              /* one sample description, no ambiguity */
    }
    pos = 8u;
    if (len - pos < 8u) {
        return -1;
    }
    esize = nj_mp4_rd32(sd + pos);
    fmt = nj_mp4_rd32(sd + pos + 4);
    if (esize < 8u || esize > len - pos) {
        return -1;
    }
    if (fmt != NJ_BOX_SAMR) {
        return 0;               /* some other codec: not our track */
    }
    /* SampleEntry(8) + AudioSampleEntry(20) before the child boxes. */
    if (esize < 8u + 28u) {
        return -1;
    }
    e = sd + pos + 8u;
    elen = esize - 8u;
    if (nj_mp4_rd16(e + 16u) != 1u) {
        return -1;              /* AMR-NB is mono */
    }
    {
        nj_u4 rate = nj_mp4_rd32(e + 24u);
        if (rate != 0u && (rate >> 16) != 8000u) {
            return -1;          /* AMR-NB is 8 kHz */
        }
    }
    if (!nj_mp4_find(e + 28u, elen - 28u, NJ_BOX_DAMR, &damr, &dlen)) {
        return -1;
    }
    if (dlen < 9u) {
        return -1;
    }
    /* 3GPP TS 26.244: vendor(4) decoder_version(1) mode_set(2) then one
     * reserved bit and seven bits of frames per sample. */
    {
        nj_u4 fps = (nj_u4)damr[8] & 0x7Fu;
        if (fps < 1u || fps > 64u) {
            return -1;
        }
        it->frames_per_sample = fps;
    }
    return 1;
}

/* stsz: one constant size, or one entry per sample.  Table must fit its box. */
static int nj_mp4_stsz(nj_amr_mp4 *it, const nj_u1 *b, nj_u4 len)
{
    nj_u4 constant, count;

    if (len < 12u) {
        return 0;
    }
    constant = nj_mp4_rd32(b + 4);
    count = nj_mp4_rd32(b + 8);
    if (count == 0u) {
        return 0;
    }
    if (constant != 0u) {
        it->stsz_at = 0u;
        it->stsz_size = constant;
    } else {
        if (count > (len - 12u) / 4u) {
            return 0;
        }
        it->stsz_at = (nj_u4)(b + 12u - it->data);
        it->stsz_size = 0u;
    }
    it->stsz_count = count;
    return 1;
}

/*
 * stsc: chunk 1 starts at the first entry, and the entries map a growing run of
 * chunk numbers to a sample count.  Strictly increasing first_chunk, a positive
 * count, and sample description 1 (there is only one) are the invariants the
 * validator relies on.
 */
static int nj_mp4_stsc(nj_amr_mp4 *it, const nj_u1 *b, nj_u4 len)
{
    nj_u4 count, i, prev = 0u;

    if (len < 8u) {
        return 0;
    }
    count = nj_mp4_rd32(b + 4);
    if (count == 0u || count > (len - 8u) / 12u) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        const nj_u1 *e = b + 8u + (nj_size)i * 12u;
        nj_u4 first = nj_mp4_rd32(e);
        nj_u4 spc = nj_mp4_rd32(e + 4);
        nj_u4 sdi = nj_mp4_rd32(e + 8);

        if (spc == 0u || sdi != 1u) {
            return 0;
        }
        if (i == 0u) {
            if (first != 1u) {
                return 0;
            }
        } else if (first <= prev) {
            return 0;
        }
        prev = first;
    }
    it->stsc_at = (nj_u4)(b + 8u - it->data);
    it->stsc_count = count;
    return 1;
}

/* stco/co64: chunk offsets.  A 64-bit offset above 4 GiB is refused. */
static int nj_mp4_stco(nj_amr_mp4 *it, const nj_u1 *b, nj_u4 len, int is64)
{
    nj_u4 count, i;
    nj_u4 step = is64 ? 8u : 4u;

    if (len < 8u) {
        return 0;
    }
    count = nj_mp4_rd32(b + 4);
    if (count == 0u || count > (len - 8u) / step) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        const nj_u1 *e = b + 8u + (nj_size)i * step;
        if (is64 && nj_mp4_rd32(e) != 0u) {
            return 0;
        }
    }
    it->stco_at = (nj_u4)(b + 8u - it->data);
    it->stco_count = count;
    it->stco64 = is64 ? 1 : 0;
    return 1;
}

/*
 * Cross-check the three tables against each other and against the file: the
 * sample count they imply must be the one stsz declares, and every chunk must
 * start inside an 'mdat'.  Per-sample bounds are checked as samples are handed
 * out, because that is where the running chunk offset is known.
 */
static int nj_mp4_validate(nj_amr_mp4 *it)
{
    const nj_u1 *stsc = it->data + it->stsc_at;
    nj_u4 i;
    nj_u8 total = 0u;

    if (it->stsc_count == 0u || it->stco_count == 0u ||
        it->stsz_count == 0u) {
        return 0;
    }
    for (i = 0u; i < it->stsc_count; i++) {
        nj_u4 first = nj_mp4_rd32(stsc + (nj_size)i * 12u);
        nj_u4 spc = nj_mp4_rd32(stsc + (nj_size)i * 12u + 4u);
        nj_u4 last = (i + 1u < it->stsc_count)
                     ? nj_mp4_rd32(stsc + (nj_size)(i + 1u) * 12u) - 1u
                     : it->stco_count;

        if (first > it->stco_count || last < first ||
            last > it->stco_count) {
            return 0;
        }
        total += (nj_u8)spc * (nj_u8)(last - first + 1u);
        if (total > (nj_u8)it->stsz_count) {
            return 0;
        }
    }
    if (total != (nj_u8)it->stsz_count) {
        return 0;
    }
    for (i = 0u; i < it->stco_count; i++) {
        if (!nj_mp4_in_mdat(it, nj_mp4_chunk_offset(it, i), 1u)) {
            return 0;
        }
    }
    return 1;
}

/*
 * One trak: walk the single path down to stbl and read the tables.  Returns 1
 * for an acceptable AMR track, 0 when the track is not AMR, -1 when it is AMR
 * but the tables do not hold together.
 */
static int nj_mp4_trak(nj_amr_mp4 *it, const nj_u1 *trak, nj_u4 len)
{
    const nj_u1 *mdia, *minf, *stbl, *box;
    nj_u4 mlen, nlen, slen, blen;
    int st;

    if (!nj_mp4_find(trak, len, NJ_BOX_MDIA, &mdia, &mlen) ||
        !nj_mp4_find(mdia, mlen, NJ_BOX_MINF, &minf, &nlen) ||
        !nj_mp4_find(minf, nlen, NJ_BOX_STBL, &stbl, &slen)) {
        return 0;
    }
    if (!nj_mp4_find(stbl, slen, NJ_BOX_STSD, &box, &blen)) {
        return 0;
    }
    st = nj_mp4_stsd(it, box, blen);
    if (st <= 0) {
        return st;
    }
    if (!nj_mp4_find(stbl, slen, NJ_BOX_STSZ, &box, &blen) ||
        !nj_mp4_stsz(it, box, blen)) {
        return -1;
    }
    if (!nj_mp4_find(stbl, slen, NJ_BOX_STSC, &box, &blen) ||
        !nj_mp4_stsc(it, box, blen)) {
        return -1;
    }
    if (nj_mp4_find(stbl, slen, NJ_BOX_STCO, &box, &blen)) {
        if (!nj_mp4_stco(it, box, blen, 0)) {
            return -1;
        }
    } else if (nj_mp4_find(stbl, slen, NJ_BOX_CO64, &box, &blen)) {
        if (!nj_mp4_stco(it, box, blen, 1)) {
            return -1;
        }
    } else {
        return -1;
    }
    return nj_mp4_validate(it) ? 1 : -1;
}

int nj_amr_mp4_open(nj_amr_mp4 *it, const void *data, nj_u4 size)
{
    const nj_u1 *b = (const nj_u1 *)data;
    const nj_u1 *moov = NULL;
    nj_u4 moov_len = 0u, pos = 0u, type = 0u, total = 0u;
    int found = 0;

    if (it == NULL || b == NULL || size < 8u) {
        return 0;
    }
    memset(it, 0, sizeof(*it));
    it->data = b;
    it->size = size;

    /* Top level: remember the moov payload and every mdat payload range. */
    while (pos < size) {
        nj_u4 hdr = nj_mp4_head(b + pos, size - pos, &type, &total);
        if (hdr == 0u) {
            return 0;
        }
        if (type == NJ_BOX_MDAT) {
            if (it->mdat_count >= NJ_AMR_MP4_MDAT) {
                return 0;
            }
            it->mdat_at[it->mdat_count] = pos + hdr;
            it->mdat_end[it->mdat_count] = pos + total;
            it->mdat_count++;
        } else if (type == NJ_BOX_MOOV) {
            if (moov != NULL) {
                return 0;       /* one movie, no ambiguity */
            }
            moov = b + pos + hdr;
            moov_len = total - hdr;
        }
        pos += total;
    }
    if (moov == NULL || it->mdat_count == 0) {
        return 0;
    }

    /* moov holds the tracks; exactly one of them may be AMR. */
    pos = 0u;
    while (pos < moov_len) {
        nj_u4 hdr = nj_mp4_head(moov + pos, moov_len - pos, &type, &total);
        int st;

        if (hdr == 0u) {
            return 0;
        }
        if (type == NJ_BOX_TRAK) {
            st = nj_mp4_trak(it, moov + pos + hdr, total - hdr);
            if (st < 0) {
                return 0;
            }
            if (st > 0) {
                if (found) {
                    return 0;   /* two AMR tracks: which one is the sound? */
                }
                found = 1;
            }
        }
        pos += total;
    }
    return found;
}

int nj_amr_mp4_next(nj_amr_mp4 *it, const nj_u1 **sample, nj_u4 *bytes)
{
    const nj_u1 *stsc;
    nj_u4 size, off;

    if (it == NULL || sample == NULL || bytes == NULL) {
        return -1;
    }
    if (it->index >= it->stsz_count) {
        return 0;
    }
    if (it->in_chunk == 0u) {
        stsc = it->data + it->stsc_at;
        while (it->entry + 1u < it->stsc_count &&
               it->chunk + 1u >=
                   nj_mp4_rd32(stsc + (nj_size)(it->entry + 1u) * 12u)) {
            it->entry++;
        }
        it->spc = nj_mp4_rd32(stsc + (nj_size)it->entry * 12u + 4u);
        it->chunk_at = nj_mp4_chunk_offset(it, it->chunk);
        it->used = 0u;
    }
    size = (it->stsz_size != 0u)
           ? it->stsz_size
           : nj_mp4_rd32(it->data + it->stsz_at + (nj_size)it->index * 4u);
    if (size == 0u) {
        return -1;              /* an empty AMR sample is not a frame */
    }
    off = it->chunk_at + it->used;
    if (off < it->chunk_at || !nj_mp4_in_mdat(it, off, size)) {
        return -1;
    }
    *sample = it->data + off;
    *bytes = size;
    it->used += size;
    it->in_chunk++;
    it->index++;
    if (it->in_chunk >= it->spc) {
        it->chunk++;
        it->in_chunk = 0u;
        it->used = 0u;
    }
    return 1;
}

#else  /* !NJ_ENABLE_AMR */

/* ISO C forbids an empty translation unit, and a build with AMR off is exactly
 * that. */
typedef int nj_amr_mp4_disabled_;

#endif /* NJ_ENABLE_AMR */
