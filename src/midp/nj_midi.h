/*
 * nano_j2me - nj_midi.h
 *
 * The Standard MIDI File renderer behind Manager.createPlayer() for .mid
 * assets.  Private to src/midp: nj_lib_media.c is the only caller.
 */
#ifndef NJ_MIDI_H
#define NJ_MIDI_H

#include "nj2me/nj_types.h"

/*
 * Mono, 16-bit signed, little-endian: the shape nj_port.pcm_write() takes.
 *
 * 22.05 kHz rather than the 8 kHz this started at: a square wave has strong
 * harmonics, and at 8 kHz everything above 4 kHz folded back into the audible
 * band as a metallic buzz - a MIDlet asking for a tone got a telephone.  The
 * cost is 2.75x the PCM buffer (a sixty second song is 2.6 MB).
 */
#define NJ_MIDI_RATE 22050u

/*
 * The whole-file renderers stop here.  A stream does not: that is the point of
 * the streaming API, because a quarter of the corpus is longer than this and
 * the buffer for one of those does not fit any heap we run on.
 */
#define NJ_MIDI_MAX_SECONDS 60u
#define NJ_MIDI_MAX_FRAMES (NJ_MIDI_RATE * NJ_MIDI_MAX_SECONDS)

/*
 * Allocation hook.  Allocation in the runtime goes through nj_port callbacks
 * and never through libc (include/nj2me/nj_libc.h), so the renderer borrows
 * the caller's allocator - the same shape the image codecs take.
 */
typedef struct nj_midi_mem {
    void  *ud;
    void *(*alloc)(void *ud, nj_size n);
    void  (*free)(void *ud, void *p);
} nj_midi_mem;

/*
 * Render a whole Standard MIDI File to PCM.  Returns a buffer of
 * (*frames_out * 2) bytes from `mem`, or NULL when the file holds nothing to
 * play - the caller keeps its silent Player in that case, and the caller owns
 * the buffer and releases it with mem->free.
 */
nj_u1 *nj_midi_render(const void *data, nj_u4 size, nj_u4 *frames_out,
                      const nj_midi_mem *mem);

/*
 * The stereo form: a file that never sends CC10 renders mono (one channel),
 * and a file that pans renders two interleaved channels.  nj_midi_render()
 * is the mono-only wrapper, so a caller that does not care stays simple.
 */
nj_u1 *nj_midi_render_ex(const void *data, nj_u4 size, nj_u4 *frames_out,
                         nj_u4 *chans_out, const nj_midi_mem *mem);

/*
 * The same renderer as a resumable stream.
 *
 * A whole-file render has to hold every sample of the song at once, which a
 * three minute MIDI makes eight megabytes of and the corpus holds files an
 * hour long - and when the heap cannot take it the Player falls back to
 * silence.  A stream keeps only the parsed events and renders as the port
 * pulls, so the memory is a few kilobytes whatever the song's length.
 *
 * nj_midi_stream_open() returns NULL when there is nothing to play.
 * nj_midi_stream_frames() is the length the song needs, tail included (the
 * caller can use it for a duration).  nj_midi_stream_pull() writes up to
 * `want` frames and returns how many it wrote: zero means the song is over.
 * The output is interleaved when nj_midi_stream_channels() says 2.
 */
struct nj_midi_stream;

struct nj_midi_stream *nj_midi_stream_open(const void *data, nj_u4 size,
                                           const nj_midi_mem *mem);
nj_u4 nj_midi_stream_channels(const struct nj_midi_stream *s);
nj_u4 nj_midi_stream_frames(const struct nj_midi_stream *s);
nj_u4 nj_midi_stream_pull(struct nj_midi_stream *s, nj_u1 *out, nj_u4 want);
/*
 * Jump to a frame.  Returns the frame actually reached, which is the end of
 * the song when the target is past it.
 */
nj_u4 nj_midi_stream_seek(struct nj_midi_stream *s, nj_u4 frame);
void nj_midi_stream_close(struct nj_midi_stream *s);
#endif /* NJ_MIDI_H */
