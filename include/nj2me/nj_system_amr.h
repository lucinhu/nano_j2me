/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 nano_j2me contributors
 */
#ifndef NJ_SYSTEM_AMR_H
#define NJ_SYSTEM_AMR_H

#include "nj_avcodec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional platform AMR-NB decoder. Android uses the public native MediaCodec
 * API (API 21+), loaded at open time. Apple loads AudioToolbox and attempts an
 * AudioConverter. Windows dynamically loads Media Foundation and enumerates
 * AMR-NB transforms (format identifier available from Windows 8.1). NULL on
 * hosts without an implemented service. A non-NULL table is a candidate, not a
 * capability guarantee: open()
 * may fail when the system library/codec is absent or rejects the stream.
 * The registry must then retry the original input with its next provider.
 *
 * No system implementation or data is bundled. Buffers use request memory
 * hooks; decoder handles use the platform's own release functions. Android
 * requires libdl, but no mandatory libmediandk dependency or Java bridge.
 * It queries an existing Java VM through JNI_GetCreatedJavaVMs before opening
 * MediaCodec. In a VM-less shell executable, open() returns NULL for software
 * fallback; no Java VM is loaded/created and no thread attachment is performed
 * here. Windows adds no mandatory Media Foundation or COM DLL imports.
 */
const nj_av_audio_ops *nj_avcodec_system_amr(void);

#ifdef __cplusplus
}
#endif
#endif
