/*
 * nano_j2me - nj_midi.h
 *
 * The Standard MIDI File renderer behind Manager.createPlayer() for .mid
 * assets.  Private to src/midp: nj_lib_media.c is the only caller.
 */
#ifndef NJ_MIDI_H
#define NJ_MIDI_H

#include "nj2me/nj_types.h"

/* Mono, 16-bit signed, little-endian: the shape nj_port.pcm_write() takes. */
#define NJ_MIDI_RATE 8000u

/*
 * Render a whole Standard MIDI File to PCM.  Returns a malloc'd buffer of
 * (*frames_out * 2) bytes, or NULL when the file holds nothing to play - the
 * caller keeps its silent Player in that case.  The caller owns the buffer.
 */
nj_u1 *nj_midi_render(const void *data, nj_u4 size, nj_u4 *frames_out);

#endif /* NJ_MIDI_H */
