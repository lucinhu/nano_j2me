/* SPDX-License-Identifier: MIT
 * Minimal public AudioToolbox ABI used by the optional dynamic bridge.
 * These are interface layouts and identifiers, not codec code. Field order
 * and signatures are documented by Apple under the symbols linked below:
 * https://developer.apple.com/documentation/coreaudiotypes/audiostreambasicdescription
 * https://developer.apple.com/documentation/coreaudiotypes/audiobufferlist
 * https://developer.apple.com/documentation/coreaudiotypes/audiostreampacketdescription
 * https://developer.apple.com/documentation/audiotoolbox/audioconvertercomplexinputdataproc
 * Local names avoid defining or replacing SDK types. No Apple SDK required.
 */
#ifndef NJ_SYSTEM_AMR_APPLE_ABI_H
#define NJ_SYSTEM_AMR_APPLE_ABI_H
#include <stdint.h>
typedef struct nj_apple_asbd {
    double rate;
    uint32_t format, flags, bytes_packet, frames_packet, bytes_frame;
    uint32_t channels, bits, reserved;
} nj_apple_asbd;
typedef struct nj_apple_buffer {
    uint32_t channels, size;
    void *data;
} nj_apple_buffer;
typedef struct nj_apple_buffers {
    uint32_t count;
    nj_apple_buffer buffers[1];
} nj_apple_buffers;
typedef struct nj_apple_packet {
    int64_t offset;
    uint32_t frames, size;
} nj_apple_packet;
typedef void *nj_apple_converter;
typedef int32_t (*nj_apple_input)(nj_apple_converter, uint32_t *,
                                nj_apple_buffers *, nj_apple_packet **, void *);
/* FourCC values identify AMR-NB and linear PCM. Signed, packed, little-endian
 * output uses bit 2 (signed integer) and bit 3 (packed), with bit 1 clear. */
#define NJ_APPLE_AMR  UINT32_C(0x73616d72)
#define NJ_APPLE_PCM  UINT32_C(0x6c70636d)
#define NJ_APPLE_S16_PACKED UINT32_C(12)
#endif
