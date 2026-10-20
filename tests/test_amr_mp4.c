/*
 * nano_j2me - the AMR narrowband sample iterator for MP4/3GP containers.
 *
 * Three assets in the collection are named ".amr" while holding an ISO base
 * media file: Doom's sound effects and its 3gp video.  Those files carry the
 * speech inside a 'samr' track, reachable only through the sample tables, and
 * nj_amr_mp4.c is the reverse of muxing that finds them.  This is the test for
 * that walk, where a Player cannot see: which box layouts are accepted, that a
 * chunk's samples come out in table order at the right byte offsets, and that
 * the bytes handed to the decoder are the frames a "#!AMR" stream would hold.
 *
 * Every input is built in memory, byte by byte, by the box writers below - no
 * game asset is copied and nothing is read from disk.  The sample bytes are the
 * same synthetic AMR frames tests/test_amr.c decodes, so a walking or offset
 * error shows up as a PCM disagreement rather than as a merely plausible
 * number.  The container shape (an 'ftyp' box, a 'moov' of one 'samr' track
 * whose 'damr' child declares framesPerSample, the three sample tables, and
 * an 'mdat') and the 17-byte 'damr' box are the ones the real files carry,
 * checked with xxd against snd/pistol_use_01.amr in Doom's JAR.
 *
 * No heap, no I/O, four kilobytes of static buffers.
 *
 * Usage: test_amr_mp4
 */
#include "nj_amr.h"
#include "nj_amr_mp4.h"

#include <stdio.h>
#include <string.h>

#define FILE_MAX    4096
#define MAX_FRAMES  8
#define MAX_SAMPLES 8
#define MAX_CHUNKS  4
#define PCM_MAX     (MAX_FRAMES * (int)NJ_AMR_BLOCK * 2)

static int checks = 0;
static int fails = 0;

static void ok(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        printf("  FAIL %s\n", what);
    }
}

/* ------------------------------------------------------------------ */
/* Synthetic AMR frames, the same bytes tests/test_amr.c builds.       */
/* ------------------------------------------------------------------ */

static nj_u1 frame_payload(int mode, int i)
{
    return (nj_u1)((i * 37 + 11 + mode * 5) & 0xFF);
}

/* One speech frame's header: mode, quality bit set. */
static nj_u1 frame_header(int mode)
{
    return (nj_u1)(((nj_u4)mode << 3) | 0x04u);
}

static int frame_payload_bytes(int mode)
{
    static const int bytes[NJ_AMR_MODES] = {
        12, 13, 15, 17, 19, 20, 26, 31, 5
    };
    return bytes[mode];
}

static nj_u1 frames[MAX_FRAMES * 64];
static nj_u4 frame_at[MAX_FRAMES];
static nj_u4 frame_bytes[MAX_FRAMES];
static nj_u4 frames_len;

/* Build the frame store: modes[0], modes[1], ... on the wire, in order. */
static void make_frames(const int *modes, int count)
{
    int i;
    int j;

    frames_len = 0u;
    for (i = 0; i < count; i++) {
        frame_at[i] = frames_len;
        frames[frames_len++] = frame_header(modes[i]);
        for (j = 0; j < frame_payload_bytes(modes[i]); j++) {
            frames[frames_len++] = frame_payload(modes[i], j);
        }
        frame_bytes[i] = frames_len - frame_at[i];
    }
}

/* The concatenated sample bytes a valid fixture should carry in its 'mdat'. */
static nj_u1 track[FILE_MAX];
static nj_u4 track_len;

/* ------------------------------------------------------------------ */
/* The fixture: one 'samr' track laid out the way a muxer would.       */
/* ------------------------------------------------------------------ */

