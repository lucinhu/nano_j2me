/* Bounded, allocation-free Mascot Capsule MTRA animation storage and evaluation. */
#ifndef NJ_MASCOT_ANIM_H
#define NJ_MASCOT_ANIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NJ_MASCOT_ANIM_OK = 0,
    NJ_MASCOT_ANIM_INVALID = -1,
    NJ_MASCOT_ANIM_SPACE = -2,
    NJ_MASCOT_ANIM_RANGE = -3
};

/* Validate MTRA versions 2..5 and report the exact blob size. The immutable
 * blob retains the original packed bytes and indexes actions/bones; it never
 * expands frames. Trailing vendor bytes are retained. No alignment required.
 * All functions allocate nothing and use no writable global state. Empty
 * action/bone tables and zero declared frame lengths are accepted; sampled
 * curves must contain at least one key with nondecreasing unsigned times.
 */
int nj_mascot_anim_query(const void *mtra, size_t mtra_size, size_t *blob_size);

/* Source and destination must not overlap. On invalid input or insufficient
 * capacity, destination is unchanged. The source may be released after fill.
 */
int nj_mascot_anim_fill(const void *mtra, size_t mtra_size,
                        void *blob, size_t blob_size);

/* Metadata returns -1 for an invalid blob. Action/bone indexes are zero-based.
 * Pass the filled blob's size to every accessor. Keep the blob unmodified.
 */
int nj_mascot_anim_action_count(const void *blob, size_t blob_size);
int nj_mascot_anim_bone_count(const void *blob, size_t blob_size);
int nj_mascot_anim_frames(const void *blob, size_t blob_size,
                          int action, int32_t *frames_16_16);
int nj_mascot_anim_pattern(const void *blob, size_t blob_size,
                           int action, int32_t frame_16_16,
                           int32_t default_pattern, int32_t *pattern);

/* Evaluate one bone in row-major 3x4 form: rotation/scale use 12 fractional
 * bits, translation uses integers. The optional base is composed on the left
 * (base * animation); NULL means identity. base and out may alias. Frame is
 * signed 16.16; arithmetic matches Java int wrap and the reference's frame>>4
 * interpolation. Frames before a curve's first key use its first vector
 * (the Java implementation otherwise reads shared scratch from prior calls).
 * Output is unchanged on failure. A successful fill is required; the blob
 * must remain immutable for all metadata/evaluation calls.
 */
int nj_mascot_anim_evaluate(const void *blob, size_t blob_size,
                            int action, int bone, int32_t frame_16_16,
                            const int32_t base[12], int32_t out[12]);

#ifdef __cplusplus
}
#endif
#endif
