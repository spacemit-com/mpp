/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_writer.h
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Container writer abstraction (fMP4 / TS) for MUX file
 *                 recording. Each writer turns Annex-B access units into a
 *                 power-loss-resilient on-disk container.
 *------------------------------------------------------------------------------
 */

#ifndef MUX_WRITER_H
#define MUX_WRITER_H

#include <stdio.h>

#include "mux/mux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef struct _MuxWriter MuxWriter;

/* Writer backend virtual table. All callbacks operate on an open FILE*. */
typedef struct _MuxWriterOps {
    const CHAR *pszName;
    /* Write container header / init segment to a freshly opened file. */
    S32 (*pfnStart)(MuxWriter *pWr);
    /* Append one Annex-B access unit. u64PtsUs in microseconds. */
    S32 (*pfnWrite)(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);
    /* Finalize current file (flush pending fragment, write trailer). */
    S32 (*pfnFinish)(MuxWriter *pWr);
    /* Release backend private state (optional). */
    VOID (*pfnDestroy)(MuxWriter *pWr);
} MuxWriterOps;

/* Concrete writer state lives behind an opaque pointer per backend. */
struct _MuxWriter {
    const MuxWriterOps *pstOps;
    FILE *pFile;
    MuxCodecType eCodecType;
    U32 u32Width;
    U32 u32Height;
    U32 u32Fps;
    U32 u32FragDurationMs; /* desired fragment duration for fMP4 */
    VOID *pPriv;           /* backend private context */
};

/**
 * @brief  Create a writer for the given file format bound to an open file.
 * @param  eFormat   MUX_FILE_FMP4 / MUX_FILE_TS
 * @param  pFile     open FILE* (writer takes ownership of writing, not closing)
 * @param  eCodec    MUX_CODEC_H264 / MUX_CODEC_H265
 * @return writer handle or NULL on failure.
 */
MuxWriter *MuxWriter_Create(MuxFileFormat eFormat, FILE *pFile, MuxCodecType eCodec, U32 u32Width, U32 u32Height,
                            U32 u32Fps, U32 u32FragDurationMs);

/** @brief  Write the container header / init segment. */
S32 MuxWriter_Start(MuxWriter *pWr);

/** @brief  Append one Annex-B access unit. */
S32 MuxWriter_Write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);

/** @brief  Finalize the current file (does not close FILE*). */
S32 MuxWriter_Finish(MuxWriter *pWr);

/** @brief  Query the number of bytes written to the underlying FILE so far.
 *  @return byte count (>= 0) on success, 0 if the position cannot be
 *          determined (the caller should treat this as a non-fatal hint). */
U64 MuxWriter_GetBytesWritten(MuxWriter *pWr);

/** @brief  Destroy writer and free private state. */
VOID MuxWriter_Destroy(MuxWriter *pWr);

/* Backend factories (implemented in mux_mp4.c / mux_ts.c). */
S32 MuxMp4_Attach(MuxWriter *pWr);
S32 MuxTs_Attach(MuxWriter *pWr);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __MUX_WRITER_H__ */