typedef struct fixture {
    /* Samples: sample i is frames[first_frame[i] .. +nframes[i]-1]. */
    int    nsamples;
    int    first_frame[MAX_SAMPLES];
    int    nframes[MAX_SAMPLES];

    /* stsz: constant sample size (only legal when all samples are equal), or
     * one entry per sample. */
    int    const_stsz;

    /* stsc and chunk layout: chunk k holds chunk_n[k] samples. */
    int    nchunks;
    int    chunk_n[MAX_CHUNKS];
    int    nstsc;
    nj_u4  stsc_first[MAX_CHUNKS];
    nj_u4  stsc_spc[MAX_CHUNKS];

    /* Variants. */
    int    co64;            /* 'co64' instead of 'stco' */
    int    ntraks;          /* 1, or 2 to make the file ambiguous */
    int    second_mp4a;     /* second track's sample entry is 'mp4a' */
    int    entry_mp4a;      /* the only track's sample entry is 'mp4a' */
    int    no_ftyp;
    int    no_mdat;
    int    no_damr;
    int    fps;             /* 'damr' framesPerSample */
    int    stsz_count_delta;/* lie about the sample count by this much */
    int    mdat_bytes;      /* 0: write every sample; else truncate the mdat */
    nj_u4  mdat_skip;       /* shift every chunk offset out of the mdat */
} fixture;

/* How many bytes sample i occupies. */
static nj_u4 sample_size(const fixture *f, int i)
{
    nj_u4 n = 0u;
    int j;

    for (j = 0; j < f->nframes[i]; j++) {
        n += frame_bytes[f->first_frame[i] + j];
    }
    return n;
}

/* Lay the sample bytes out in sample order. */
static void make_track(const fixture *f)
{
    int i;
    int j;

    track_len = 0u;
    for (i = 0; i < f->nsamples; i++) {
        for (j = 0; j < f->nframes[i]; j++) {
            int fr = f->first_frame[i] + j;
            memcpy(track + track_len, frames + frame_at[fr], frame_bytes[fr]);
            track_len += frame_bytes[fr];
        }
    }
}

/* ------------------------------------------------------------------ */
/* Big-endian box writer over one static buffer.                       */
/* ------------------------------------------------------------------ */

static nj_u1 file[FILE_MAX];
static nj_u4 bw;                        /* write cursor */
static nj_u4 offpatch_at[MAX_CHUNKS * 2];
static int   noffpatch;
static nj_u4 stsz_table_at;

static void w1(nj_u4 v)
{
    file[bw++] = (nj_u1)(v & 0xFFu);
}

static void w2(nj_u4 v)
{
    w1(v >> 8);
    w1(v);
}

static void w4(nj_u4 v)
{
    w1(v >> 24);
    w1(v >> 16);
    w1(v >> 8);
    w1(v);
}

static void w8(nj_u8 v)
{
    w4((nj_u4)(v >> 32));
    w4((nj_u4)(v & 0xFFFFFFFFu));
}

static void wbytes(const char *p, nj_u4 n)
{
    memcpy(file + bw, p, n);
    bw += n;
}

static void wzeros(nj_u4 n)
{
    memset(file + bw, 0, n);
    bw += n;
}

/* Open a box: write the size placeholder and the four-character type. */
static nj_u4 box_open(const char *type)
{
    nj_u4 h = bw;

    w4(0u);
    wbytes(type, 4u);
    return h;
}

/* Close it: the size is everything written since the box was opened. */
static void box_close(nj_u4 h)
{
    nj_u4 n = bw - h;

    file[h + 0] = (nj_u1)(n >> 24);
    file[h + 1] = (nj_u1)(n >> 16);
    file[h + 2] = (nj_u1)(n >> 8);
    file[h + 3] = (nj_u1)n;
}

/* A full box: version 0 and no flags. */
static void full0(void)
{
    w4(0u);
}

/*
 * ftyp: 20 bytes, the shape the real files have - isom, minor version, one
 * compatible brand.  Nothing in the reader looks inside it; it is here so the
 * fixtures start the way a file off the wire does.
 */
static void write_ftyp(void)
{
    nj_u4 h = box_open("ftyp");

    wbytes("isom", 4u);
    w4(0x00000200u);
    wbytes("isom", 4u);
    box_close(h);
}

