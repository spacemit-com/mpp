/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_writer.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Container writer dispatcher for MUX file recording.
 *------------------------------------------------------------------------------
 */

#include "mux_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mux_common.h"

MuxWriter *MuxWriter_Create(MuxFileFormat eFormat, FILE *pFile, MuxCodecType eCodec, U32 u32Width, U32 u32Height,
                            U32 u32Fps, U32 u32FragDurationMs) {
    MuxWriter *pWr;
    S32 ret;

    if (!pFile) {
        return NULL;
    }

    pWr = (MuxWriter *)calloc(1, sizeof(MuxWriter));
    if (!pWr) {
        return NULL;
    }

    pWr->pFile = pFile;
    pWr->eCodecType = eCodec;
    pWr->u32Width = u32Width;
    pWr->u32Height = u32Height;
    pWr->u32Fps = (u32Fps > 0) ? u32Fps : 25;
    pWr->u32FragDurationMs = u32FragDurationMs;

    switch (eFormat) {
    case MUX_FILE_FMP4:
        ret = MuxMp4_Attach(pWr);
        break;
    case MUX_FILE_TS:
        ret = MuxTs_Attach(pWr);
        break;
    default:
        ret = -1;
        break;
    }

    if (ret != 0 || !pWr->pstOps) {
        free(pWr);
        return NULL;
    }

    return pWr;
}

S32 MuxWriter_Start(MuxWriter *pWr) {
    if (!pWr || !pWr->pstOps || !pWr->pstOps->pfnStart) {
        return ERR_MUX_NULL_PTR;
    }
    return pWr->pstOps->pfnStart(pWr);
}

S32 MuxWriter_Write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs) {
    if (!pWr || !pWr->pstOps || !pWr->pstOps->pfnWrite) {
        return ERR_MUX_NULL_PTR;
    }
    if (!pu8Data || u32Size == 0) {
        return ERR_MUX_INVALID_ARG;
    }
    return pWr->pstOps->pfnWrite(pWr, pu8Data, u32Size, bKeyFrame, u64PtsUs);
}

S32 MuxWriter_Finish(MuxWriter *pWr) {
    if (!pWr || !pWr->pstOps || !pWr->pstOps->pfnFinish) {
        return ERR_MUX_NULL_PTR;
    }
    return pWr->pstOps->pfnFinish(pWr);
}

U64 MuxWriter_GetBytesWritten(MuxWriter *pWr) {
    off_t off;

    if (!pWr || !pWr->pFile) {
        return 0;
    }
    fflush(pWr->pFile);
    off = ftello(pWr->pFile);
    return (off > 0) ? (U64)off : 0;
}

VOID MuxWriter_Destroy(MuxWriter *pWr) {
    if (!pWr) {
        return;
    }
    if (pWr->pstOps && pWr->pstOps->pfnDestroy) {
        pWr->pstOps->pfnDestroy(pWr);
    }
    if (pWr->pPriv) {
        free(pWr->pPriv);
        pWr->pPriv = NULL;
    }
    free(pWr);
}
