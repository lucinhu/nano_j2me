#!/usr/bin/env python3
"""Generate tests/m4v_audio_clip.h: a synthetic dual-track 3GP for the
nj_mp4 audio-track selector tests.

The fixture is a real ISO base media file (ftyp + mdat + moov) with two tracks:

  * a 64x48 'mp4v' video track with two samples of four bytes each, so that
    selecting the video track from the same file can be tested next to the
    audio one;
  * an AMR-NB 'samr' sound track with five samples in two chunks (3 + 2) and
    a 20 ms stts delta (8000 Hz timescale), so the shared stsz/stsc/stco/stts
    iterator is exercised across a chunk-run boundary.

DERIVED is a video-only copy (same bytes minus the sound trak), used to pin
NJ_MP4_ERR_NO_AUDIO.  The header also records the byte offsets of two fields
the tests patch in place to build the other error cases: the 'samr' fourcc
(rewrite to 'mp4a' -> NJ_MP4_ERR_AUDIO_CODEC) and the sound track's stsz
sample-count field (rewrite 5 -> 4 so stts and stsz disagree ->
NJ_MP4_ERR_FORMAT).

Run from the repository root:  python3 tools/gen_m4v_audio_clip.py
"""

import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                   "tests", "m4v_audio_clip.h")

FOURCC = lambda s: s.encode("ascii")


def box(typ, payload):
    return struct.pack(">I", 8 + len(payload)) + FOURCC(typ) + payload


def full(typ, version, flags, payload):
    return box(typ, struct.pack(">B", version) + flags.to_bytes(3, "big") + payload)


def u32(v):
    return struct.pack(">I", v)


def u16(v):
    return struct.pack(">H", v)


def table(typ, entries):
    body = b"".join(entries)
    return full(typ, 0, 0, u32(len(entries)) + body)


def stts(entries):
    return table("stts", [u32(n) + u32(delta) for n, delta in entries])


def stsc(entries):
    return table("stsc", [u32(first) + u32(spc) + u32(sdi)
                          for first, spc, sdi in entries])


def stsz(sizes):
    return full("stsz", 0, 0, u32(0) + u32(len(sizes)) +
                b"".join(u32(n) for n in sizes))


def stco(offsets):
    return table("stco", [u32(o) for o in offsets])


def visual_entry(width, height):
    # 8-byte sample entry header + 70 bytes VisualSampleEntry.
    body = (b"\x00" * 6 + u16(1) +            # reserved, data_reference_index
            b"\x00" * 16 +                    # pre_defined/reserved/pre_defined
            u16(width) + u16(height) +        # geometry     (offsets 24/26)
            u32(0x00480000) + u32(0x00480000) +  # 72 dpi
            u32(0) +                          # reserved
            u16(1) +                          # frame count
            b"\x00" * 32 +                    # compressor name
            u16(24) +                         # depth
            u16(0xFFFF))                      # pre-defined  (body is 78 bytes)
    return box("mp4v", body)


def audio_entry():
    # 8-byte sample entry header + 28 bytes AudioSampleEntry + one 'damr' box.
    body = (b"\x00" * 6 + u16(1) +            # reserved, data_reference_index
            b"\x00" * 8 +                     # version/revision/vendor
            u16(1) +                          # channelcount (AMR-NB is mono)
            u16(16) +                         # samplesize
            u16(0) +                          # pre_defined
            u16(0) +                          # reserved
            u32(8000 << 16))               # 16.16 sample rate

    # damr: vendor(4) + decoder_version(1) + mode_set(2).  The runtime does not
    # need it (AMR frames are self-describing) but real 3GPs carry it.
    damr = box("damr", FOURCC("nano") + b"\x00" + u16(0x0081))
    return box("samr", body + damr)


def hdlr(kind, name):
    return full("hdlr", 0, 0, u32(0) + FOURCC(kind) + b"\x00" * 12 +
                name.encode("ascii") + b"\x00")


def tkhd(duration, width, height, track_id):
    body = (u32(0) + u32(0) + u32(track_id) + u32(0) + u32(duration) +
            b"\x00" * 8 +                     # reserved[2]
            u16(0) + u16(0) + u16(0) + u16(0) +   # layer, alt group, volume
            struct.pack(">9i", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000) +
            u32(width << 16) + u32(height << 16))
    return full("tkhd", 0, 3, body)


def mdhd(timescale, duration):
    return full("mdhd", 0, 0, u32(0) + u32(0) + u32(timescale) + u32(duration) +
                u16(0x55C4) + u16(0))


def mvhd(timescale, duration):
    body = (u32(0) + u32(0) + u32(timescale) + u32(duration) +
            u32(0x00010000) + u16(0x0100) + u16(0) + b"\x00" * 8 +
            struct.pack(">9i", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000) +
            b"\x00" * 24 + u32(3))
    return full("mvhd", 0, 0, body)