/* mvhd: the movie header.  Irrelevant to the sample walk, present because
 * every muxed file has one and a reader that skipped 'moov' children by type
 * should not care what else is in there. */
static void write_mvhd(void)
{
    nj_u4 h = box_open("mvhd");

    full0();
    w4(0u);                             /* creation_time */
    w4(0u);                             /* modification_time */
    w4(8000u);                          /* timescale */
    w4(0u);                             /* duration */
    w4(0x00010000u);                    /* rate 1.0 */
    w2(0x0100u);                        /* volume 1.0 */
    w2(0u);                             /* reserved */
    w4(0u);
    w4(0u);                             /* reserved[2] */
    w4(0x00010000u); w4(0u); w4(0u);    /* matrix */
    w4(0u); w4(0x00010000u); w4(0u);
    w4(0u); w4(0u); w4(0x40000000u);
    wzeros(24u);                        /* pre_defined[6] */
    w4(2u);                             /* next_track_ID */
    box_close(h);
}

/* tkhd: one enabled track. */
static void write_tkhd(void)
{
    nj_u4 h = box_open("tkhd");

    w4(0x00000007u);                    /* version 0, enabled|movie|preview */
    w4(0u);                             /* creation_time */
    w4(0u);                             /* modification_time */
    w4(1u);                             /* track_ID */
    w4(0u);                             /* reserved */
    w4(0u);                             /* duration */
    w4(0u);
    w4(0u);                             /* reserved[2] */
    w2(0u);                             /* layer */
    w2(0u);                             /* alternate_group */
    w2(0x0100u);                        /* volume 1.0 */
    w2(0u);                             /* reserved */
    w4(0x00010000u); w4(0u); w4(0u);    /* matrix */
    w4(0u); w4(0x00010000u); w4(0u);
    w4(0u); w4(0u); w4(0x40000000u);
    w4(0u);                             /* width */
    w4(0u);                             /* height */
    box_close(h);
}

/* mdhd: the media header, 8 kHz because AMR-NB has no other rate. */
static void write_mdhd(void)
{
    nj_u4 h = box_open("mdhd");

    full0();
    w4(0u);                             /* creation_time */
    w4(0u);                             /* modification_time */
    w4(8000u);                          /* timescale */
    w4(0u);                             /* duration: the tables say what counts */
    w2(0x15C7u);                        /* language 'eng' */
    w2(0u);                             /* pre_defined */
    box_close(h);
}

/* hdlr: a sound track. */
static void write_hdlr(void)
{
    nj_u4 h = box_open("hdlr");

    full0();
    w4(0u);                             /* pre_defined */
    wbytes("soun", 4u);                 /* handler_type */
    w4(0u); w4(0u); w4(0u);             /* reserved[3] */
    wbytes("SoundHandler", 13u);        /* name, NUL included */
    box_close(h);
}

/* smhd: sound balance, all zero. */
static void write_smhd(void)
{
    nj_u4 h = box_open("smhd");

    full0();
    w2(0u);                             /* balance */
    w2(0u);                             /* reserved */
    box_close(h);
}

/* dinf/dref/url: one self-contained data reference. */
static void write_dinf(void)
{
    nj_u4 h = box_open("dinf");
    nj_u4 d = box_open("dref");
    nj_u4 u;

    full0();
    w4(1u);                             /* entry_count */
    u = box_open("url ");
    w4(0x00000001u);                    /* version 0, flags self-contained */
    box_close(u);
    box_close(d);
    box_close(h);
}

/*
 * damr: the AMR decoder configuration, 17 bytes total - vendor(4),
 * decoder_version(1), mode_set(2), reserved(1), frames_per_sample(1).  That is
 * exactly what the real files carry: "appl", 1, 0x0080, 0, 1.
 */
static void write_damr(const fixture *f)
{
    nj_u4 h = box_open("damr");

    wbytes("appl", 4u);                 /* vendor */
    w1(1u);                             /* decoder_version */
    w2(0x0080u);                        /* mode_set */
    w1(0u);                             /* reserved */
    w1((nj_u4)f->fps & 0x7Fu);          /* frames_per_sample */
    box_close(h);
}

