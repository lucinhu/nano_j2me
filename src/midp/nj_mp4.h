/*
 * nano_j2me - nj_mp4.h
 *
 * ============================================================================
 *  ISO BASE MEDIA (MP4 / 3GP) VIDEO SAMPLE ITERATOR
 * ============================================================================
 *
 * A 3GP clip is an ISO base media file: a tree of boxes whose 'moov' holds the
 * sample tables, and whose 'mdat' holds the samples.  The bytes of one video
 * frame are only reachable through three tables that describe chunks:
 *
 *   stsz   how many bytes each sample has
 *   stsc   how many samples each chunk holds, and from which chunk that count
 *          applies
 *   stco   where each chunk starts ('co64' when the file needs 64-bit offsets)
 *
 * This file walks that machinery for the one video track a J2ME title can use
 * and hands out each sample's offset, size and decode time.  It does not copy
 * the samples and it does not allocate: the caller keeps the whole file alive
 * and the iterator reads the tables in place, so a 3GP that fits in a JAR is
 * decoded straight out of the JAR's in-memory entry.
 *
 * Everything here is a pure parser.  No decoder state, no globals, no writes
 * outside the caller's nj_mp4.  Every size, count, offset and index read from
 * the file is bounds checked before use, and the iterator refuses a file whose
 * tables disagree with each other rather than producing samples at a guessed
 * offset: the input is a game loader's file, so a wrong offset is a read of
 * somebody else's memory.
 *
 * Supported: ftyp, moov/mvhd, trak/tkhd, mdia/mdhd/hdlr, minf/stbl with stsd
 * (one visual entry), stts, stsc, stsz, stco/co64 and stss.  Fragmented
 * movies, multiple sample descriptions, edit lists with media time mapping and
 * external data references are refused.  B-frames are not a container
 * problem here (the sample table is still linear); a 'ctts' box is ignored.
 */
#ifndef NJ_MP4_H
#define NJ_MP4_H

#include "nj2me/nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Room for the 'mdat' boxes of one file.  A muxer writes one; a fragmented or
 * edited file can have more.  Past this the file is refused, not truncated. */
#define NJ_MP4_MDAT_MAX 8

/* nj_mp4_open() results.  NJ_MP4_OK is 0; errors are negative and distinct so
 * the provider can pick an error message and code without guessing. */
#define NJ_MP4_OK             0
#define NJ_MP4_ERR_FORMAT    (-1)   /* not an ISO base media file / damaged  */
#define NJ_MP4_ERR_NO_VIDEO  (-2)   /* no usable video track                 */
#define NJ_MP4_ERR_CODEC     (-3)   /* a video track, but not H.263          */
#define NJ_MP4_ERR_FEATURE   (-4)   /* valid container we do not decode      */
/* Audio-track results.  They are separate from the video codes above so a
 * caller that wants both tracks can tell "this file has no sound" from "this
 * file has a sound track this runtime cannot decode". */
#define NJ_MP4_ERR_NO_AUDIO    (-5) /* no sound track at all                 */
#define NJ_MP4_ERR_AUDIO_CODEC (-6) /* a sound track, but not AMR-NB (samr)  */

/* Sample entry fourccs this iterator recognises.  s263 is what the 3GP spec
 * names H.263; H263/h263 appear in QuickTime-derived files. */
#define NJ_MP4_CODEC_S263   0x73323633u   /* 's263' */
#define NJ_MP4_CODEC_H263   0x48323633u   /* 'H263' */
#define NJ_MP4_CODEC_H263LC 0x68323633u   /* 'h263' */
#define NJ_MP4_CODEC_MP4V   0x6d703476u   /* 'mp4v' */
/* Audio sample entry this iterator recognises: AMR narrowband inside 3GP.  Each
 * sample is exactly one AMR frame (its first byte is the AMR ToC), which is the
 * granularity the runtime's AMR decoder takes. */
#define NJ_MP4_CODEC_SAMR   0x73616d72u   /* 'samr' */

/* Track kinds for nj_mp4_open_track(). */
#define NJ_MP4_TRACK_VIDEO  0
#define NJ_MP4_TRACK_AUDIO  1

/* One sample.  `offset' is absolute in the caller's buffer, `dts' is in the
 * track's media timescale (see mp4->timescale), `key' is 1 for a sync sample. */
typedef struct nj_mp4_sample {
    nj_u4 offset;
    nj_u4 size;
    nj_u4 dts;
    nj_u4 duration;
    nj_u1 key;
} nj_mp4_sample;