def build(with_audio):
    ftyp = box("ftyp", FOURCC("isom") + u32(0x200) +
               FOURCC("isom") + FOURCC("3gp4"))

    video_samples = [b"VID0", b"VID1"]
    audio_sizes = [13, 12, 15, 14, 16]
    audio_samples = []
    for i, n in enumerate(audio_sizes):
        audio_samples.append(bytes([0x3C]) + bytes([i]) * (n - 1))

    mdat_payload = b"".join(video_samples) + b"".join(audio_samples)
    mdat_at = len(ftyp) + 8                 # first payload byte
    video_at = mdat_at
    audio_at = mdat_at + len(video_samples)
    chunk1_at = audio_at
    chunk2_at = audio_at + sum(audio_sizes[:3])

    video_stbl = box("stbl",
                     box("stsd", u32(0) + u32(1) + visual_entry(64, 48)) +
                     stts([(2, 500)]) +
                     stsc([(1, 2, 1)]) +
                     stsz([4, 4]) +
                     stco([video_at]))
    video_trak = box("trak",
                     tkhd(1000, 64, 48, 1) +
                     box("mdia", mdhd(1000, 1000) +
                         hdlr("vide", "VideoHandler") +
                         box("minf", video_stbl)))

    traks = [video_trak]
    audio_entry_pos = 0
    audio_stsz_count_pos = 0
    if with_audio:
        audio_stbl_body = (box("stsd", u32(0) + u32(1) + audio_entry()) +
                           stts([(5, 160)]) +
                           stsc([(1, 3, 1), (2, 2, 1)]) +
                           stsz(audio_sizes) +
                           stco([chunk1_at, chunk2_at]))
        audio_stbl = box("stbl", audio_stbl_body)
        audio_trak = box("trak",
                         tkhd(800, 0, 0, 2) +
                         box("mdia", mdhd(8000, 800) +
                             hdlr("soun", "SoundHandler") +
                             box("minf", audio_stbl)))
        traks.append(audio_trak)

    moov = box("moov", mvhd(1000, 1000) + b"".join(traks))
    data = ftyp + box("mdat", mdat_payload) + moov

    # Offsets the tests patch (only meaningful for the dual-track file).
    audio_entry_pos = data.find(FOURCC("samr"))
    audio_stsz = data.find(FOURCC("stsz"), data.find(FOURCC("soun")))
    audio_stsz_count_pos = audio_stsz + 4 + 8      # box hdr + version/flags
    return data, audio_entry_pos, audio_stsz_count_pos


def carray(name, data):
    lines = ["static const nj_u1 %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 12):
        chunk = ", ".join("0x%02x" % b for b in data[i:i + 12])
        lines.append("    " + chunk + ("," if i + 12 < len(data) else ""))
    lines.append("};")
    return "\n".join(lines)


def main():
    dual, samr_at, stsz_count_at = build(True)
    video_only, _, _ = build(False)

    out = []
    out.append("/*")
    out.append(" * Synthetic dual-track 3GP for the nj_mp4 audio-track tests.")
    out.append(" *")
    out.append(" * Generated by tools/gen_m4v_audio_clip.py - do not edit by hand.")
    out.append(" * The file is a real ISO base media container:")
    out.append(" *")
    out.append(" *   64x48 mp4v video track, 2 samples of 4 bytes, one chunk")
    out.append(" *   AMR-NB 'samr' sound track, 5 samples in 2 chunks (3 + 2),")
    out.append(" *   timescale 8000, one stts run of 160 (20 ms) per sample")
    out.append(" *")
    out.append(" * The video samples come first in 'mdat', so both tracks can be")
    out.append(" * selected from the same buffer and the two iterators cannot share")
    out.append(" * state (each nj_mp4 is its own cursor).")
    out.append(" *")
    out.append(" * Expected audio track: codec 0x73616d72, timescale 8000,")
    out.append(" * duration 800, 5 samples, 1 channel, 8000 Hz, first sample at")
    out.append(" * offset %d size 13 dts 0 duration 160," % (dual.find(b"VID"),))
    out.append(" * samples [13,12,15,14,16] in chunks of 3 and 2.")
    out.append(" *")
    out.append(" * The three REAL corpus movies, for reference (verified with")
    out.append(" * tools/mp4_audio_stats.c against ffprobe; see docs/CODECS.md):")
    out.append(" *   1942.3gp : 8000 Hz, timescale 8000, 109 samples of 100 ms")
    out.append(" *              (each holds five 32-byte AMR frames), first at 3703")
    out.append(" *   doom.3gp : 8000 Hz, timescale 8000, 624 samples of 20 ms")
    out.append(" *              (one 32-byte AMR frame each), first at 28")
    out.append(" *   movie.3gp: 8000 Hz, timescale 8000, 172 samples of 100 ms")
    out.append(" *              (each holds five 27-byte AMR frames), first at 40")
    out.append(" * Their ChannelCount field is 2 in two of the files even though")
    out.append(" * AMR-NB is mono by definition; the iterator reports what the")
    out.append(" * container says, and AMR callers must treat it as mono.")
    out.append(" */")
    out.append("#ifndef NJ_TESTS_M4V_AUDIO_CLIP_H")
    out.append("#define NJ_TESTS_M4V_AUDIO_CLIP_H")
    out.append("")
    out.append("#define M4V_AV_VIDEO_WIDTH  64")
    out.append("#define M4V_AV_VIDEO_HEIGHT 48")
    out.append("#define M4V_AV_VIDEO_FRAMES 2")
    out.append("#define M4V_AV_AUDIO_FRAMES 5")
    out.append("")
    out.append("/* Byte offsets of fields the tests rewrite to build error cases. */")
    out.append("#define M4V_AV_SAMR_AT       %d" % samr_at)
    out.append("#define M4V_AV_STSZ_COUNT_AT %d" % stsz_count_at)
    out.append("")
    out.append(carray("m4v_av_3gp", dual))
    out.append("")
    out.append("/* The same container without the sound trak (same video track). */")
    out.append(carray("m4v_av_video_only_3gp", video_only))
    out.append("")
    out.append("#endif /* NJ_TESTS_M4V_AUDIO_CLIP_H */")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("wrote", OUT, len(dual), "bytes (dual),", len(video_only),
          "bytes (video only)")


if __name__ == "__main__":
    main()