/* One AudioSampleEntry body: 28 bytes before any child box. */
static void write_audio_entry_fixed(nj_u4 channelcount, nj_u4 rate)
{
    wzeros(6u);                         /* SampleEntry reserved */
    w2(1u);                             /* data_reference_index */
    w4(0u);
    w4(0u);                             /* AudioSampleEntry reserved[2] */
    w2(channelcount);                   /* channelcount: AMR-NB is mono */
    w2(16u);                            /* samplesize */
    w2(0u);                             /* pre_defined */
    w2(0u);                             /* reserved */
    w4(rate << 16);                     /* samplerate, 16.16 */
}

/* 'samr': AMR narrowband, mono 8 kHz, with its 'damr' child. */
static void write_samr_entry(const fixture *f)
{
    nj_u4 h = box_open("samr");

    write_audio_entry_fixed(1u, 8000u);
    if (!f->no_damr) {
        write_damr(f);
    }
    box_close(h);
}

/* 'mp4a': some other codec.  The reader must reject on the fourcc, before it
 * ever needs an 'esds'. */
static void write_mp4a_entry(void)
{
    nj_u4 h = box_open("mp4a");

    write_audio_entry_fixed(2u, 44100u);
    box_close(h);
}

static void write_stsd(const fixture *f, int mp4a)
{
    nj_u4 h = box_open("stsd");

    full0();
    w4(1u);                             /* entry_count */
    if (mp4a) {
        write_mp4a_entry();
    } else {
        write_samr_entry(f);
    }
    box_close(h);
}

/* stsz: a constant size, or one entry per sample. */
static void write_stsz(const fixture *f)
{
    nj_u4 h = box_open("stsz");
    int i;

    full0();
    w4(f->const_stsz ? sample_size(f, 0) : 0u);
    w4((nj_u4)(f->nsamples + f->stsz_count_delta));
    stsz_table_at = bw;
    if (!f->const_stsz) {
        for (i = 0; i < f->nsamples; i++) {
            w4(sample_size(f, i));
        }
    }
    box_close(h);
}

/* stsc: first_chunk, samples_per_chunk, sample_description_index. */
static void write_stsc(const fixture *f)
{
    nj_u4 h = box_open("stsc");
    int i;

    full0();
    w4((nj_u4)f->nstsc);
    for (i = 0; i < f->nstsc; i++) {
        w4(f->stsc_first[i]);
        w4(f->stsc_spc[i]);
        w4(1u);                         /* the one and only description */
    }
    box_close(h);
}

/* stco/co64: chunk offsets, patched once the 'mdat' position is known. */
static void write_stco(const fixture *f)
{
    nj_u4 h = box_open(f->co64 ? "co64" : "stco");
    int i;

    full0();
    w4((nj_u4)f->nchunks);
    for (i = 0; i < f->nchunks; i++) {
        offpatch_at[noffpatch++] = bw;
        if (f->co64) {
            w8(0u);
        } else {
            w4(0u);
        }
    }
    box_close(h);
}

static void write_stbl(const fixture *f, int mp4a)
{
    nj_u4 h = box_open("stbl");

    write_stsd(f, mp4a);
    write_stsz(f);
    write_stsc(f);
    write_stco(f);
    box_close(h);
}

static void write_minf(const fixture *f, int mp4a)
{
    nj_u4 h = box_open("minf");

    write_smhd();
    write_dinf();
    write_stbl(f, mp4a);
    box_close(h);
}

static void write_mdia(const fixture *f, int mp4a)
{
    nj_u4 h = box_open("mdia");

    write_mdhd();
    write_hdlr();
    write_minf(f, mp4a);
    box_close(h);
}

static void write_trak(const fixture *f, int mp4a)
{
    nj_u4 h = box_open("trak");

    write_tkhd();
    write_mdia(f, mp4a);
    box_close(h);
}

