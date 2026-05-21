/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#ifndef __AL_VI_K3_CTX_H__
#define __AL_VI_K3_CTX_H__

#include "vi_k3_defs.h"
#include <linux/videodev2.h>

/*
 * K3 AL channel context — pure V4L2 state.
 *
 * Buffer pool, dma-buf fds, and plane sizes are all owned by MPI; AL only
 * caches what it needs to run V4L2 ioctls (REQBUFS / QBUF / DQBUF).
 *
 * No VB / SYS / pthread state lives here — those moved up to mpi/vi/vi.c.
 */

typedef struct _K3_VI_CHN_CTX_S {
    BOOL bCreated;
    BOOL bEnabled;
    BOOL bStreaming;
    VI_DEV ViDev;
    VI_CHN ViChn;
    ViChnAttrS stChnAttr;

    /* V4L2 device fd (opened in K3_V4L2_Open) */
    S32 s32Fd;

    /* Negotiated buffer geometry — filled by K3_V4L2_Config */
    U32 u32BufCnt;
    U32 u32PlaneCnt;

    /* Buffer descriptors — populated by al_vi_set_external_buf_pool from MPI.
     * Each slot owns one dmabuf fd (single fd shared across planes for
     * mplane formats). au32PlaneSize[i][p] is the V4L2 plane.length value. */
    S32 as32DmaBufFd[K3_VI_MAX_BUF_CNT];
    U32 au32PlaneSize[K3_VI_MAX_BUF_CNT][VIDEO_MAX_PLANES];

    /* Latest DQBUF metadata (per slot) — captured by al_vi_dequeue_done_buffer.
     * MPI fetches it via al_vi_query_frame_meta when assembling VideoFrameInfo. */
    struct {
        U64 u64TimestampUs;
        U32 u32Sequence;
        U32 au32BytesUsed[VIDEO_MAX_PLANES];
    } astSlotMeta[K3_VI_MAX_BUF_CNT];
} K3_VI_CHN_CTX_S;

typedef struct _K3_VI_DEV_CTX_S {
    BOOL bCreated;
    BOOL bEnabled;
    ViDevAttrS stAttr;
} K3_VI_DEV_CTX_S;

typedef struct _K3_VI_CTX_S {
    BOOL bInit;
    K3_VI_DEV_CTX_S astDevCtx[K3_VI_MAX_DEV_NUM];
    K3_VI_CHN_CTX_S astChnCtx[K3_VI_MAX_DEV_NUM][K3_VI_MAX_CHN_NUM];
} K3_VI_CTX_S;

extern K3_VI_CTX_S g_stK3ViCtx;

/* V4L2 lifecycle (vi_k3_v4l2.c) */
S32 K3_V4L2_Open(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx);
S32 K3_V4L2_Config(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx);
S32 K3_V4L2_Start(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx);
S32 K3_V4L2_Stop(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx);
S32 K3_V4L2_Close(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx);

/* DMABUF helpers (vi_k3_v4l2.c) */
S32 K3_V4L2_QBuf_DmaBuf(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx, U32 u32BufIdx);
S32 K3_V4L2_DQBuf_Wait(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx,
                       S32 s32MilliSec, U32 *pu32BufIdx);

#endif /* __AL_VI_K3_CTX_H__ */
