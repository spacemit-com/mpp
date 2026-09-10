/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE. */
#include "vdec_bufpool.h"

#include <stdint.h>
#include <string.h>

#include "sys/vb_api.h"

S32 vdec_bufpool_create(VdecBufPool *pool, const AlDecOutputRequirement *req) {
    U64 size = 0;
    if (pool->ulPoolId || !req->u32BufCnt || req->u32BufCnt > VDEC_MAX_EXT_BUF || !req->u32PlaneNum ||
        req->u32PlaneNum > FRAME_MAX_PLANE)
        return ERR_VDEC_NOT_SUPPORT;
    for (U32 i = 0; i < req->u32PlaneNum; ++i) {
        if (!req->au32Size[i])
            return ERR_VDEC_NOT_SUPPORT;
        size += req->au32Size[i];
    }
    if (size > UINT32_MAX)
        return ERR_VDEC_NOMEM;

    VbPoolCfg cfg = {
        .u32BufSize = (U32)size,
        .u32BufCnt = req->u32BufCnt,
        .eModId = MPP_ID_VDEC,
        .eRemapMode = VBUF_REMAP_MODE_NOCACHE,
    };
    pool->ulPoolId = VB_CreatePool(&cfg);
    if (!pool->ulPoolId)
        return ERR_VDEC_NOMEM;
    pool->u32Count = req->u32BufCnt;
    memset(pool->astBuf, 0, sizeof(pool->astBuf));
    for (U32 i = 0; i < pool->u32Count; ++i) {
        VdecExtBuf *buf = &pool->astBuf[i];
        buf->ulVbBuff = VB_ModGetBuffer(pool->ulPoolId, MPP_ID_VDEC, 0);
        if (!buf->ulVbBuff)
            return ERR_VDEC_NOMEM;
        buf->eState = VDEC_EXT_OWNED;
        if (VB_GetDmaBufFd(buf->ulVbBuff, &buf->s32DmaBufFd) != 0 || buf->s32DmaBufFd < 0)
            return ERR_VDEC_NOMEM;
        if (VB_GetVirAddr(buf->ulVbBuff, &buf->pVirAddr) != 0)
            return ERR_VDEC_NOMEM;
    }
    return ERR_VDEC_OK;
}

void vdec_bufpool_try_destroy_retired_pools(VdecBufPool *pool) {
    U32 i = 0;
    while (i < pool->u32RetiredCount) {
        if (VB_DestroyPool(pool->aulRetired[i]) == 0)
            pool->aulRetired[i] = pool->aulRetired[--pool->u32RetiredCount];
        else
            ++i;
    }
}

S32 vdec_bufpool_release_vdec_ref(VdecBufPool *pool, U32 index) {
    if (index >= pool->u32Count || pool->astBuf[index].eState != VDEC_EXT_OWNED)
        return ERR_VDEC_BUSY;
    VdecExtBuf *buf = &pool->astBuf[index];
    S32 ret = VB_ModReleaseBuffer(buf->ulVbBuff, MPP_ID_VDEC);
    if (ret == 0)
        buf->eState = VDEC_EXT_RELEASED;
    return ret;
}

void vdec_bufpool_on_stream_stopped(VdecBufPool *pool) {
    for (U32 i = 0; i < pool->u32Count; ++i) {
        if (pool->astBuf[i].eState == VDEC_EXT_QUEUED)
            pool->astBuf[i].eState = VDEC_EXT_OWNED;
    }
}

S32 vdec_bufpool_retire_active_pool(VdecBufPool *pool) {
    if (!pool->ulPoolId)
        return ERR_VDEC_OK;
    vdec_bufpool_try_destroy_retired_pools(pool);
    if (pool->u32RetiredCount == MPP_MAX_POOL)
        return ERR_VDEC_NOMEM;
    vdec_bufpool_on_stream_stopped(pool);
    for (U32 i = 0; i < pool->u32Count; ++i) {
        if (pool->astBuf[i].eState == VDEC_EXT_OWNED) {
            S32 ret = vdec_bufpool_release_vdec_ref(pool, i);
            if (ret != 0)
                return ret;
        }
    }
    pool->aulRetired[pool->u32RetiredCount++] = pool->ulPoolId;
    pool->ulPoolId = 0;
    pool->u32Count = 0;
    memset(pool->astBuf, 0, sizeof(pool->astBuf));
    vdec_bufpool_try_destroy_retired_pools(pool);
    return ERR_VDEC_OK;
}

/* Acquire one reusable buffer's VDEC reference: RELEASED -> OWNED. */
static S32 vdec_bufpool_acquire_available_buffer(VdecBufPool *pool, U32 waitMs) {
    UL handle = VB_ModGetBuffer(pool->ulPoolId, MPP_ID_VDEC, waitMs);
    if (!handle)
        return ERR_VDEC_NO_FRAME;
    for (U32 i = 0; i < pool->u32Count; ++i) {
        VdecExtBuf *buf = &pool->astBuf[i];
        if (buf->ulVbBuff == handle && buf->eState == VDEC_EXT_RELEASED) {
            buf->eState = VDEC_EXT_OWNED;
            return ERR_VDEC_OK;
        }
    }
    VB_ModReleaseBuffer(handle, MPP_ID_VDEC);
    return ERR_VDEC_BUSY;
}

S32 vdec_bufpool_queue_available_buffers(VdecBufPool *pool, ALBaseContext *ctx, VdecQueueBuffer queue, U32 waitMs) {
    /* Acquire no more than the pool size, even if a backend misbehaves. Only
     * wait when no producer-owned buffer can already be submitted. */
    for (U32 i = 0; i < pool->u32Count; ++i) {
        if (pool->astBuf[i].eState != VDEC_EXT_RELEASED) {
            waitMs = 0;
            break;
        }
    }
    for (U32 i = 0; i < pool->u32Count; ++i) {
        S32 ret = vdec_bufpool_acquire_available_buffer(pool, i == 0 ? waitMs : 0);
        if (ret == ERR_VDEC_NO_FRAME)
            break;
        if (ret != 0)
            return ret;
    }
    for (U32 i = 0; i < pool->u32Count; ++i) {
        VdecExtBuf *buf = &pool->astBuf[i];
        if (buf->eState != VDEC_EXT_OWNED)
            continue;
        VideoFrameInfo frame = {0};
        frame.u32Idx = i;
        frame.stVFrame.u32Fd[0] = (UL)buf->s32DmaBufFd;
        frame.stVFrame.ulPlaneVirAddr[0] = (UL)buf->pVirAddr;
        S32 ret = queue(ctx, &frame);
        if (ret != 0)
            return ret;
        buf->eState = VDEC_EXT_QUEUED;
    }
    return ERR_VDEC_OK;
}

S32 vdec_bufpool_on_buffer_dequeued(VdecBufPool *pool, VideoFrameInfo *frame) {
    if (frame->u32Idx >= pool->u32Count || pool->astBuf[frame->u32Idx].eState != VDEC_EXT_QUEUED)
        return ERR_VDEC_BUSY;
    VdecExtBuf *buf = &pool->astBuf[frame->u32Idx];
    buf->eState = VDEC_EXT_OWNED;
    frame->ulPoolId = pool->ulPoolId;
    frame->ulBufferId = buf->ulVbBuff;
    return ERR_VDEC_OK;
}

U32 vdec_bufpool_get_queued_count(const VdecBufPool *pool) {
    U32 count = 0;
    for (U32 i = 0; i < pool->u32Count; ++i)
        count += pool->astBuf[i].eState == VDEC_EXT_QUEUED;
    return count;
}