static void write_moov(const fixture *f)
{
    nj_u4 h = box_open("moov");

    write_mvhd();
    write_trak(f, f->entry_mp4a);
    if (f->ntraks > 1) {
        write_trak(f, f->second_mp4a);
    }
    box_close(h);
}

/* Where everything landed, for the tests that cut the file or look at a box. */
typedef struct layout {
    nj_u4 len;
    nj_u4 moov_at;
    nj_u4 moov_end;
    nj_u4 mdat_at;
    nj_u4 mdat_payload_at;
    nj_u4 mdat_end;
    nj_u4 stsz_table_at;
} layout;

static void patch_chunks(const fixture *f, const layout *L)
{
    nj_u4 base[MAX_CHUNKS];
    nj_u4 cum = 0u;
    int k;
    int i;
    int s = 0;

    if (f->no_mdat || noffpatch == 0 || f->nchunks == 0) {
        return;
    }
    for (k = 0; k < f->nchunks; k++) {
        base[k] = L->mdat_payload_at + cum + f->mdat_skip;
        for (i = 0; i < f->chunk_n[k]; i++) {
            cum += sample_size(f, s);
            s++;
        }
    }
    for (k = 0; k < noffpatch; k++) {
        nj_u4 v = base[k % f->nchunks];
        nj_u4 p = offpatch_at[k];

        if (f->co64) {
            nj_u8 v64 = (nj_u8)v;

            file[p + 0] = (nj_u1)(v64 >> 56);
            file[p + 1] = (nj_u1)(v64 >> 48);
            file[p + 2] = (nj_u1)(v64 >> 40);
            file[p + 3] = (nj_u1)(v64 >> 32);
            file[p + 4] = (nj_u1)(v64 >> 24);
            file[p + 5] = (nj_u1)(v64 >> 16);
            file[p + 6] = (nj_u1)(v64 >> 8);
            file[p + 7] = (nj_u1)v64;
        } else {
            file[p + 0] = (nj_u1)(v >> 24);
            file[p + 1] = (nj_u1)(v >> 16);
            file[p + 2] = (nj_u1)(v >> 8);
            file[p + 3] = (nj_u1)v;
        }
    }
}

/* Write ftyp, moov and mdat into file[], patch the chunk offsets, and say
 * where each box starts.  The 'mdat' is last so its payload position is known
 * by the time the offset table has to hold it. */
static void build_file(const fixture *f, layout *L)
{
    nj_u4 mh;
    nj_u4 payload;
    nj_u4 i;

    memset(L, 0, sizeof(*L));
    memset(file, 0, sizeof(file));
    bw = 0u;
    noffpatch = 0;
    stsz_table_at = 0u;

    if (!f->no_ftyp) {
        write_ftyp();
    }
    L->moov_at = bw;
    write_moov(f);
    L->moov_end = bw;

    make_track(f);
    if (!f->no_mdat) {
        L->mdat_at = bw;
        mh = box_open("mdat");
        L->mdat_payload_at = bw;
        payload = (f->mdat_bytes > 0) ? (nj_u4)f->mdat_bytes : track_len;
        for (i = 0u; i < payload; i++) {
            file[bw++] = (i < track_len) ? track[i] : (nj_u1)0u;
        }
        L->mdat_end = bw;
        box_close(mh);
    }
    L->len = bw;

    patch_chunks(f, L);
    L->stsz_table_at = stsz_table_at;
}

/* A bare 'ftyp': a file that names its container and holds nothing else. */
static nj_u4 build_ftyp_only(void)
{
    bw = 0u;
    write_ftyp();
    return bw;
}

/* ------------------------------------------------------------------ */
/* Fixtures used by more than one test.                                */
/* ------------------------------------------------------------------ */

/* One track, one chunk, one sample (which may hold several frames). */
static fixture base_fixture(int sample_frames)
{
    fixture f;

    memset(&f, 0, sizeof(f));
    f.nsamples = 1;
    f.first_frame[0] = 0;
    f.nframes[0] = sample_frames;
    f.const_stsz = 1;                   /* one sample of one size */
    f.nchunks = 1;
    f.chunk_n[0] = 1;
    f.nstsc = 1;
    f.stsc_first[0] = 1;
    f.stsc_spc[0] = 1;
    f.ntraks = 1;
    f.fps = 1;
    return f;
}

