/*
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: abstract layer interface of video decode.
 *               Consumes MPI public structs directly (VdecChnAttr /
 *               VdecChnStatus / VideoFrameInfo / StreamBufferInfo).
 */

#ifndef AL_INTERFACE_DEC_H
#define AL_INTERFACE_DEC_H

#include "al_interface_base.h"
#include "vdec/vdec_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Buffer counts negotiated at init time (in/out).
 *        On input the caller fills its desired counts (0 = plugin default);
 *        on return the plugin overwrites them with the counts actually
 *        allocated by the V4L2 driver — the caller must supply exactly
 *        u32OutputBufNum capture buffers via al_dec_queue_output_buffer.
 */
typedef struct _AlDecBufferRequirement {
    U32 u32InputBufNum;  /**< V4L2 OUTPUT (bitstream) buffers allocated by the plugin */
    U32 u32OutputBufNum; /**< V4L2 CAPTURE buffers the caller must supply via al_dec_queue_output_buffer */
} AlDecBufferRequirement;

/**
 * @description: create a context for video decoder
 * @return {*}: the base context of video decoder, NULL on failure
 */
ALBaseContext *al_dec_create(void);

/**
 * @description: init the video decoder.
 *               The decoder always runs CAPTURE in external-dmabuf mode: after
 *               init the caller must queue pstReq->u32OutputBufNum buffers via
 *               al_dec_queue_output_buffer before frames can be produced.
 * @param {ALBaseContext} *ctx: the base context of video decoder
 * @param {VdecChnAttr} *pstAttr: channel attributes from MPI
 * @param {AlDecBufferRequirement} *pstReq: out, buffer counts the plugin decided
 * @return {*}: MPP_OK on success, else error code
 */
S32 al_dec_init(ALBaseContext *ctx, const VdecChnAttr *pstAttr, AlDecBufferRequirement *pstReq);

/**
 * @description: query runtime status (replaces the legacy al_dec_getparam
 *               MppVdecPara feedback fields).
 * @param {VdecChnStatus} *pstStatus: out, current decoder status; u32Width and
 *               u32Height are rotation-adjusted current stream dimensions
 * @return {*}: MPP_OK on success
 */
S32 al_dec_get_status(ALBaseContext *ctx, VdecChnStatus *pstStatus);

/**
 * @description: send one bitstream packet to the decoder.
 *               EOS is signaled by pstStream->bEndOfStream == MPP_TRUE or
 *               u32Size == 0; the PTS of an EOS-with-data packet marks the
 *               last displayable frame.
 * @return {*}: MPP_OK on success, MPP_CODER_NO_DATA if no input slot available
 */
S32 al_dec_decode(ALBaseContext *ctx, const StreamBufferInfo *pstStream);

/**
 * @description: queue one external dma-buf CAPTURE buffer to the decoder.
 *               Required fields: u32Idx (slot index, stable for the channel's
 *               lifetime), stVFrame.u32Fd[0] (dma-buf fd),
 *               stVFrame.ulPlaneVirAddr[0] (mapped address, may be 0).
 *               Also used to re-queue buffers after MPP_RESOLUTION_CHANGED and
 *               after the caller is done with a recycled buffer.
 * @return {*}: MPP_OK on success
 */
S32 al_dec_queue_output_buffer(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);

/**
 * @description: dequeue one decoded frame, blocking up to u32TimeoutMs.
 *               On MPP_OK the plugin fills stVFrame (per-plane fd / viraddr /
 *               stride=bytesperline / size=sizeimage / valid=bytesused, PTS,
 *               plane num, total size), u32Idx (V4L2 buffer index),
 *               eFrameType=FRAME_TYPE_VDEC and stVdecFrameInfo (width, height,
 *               pixel format, bEndOfStream). ulPoolId/ulBufferId/u32PrivateData
 *               are never touched by the plugin.
 * @param {U32} u32TimeoutMs: poll timeout in ms (0 = non-blocking)
 * @return {*}: MPP_OK frame valid;
 *              MPP_CODER_NO_DATA timeout or no frame;
 *              MPP_CODER_EOS terminal, no further frames (a final frame that
 *                carries data is returned first as MPP_OK with
 *                bEndOfStream=MPP_TRUE);
 *              MPP_RESOLUTION_CHANGED stream geometry changed: the plugin has
 *                already re-negotiated CAPTURE internally and filled the new
 *                width/height in stVdecFrameInfo.stCommFrameInfo; the caller
 *                must re-queue all external buffers (note: a new resolution
 *                larger than the original buffer size is not supported);
 *              MPP_ERROR_FRAME / MPP_CODER_NULL_DATA frame invalid but
 *                pstFrame->u32Idx/fd are valid — caller must hand the buffer
 *                back via al_dec_return_output_frame;
 *              MPP_POLL_FAILED on poll error
 */
S32 al_dec_request_output_frame(ALBaseContext *ctx, VideoFrameInfo *pstFrame, U32 u32TimeoutMs);

/**
 * @description: return a dequeued frame (identified by pstFrame->u32Idx) back
 *               to the decoder so its buffer can be re-queued for decoding.
 * @return {*}: MPP_OK on success
 */
S32 al_dec_return_output_frame(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);

/**
 * @description: flush pending input and decoded frames.
 * @return {*}: MPP_OK on success
 */
S32 al_dec_flush(ALBaseContext *ctx);

/**
 * @description: reset the decoder to post-init state.
 * @return {*}: MPP_OK on success
 */
S32 al_dec_reset(ALBaseContext *ctx);

/**
 * @description: destroy the decoder context.
 */
void al_dec_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*AL_INTERFACE_DEC_H*/
