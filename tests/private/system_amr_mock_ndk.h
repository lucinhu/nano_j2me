/* SPDX-License-Identifier: MIT
 * Test declarations for the API21 MediaCodec service contract. This is a mock,
 * not a decoder; production compiles against the actual installed NDK headers.
 */
#ifndef SYSTEM_AMR_MOCK_NDK_H
#define SYSTEM_AMR_MOCK_NDK_H
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
typedef int32_t jint;
typedef int32_t jsize;
typedef struct JavaVM JavaVM;
typedef struct AMediaCodec AMediaCodec;
typedef struct AMediaFormat AMediaFormat;
typedef struct ANativeWindow ANativeWindow;
typedef struct AMediaCrypto AMediaCrypto;
typedef int32_t media_status_t;
typedef struct AMediaCodecBufferInfo {
    int32_t offset, size;
    int64_t presentationTimeUs;
    uint32_t flags;
} AMediaCodecBufferInfo;
enum {
    AMEDIA_OK = 0,
    AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM = 4,
    AMEDIACODEC_INFO_TRY_AGAIN_LATER = -1,
    AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED = -2,
    AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED = -3
};
void *system_amr_mock_dlopen(const char *, int);
void *system_amr_mock_dlsym(void *, const char *);
int system_amr_mock_dlclose(void *);
#define dlopen system_amr_mock_dlopen
#define dlsym system_amr_mock_dlsym
#define dlclose system_amr_mock_dlclose
#endif