/* Three samples, two chunks: chunk 1 holds samples 0 and 1, chunk 2 holds
 * sample 2 (stsc entries {1,2} and {2,1}). */
static fixture two_chunk_fixture(void)
{
    fixture f;
    int i;

    memset(&f, 0, sizeof(f));
    f.nsamples = 3;
    for (i = 0; i < 3; i++) {
        f.first_frame[i] = i;
        f.nframes[i] = 1;
    }
    f.const_stsz = 0;                   /* a table, sample sizes differ */
    f.nchunks = 2;
    f.chunk_n[0] = 2;
    f.chunk_n[1] = 1;
    f.nstsc = 2;
    f.stsc_first[0] = 1;
    f.stsc_spc[0] = 2;
    f.stsc_first[1] = 2;
    f.stsc_spc[1] = 1;
    f.ntraks = 1;
    f.fps = 1;
    return f;
}

/* ------------------------------------------------------------------ */
/* Checks.                                                             */
/* ------------------------------------------------------------------ */

/*
 * A file that must play: open accepts it, the samples come out in table order
 * at the byte offsets the chunk mapping implies, nj_amr_mp4_frames counts the
 * frames the samples hold, and the PCM is nj_amr_decode's output for the same
 * frames behind a "#!AMR\n" header - byte for byte.
 */
static void plays(const char *name, const fixture *f)
{
    static nj_u1 pcm_mp4[PCM_MAX];
    static nj_u1 pcm_ref[PCM_MAX];
    static nj_u1 ref[FILE_MAX];
    layout L;
    nj_amr_mp4 it;
    const nj_u1 *s;
    nj_u4 n;
    nj_u4 expect_frames = 0u;
    nj_u4 ref_len;
    nj_u4 got;
    nj_u4 off;
    nj_u4 t;
    int r;
    int i;
    char what[128];

    build_file(f, &L);

    sprintf(what, "%s: open accepts the file", name);
    r = nj_amr_mp4_open(&it, file, L.len);
    ok(r == 1, what);
    if (r != 1) {
        return;
    }

    for (i = 0; i < f->nsamples; i++) {
        expect_frames += (nj_u4)f->nframes[i];
    }

    off = L.mdat_payload_at;
    t = 0u;
    for (i = 0; i < f->nsamples; i++) {
        nj_u4 want = sample_size(f, i);

        sprintf(what, "%s: sample %d is handed out", name, i);
        r = nj_amr_mp4_next(&it, &s, &n);
        ok(r == 1, what);
        if (r != 1) {
            return;
        }
        sprintf(what, "%s: sample %d starts at byte %u of the file", name, i,
                (unsigned)off);
        ok((nj_u4)(s - file) == off, what);
        sprintf(what, "%s: sample %d is %u bytes", name, i, (unsigned)want);
        ok(n == want, what);
        sprintf(what, "%s: sample %d carries the frames at their table offset",
                name, i);
        ok(n == want && memcmp(s, track + t, want) == 0, what);
        off += want;
        t += want;
    }
    sprintf(what, "%s: the walk ends after the last sample", name);
    ok(nj_amr_mp4_next(&it, &s, &n) == 0, what);

    sprintf(what, "%s: frames counts what the samples hold", name);
    ok(nj_amr_mp4_frames(file, L.len) == expect_frames, what);

    /*
     * The same frames with the RFC 4867 magic in front are what the decoder
     * underneath the Player already knows how to count and decode, so that is
     * the reference the container path has to agree with.
     */
    memcpy(ref, "#!AMR\n", 6u);
    memcpy(ref + 6, frames, frames_len);
    ref_len = 6u + frames_len;
    ok(nj_amr_frames(ref, ref_len) == expect_frames,
       "fixture: the reference stream holds the same frames");

    sprintf(what, "%s: decode returns the same frame count", name);
    got = nj_amr_mp4_decode(file, L.len, pcm_mp4);
    ok(got == expect_frames, what);
    sprintf(what, "%s: the reference decodes those frames too", name);
    ok(nj_amr_decode(ref, ref_len, pcm_ref) == expect_frames, what);
    sprintf(what, "%s: the PCM is the decoder's, byte for byte", name);
    ok(got == expect_frames &&
       memcmp(pcm_mp4, pcm_ref,
              (size_t)expect_frames * NJ_AMR_BLOCK * 2u) == 0, what);
}

