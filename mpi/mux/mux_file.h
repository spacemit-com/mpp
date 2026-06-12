/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_file.h
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    File recording backend with segmentation for MUX.
 *
 * Wraps a MuxWriter (fMP4 / TS) and adds:
 *   - file-name templating (strftime + sequence number)
 *   - size / duration based segmentation, always split on a key frame so each
 *     file starts decodable
 *   - periodic fsync so recordings survive power loss
 *------------------------------------------------------------------------------
 */

#ifndef MUX_FILE_H
#define MUX_FILE_H

#include "mux_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef struct _MuxFile MuxFile;

/**
 * @brief  Create a file recorder.
 * @param  pstSeg   segmentation / format configuration
 * @param  pstStream stream attributes (codec, resolution, fps)
 * @return recorder handle or NULL on failure.
 */
MuxFile *MuxFile_Create(const MuxSegmentAttr *pstSeg, const MuxStreamAttr *pstStream);

/**
 * @brief  Feed one Annex-B access unit to the recorder.
 *         Segmentation thresholds are evaluated on key frames.
 */
S32 MuxFile_Write(MuxFile *pFile, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);

/** @brief  Force a split to a new file at the next key frame. */
S32 MuxFile_RequestSplit(MuxFile *pFile);

/** @brief  Fill recording statistics (file count, current file bytes/path). */
VOID MuxFile_GetStat(const MuxFile *pFile, U32 *pu32FileCount, U64 *pu64CurBytes, CHAR *pszCurFile,
    U32 u32CurFileCap);

/** @brief  Finalize current file and release the recorder. */
VOID MuxFile_Destroy(MuxFile *pFile);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* MUX_FILE_H */
