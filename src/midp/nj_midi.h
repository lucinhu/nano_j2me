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
#endif /* NJ_MIDI_H */