/* A file the opener must refuse: structural damage, not a walk that stops. */
static void refused_at_open(const char *name, const fixture *f)
{
    static nj_u1 pcm[PCM_MAX];
    layout L;
    nj_amr_mp4 it;
    char what[128];

    build_file(f, &L);

    sprintf(what, "%s: open refuses it", name);
    ok(nj_amr_mp4_open(&it, file, L.len) == 0, what);
    sprintf(what, "%s: no frames are counted", name);
    ok(nj_amr_mp4_frames(file, L.len) == 0u, what);
    sprintf(what, "%s: nothing is decoded", name);
    ok(nj_amr_mp4_decode(file, L.len, pcm) == 0u, what);
}

/* A file that may open but whose walk must stop with -1 before it hands out
 * bytes that do not lie inside an 'mdat'. */
static void refused_in_walk(const char *name, const fixture *f)
{
    layout L;
    nj_amr_mp4 it;
    const nj_u1 *s;
    nj_u4 n;
    int r;
    char what[128];

    build_file(f, &L);

    r = nj_amr_mp4_open(&it, file, L.len);
    if (r == 0) {
        /* Refusing it outright is stricter than required, and fine. */
        ok(1, name);
        return;
    }
    r = nj_amr_mp4_next(&it, &s, &n);
    while (r == 1) {
        r = nj_amr_mp4_next(&it, &s, &n);
    }
    sprintf(what, "%s: the walk stops with -1", name);
    ok(r == -1, what);
}

/* A file cut short at an arbitrary byte count. */
static void refused_truncated(const char *name, const fixture *f, nj_u4 size)
{
    layout L;
    nj_amr_mp4 it;
    char what[128];

    build_file(f, &L);
    if (size >= L.len) {
        size = L.len - 1u;
    }
    sprintf(what, "%s: open refuses %u of %u bytes", name,
            (unsigned)size, (unsigned)L.len);
    ok(nj_amr_mp4_open(&it, file, size) == 0, what);
}

