/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE. */
#ifndef VDEC_BUFPOOL_H
#define VDEC_BUFPOOL_H

#include "al_interface_dec.h"
#include "sys/mpp_shm.h"

#define VDEC_MAX_EXT_BUF 64

/* Track VDEC's reference and decoder queue membership. Other consumers may
 * hold their own VB references even when VDEC has released its reference. */
typedef enum _VdecExtState {
    VDEC_EXT_RELEASED = 0, /**< No VDEC reference; the buffer may still be in use. */
    VDEC_EXT_OWNED,        /**< VDEC holds a reference; buffer is not queued. */
    VDEC_EXT_QUEUED,       /**< VDEC holds a reference; buffer is queued to the decoder. */
} VdecExtState;

typedef struct _VdecExtBuf {
    UL ulVbBuff;
    S32 s32DmaBufFd;
    VOID *pVirAddr;
    VdecExtState eState;
} VdecExtBuf;

/* Manage the active decoder output pool and old pools awaiting destruction.
 * All operations require the owning channel's outputLock. These functions do
 * not lock internally; the caller also serializes queue callbacks and handoff. */
typedef struct _VdecBufPool {
    UL ulPoolId;
    U32 u32Count;
    VdecExtBuf astBuf[VDEC_MAX_EXT_BUF];
    UL aulRetired[MPP_MAX_POOL];
    U32 u32RetiredCount;
} VdecBufPool;

typedef S32 (*VdecQueueBuffer)(ALBaseContext *, const VideoFrameInfo *);

/* Create the active VB pool and acquire its initial VDEC buffer references.
 * Buffers start as OWNED and are not submitted to the decoder here.
 * On failure, partial acquisitions remain tracked for retire_active_pool. */
S32 vdec_bufpool_create(VdecBufPool *pool, const AlDecOutputRequirement *req);

/* Requires the decoder output queue to have been stopped or reallocated.
 * Release the active pool's VDEC references and move it to the retired list.
 * Try to destroy it now; consumer-held pools remain pending even on success. */
S32 vdec_bufpool_retire_active_pool(VdecBufPool *pool);

/* Try to destroy retired pools. Pools still in use remain on the list.
 * The active pool and its buffers are not changed. */
void vdec_bufpool_try_destroy_retired_pools(VdecBufPool *pool);

/* Acquire reusable buffers from VB, then submit all OWNED buffers through queue.
 * VB grants a buffer only after all previous consumer references are gone.
 * One bounded pass: waitMs applies only to the first acquisition when all
 * buffers are RELEASED. Success may submit zero buffers. A queue failure
 * leaves that buffer OWNED, retaining its VDEC reference for recovery. */
S32 vdec_bufpool_queue_available_buffers(VdecBufPool *pool, ALBaseContext *ctx, VdecQueueBuffer queue, U32 waitMs);

/* Record a frame already dequeued by the decoder: QUEUED -> OWNED, retaining
 * the VDEC reference. Fill frame's VB pool/buffer handles from its slot index.
 * This function does not dequeue from the driver or add a consumer reference. */
S32 vdec_bufpool_on_buffer_dequeued(VdecBufPool *pool, VideoFrameInfo *frame);

/* Release one OWNED buffer's VDEC reference: OWNED -> RELEASED.
 * Consumer references remain valid. This does not requeue the buffer. */
S32 vdec_bufpool_release_vdec_ref(VdecBufPool *pool, U32 index);

/* Count buffers tracked as QUEUED, not the number of ready decoded frames. */
U32 vdec_bufpool_get_queued_count(const VdecBufPool *pool);

/* After the decoder output queue has stopped or been reallocated, record all
 * QUEUED buffers as OWNED. Retain VDEC references; do not stop the driver here. */
void vdec_bufpool_on_stream_stopped(VdecBufPool *pool);

#endif