typedef struct nj_mp4 {
    const nj_u1 *data;
    nj_u4        size;

    /* Selected track. */
    nj_u4        timescale;      /* mdhd timescale, nonzero                  */
    nj_u4        duration;       /* track duration in timescale units       */
    nj_u4        width;          /* video only; 0 on an audio track         */
    nj_u4        height;         /* video only                              */
    nj_u4        fps;            /* video only; rounded average, 0 unknown  */
    nj_u4        channels;       /* audio only: 1 for AMR-NB                */
    nj_u4        sample_rate;    /* audio only: 8000 for AMR-NB             */
    nj_u4        codec;          /* NJ_MP4_CODEC_* fourcc                   */
    nj_u4        sample_count;

    /* Table locations: absolute byte offset of the first entry, and the entry
     * count.  Zero count means the box was absent.  Sizes were bounds checked
     * by nj_mp4_open(). */
    nj_u4        stsz_at;        /* sizes, or 0 when stsz_size is constant  */
    nj_u4        stsz_count;
    nj_u4        stsz_size;
    nj_u4        stsc_at;
    nj_u4        stsc_count;
    nj_u4        stco_at;
    nj_u4        stco_count;
    int          stco64;
    nj_u4        stts_at;
    nj_u4        stts_count;
    nj_u4        stss_at;        /* 0 when not present (every sample sync)  */
    nj_u4        stss_count;

    /* 'esds' DecoderSpecificInfo (the MPEG-4 video VOL / H.263 picture config
     * bytes): absolute offset in the caller's buffer and its length.  Zero
     * length means the file carried no decoder config. */
    nj_u4        cfg_at;
    nj_u4        cfg_size;

    /* 'mdat' payload ranges, [at, end). */
    nj_u4        mdat_at[NJ_MP4_MDAT_MAX];
    nj_u4        mdat_end[NJ_MP4_MDAT_MAX];
    int          mdat_count;

    /* Walk state.  `chunk' is 1-based and 0 before the first sample; `in_chunk'
     * is how many samples of that chunk have been handed out; `chunk_used' is
     * the byte offset inside it. */
    nj_u4        next;
    nj_u4        chunk;
    nj_u4        in_chunk;
    nj_u4        chunk_used;
    nj_u4        chunk_at;
    nj_u4        spc;
    nj_u4        stsc_idx;
    nj_u4        dts;
    nj_u4        delta;
    nj_u4        stts_idx;
    nj_u4        stts_left;
    nj_u4        stss_idx;
} nj_mp4;

/*
 * Parse the container and select a track of `kind' - NJ_MP4_TRACK_VIDEO or
 * NJ_MP4_TRACK_AUDIO.  The selected track's tables are cross-checked and
 * `nj_mp4_next()' then hands out its samples; audio samples are one AMR frame
 * each.  Returns NJ_MP4_OK or one of the NJ_MP4_ERR_* values: a video request
 * can answer NO_VIDEO/CODEC/FEATURE, an audio request NO_AUDIO, AUDIO_CODEC or
 * FEATURE, and damaged containers/tables answer FORMAT for both.  A request
 * with an unknown kind answers FORMAT.
 */
int nj_mp4_open_track(nj_mp4 *mp4, const void *data, nj_u4 size, int kind);

/*
 * Parse the container and select its AMR-NB ('samr') sound track.  On success
 * mp4->codec is NJ_MP4_CODEC_SAMR, mp4->timescale is the audio media timescale
 * (8000 in the corpus movies), mp4->channels is 1, mp4->sample_rate is 8000,
 * and mp4->width/height/fps are zero.  Each nj_mp4_next() sample is one AMR
 * frame, ready for the runtime's AMR decoder (which consumes #!AMR-style
 * frames).  Returns NJ_MP4_ERR_NO_AUDIO when the file has no sound track,
 * NJ_MP4_ERR_AUDIO_CODEC when it has one that is not AMR-NB,
 * NJ_MP4_ERR_FORMAT when its tables disagree, and NJ_MP4_ERR_FEATURE for a
 * container shape this parser refuses.
 */
int nj_mp4_open_audio(nj_mp4 *mp4, const void *data, nj_u4 size);

/*
 * Parse the container and select the video track.  This is
 * nj_mp4_open_track(mp4, data, size, NJ_MP4_TRACK_VIDEO) and keeps the
 * behaviour and error codes it has always had.  Returns NJ_MP4_OK, or one
 * of the NJ_MP4_ERR_* values.  If an H.263 track exists it is preferred over
 * an MPEG-4 Part 2 one, even when both are present: the H.263 decoder is the
 * one this runtime ships.  A file with no video track, a video track whose
 * descriptor is not one of the fourccs above, or inconsistent sample tables is
 * refused.  Zero-payload 'mdat' placeholder boxes are skipped; a file whose
 * 'mdat' boxes are all empty has nowhere for a sample to live and is refused.
 * On failure the iterator contents are unspecified.
 */
int nj_mp4_open(nj_mp4 *mp4, const void *data, nj_u4 size);

/*
 * Find the DecoderSpecificInfo payload (tag 0x04) inside an 'esds' box body:
 * `at' is the first byte of the box body (after the 8-byte box header) and
 * `end' the byte after it.  Returns 1 and fills *cfg_at / *cfg_size with the
 * payload location, or 0 when the box holds no decoder config.  Exposed so the
 * decoder-specific descriptors can be unit tested without a whole movie.
 */
int nj_mp4_esds_find(const nj_u1 *b, nj_u4 at, nj_u4 end,
                     nj_u4 *cfg_at, nj_u4 *cfg_size);

/* Reset the walk to the first sample. */
void nj_mp4_rewind(nj_mp4 *mp4);

/*
 * Hand out the next sample.  Returns 1 and fills *sample, 0 at the end of the
 * track, or NJ_MP4_ERR_FORMAT when a table contradicts another one or a sample
 * falls outside the file and every 'mdat'.  Sequential only; rewind to start
 * over.  `dts' starts at zero: edit lists are not applied.
 */
int nj_mp4_next(nj_mp4 *mp4, nj_mp4_sample *sample);

#ifdef __cplusplus
}
#endif

#endif /* NJ_MP4_H */