int main(void)
{
    static const int one_mode[1] = { 0 };
    static const int two_modes[2] = { 0, 3 };
    static const int three_modes[3] = { 0, 3, 7 };
    layout L;
    nj_amr_mp4 it;

    printf("== nano_j2me AMR-in-MP4 sample iterator ==\n");

    /* ---- 1. Constant-size stsz, one chunk, one sample ---- */

    make_frames(one_mode, 1);
    {
        fixture f = base_fixture(1);

        plays("one const-size sample", &f);
        build_file(&f, &L);
        ok(L.len < FILE_MAX, "fixture fits the test buffer");
        ok(nj_amr_mp4_open(&it, file, L.len) == 1,
           "the same bytes are accepted when opened again");
    }

    /* ---- 2. A table stsz and a two-chunk stsc mapping ---- */

    make_frames(three_modes, 3);
    {
        fixture f = two_chunk_fixture();

        plays("two chunks, three table-sized samples", &f);
    }

    /* ---- 3. The 64-bit chunk offset table ---- */

    {
        fixture f = two_chunk_fixture();

        f.co64 = 1;
        plays("two chunks through co64", &f);
    }

    /* ---- 4. damr framesPerSample > 1 ---- */

    make_frames(two_modes, 2);
    {
        fixture f = base_fixture(2);   /* one sample holding two frames */

        f.fps = 2;
        plays("a sample holding two AMR frames", &f);
    }

    /* ---- 5. What must be refused ---- */

    make_frames(one_mode, 1);

    {
        fixture f = base_fixture(1);

        f.ntraks = 2;                  /* two 'samr' tracks: which is the sound? */
        refused_at_open("two 'samr' tracks", &f);
    }
    {
        fixture f = base_fixture(1);

        f.ntraks = 2;
        f.second_mp4a = 1;
        /* Exactly one AMR track is in there, so this one plays: refusing it
         * would be refusing a file whose sound is not ambiguous. */
        plays("one amr track beside an mp4a track", &f);
    }
    {
        fixture f = base_fixture(1);

        f.no_damr = 1;
        refused_at_open("a 'samr' entry without 'damr'", &f);
    }
    {
        fixture f = base_fixture(1);

        f.fps = 0;
        refused_at_open("framesPerSample 0", &f);
    }
    {
        fixture f = base_fixture(1);

        f.stsc_spc[0] = 0;
        refused_at_open("a chunk with no samples", &f);
    }
    {
        fixture f = base_fixture(1);

        f.stsc_first[0] = 2;           /* chunk 1 must start the run */
        refused_at_open("a chunk run that does not start at chunk 1", &f);
    }
    {
        fixture f = base_fixture(1);

        f.stsz_count_delta = 1;        /* stsz says two, stsc sums to one */
        refused_at_open("stsz and stsc disagreeing on the sample count", &f);
    }
    {
        fixture f = base_fixture(1);

        f.no_mdat = 1;
        refused_at_open("no 'mdat'", &f);
    }
    {
        fixture f = base_fixture(1);

        f.entry_mp4a = 1;
        refused_at_open("a non-AMR sample entry", &f);
    }

    /* Offsets pointing outside the only 'mdat': either the opener sees the
     * table cannot be right, or the walk stops at the first sample. */
    {
        fixture f = base_fixture(1);

        f.mdat_skip = 4096u;
        refused_in_walk("a chunk offset outside the 'mdat'", &f);
    }

    /* A chunk that claims two samples while the 'mdat' holds one: the first is
     * readable, the second is not, and the walk must say -1 rather than read
     * the bytes behind the box. */
    make_frames(two_modes, 2);
    {
        fixture f = base_fixture(1);

        f.nsamples = 2;
        f.first_frame[0] = 0;
        f.first_frame[1] = 1;
        f.nframes[0] = 1;
        f.nframes[1] = 1;
        f.const_stsz = 1;              /* stsz declares both 13-byte samples */
        f.chunk_n[0] = 2;              /* one chunk claiming both */
        f.nstsc = 1;
        f.stsc_first[0] = 1;
        f.stsc_spc[0] = 2;
        f.mdat_bytes = 13;             /* ... but only one sample is in it */
        refused_in_walk("a sample that runs past the 'mdat'", &f);
    }

    /* ---- 6. Truncation, and containers with no track at all ---- */

    make_frames(one_mode, 1);
    {
        fixture f = base_fixture(1);

        build_file(&f, &L);
        refused_truncated("cut in the middle of 'ftyp'", &f, 10u);
        refused_truncated("cut inside 'moov'", &f, L.moov_at + 16u);
        refused_truncated("cut one byte short of the end", &f, L.len - 1u);
    }
    make_frames(three_modes, 3);
    {
        fixture f = two_chunk_fixture();

        build_file(&f, &L);
        refused_truncated("cut inside the stsz table", &f, L.stsz_table_at + 6u);
    }
    {
        nj_u4 n = build_ftyp_only();

        ok(nj_amr_mp4_open(&it, file, n) == 0u, "'ftyp' alone is no file");
    }
    ok(nj_amr_mp4_open(&it, NULL, 0u) == 0, "a null buffer is no file");
    build_ftyp_only();
    ok(nj_amr_mp4_open(&it, file, 4u) == 0, "four bytes are no file");

    printf("\n%d checks, %d failures\n", checks, fails);
    printf("%s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
