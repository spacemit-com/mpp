/*
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: abstract layer interface of video encode.
 *               Consumes MPI public structs directly (VencChnAttr /
 *               VencChnStatus / VideoFrameInfo / StreamBufferInfo / VencCmd).
 */

#ifndef AL_INTERFACE_ENC_H
#define AL_INTERFACE_ENC_H

#include "al_interface_base.h"
#include "venc/venc_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: create a context for video encoder
 * @return {*}: the base context of video encoder, NULL on failure
 */
ALBaseContext *al_enc_create(void);

/**
 * @description: init the video encoder. Applies the full VencChnAttr:
 *               codec/pixel format/geometry/rotation/eFrameBufMode and the
 *               initial rate-control state (eRcMode, u32Bitrate, QPs, GOP,
 *               frame rate). With eRcMode == VENC_RC_MODE_FIXQP and all QP
 *               fields zero, plugin defaults are used.
 * @return {*}: MPP_OK on success, else error code
 */
S32 al_enc_init(ALBaseContext *ctx, const VencChnAttr *pstAttr);

/**
 * @description: set a dynamic encode parameter.
 * @param {VencCmd} cmd: command id
 * @param {void} *pParam: command payload (see VencCmd docs), NULL for
 *               VENC_CMD_SET_FORCE_IDR
 * @return {*}: MPP_OK on success
 */
S32 al_enc_set_para(ALBaseContext *ctx, VencCmd cmd, const void *pParam);

/**
 * @description: queue one input frame for encoding.
 *               Uses stVFrame plane fds/pointers per the channel's
 *               eFrameBufMode; u32Idx identifies the frame slot.
 *               EOS: stVFrame.u32FrameFlag & MPP_FRAME_FLAG_EOS — with planes
 *               present this is EOS-with-data; with u32PlaneNum == 0 it is an
 *               empty EOS (encoder stop only).
 * @return {*}: MPP_OK on success, MPP_CODER_NO_DATA if no input slot free
 */
S32 al_enc_send_input_frame(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);

/**
 * @description: reclaim one completed input frame slot.
 * @return {*}: frame id (u32Idx) of the reclaimed slot, or -1 if none
 */
S32 al_enc_return_input_frame(ALBaseContext *ctx);

/**
 * @description: dequeue one encoded stream packet, blocking up to
 *               u32TimeoutMs. Caller provides the output buffer:
 *               pstStream->pu8Addr with capacity in u32Size. On MPP_OK the
 *               plugin copies the payload (keyframes prefixed with SPS/PPS),
 *               overwrites u32Size with the payload length and fills u64PTS,
 *               bKeyFrame (V4L2 keyframe flag) and bEndOfStream. The CAPTURE
 *               buffer is re-queued internally.
 * @return {*}: MPP_OK / MPP_CODER_NO_DATA / MPP_CODER_EOS;
 *              MPP_OUT_OF_MEM if the payload exceeds the provided capacity
 */
S32 al_enc_request_output_stream(ALBaseContext *ctx, StreamBufferInfo *pstStream, U32 u32TimeoutMs);

/**
 * @description: query runtime status.
 * @return {*}: MPP_OK on success
 */
S32 al_enc_get_status(ALBaseContext *ctx, VencChnStatus *pstStatus);

/**
 * @description: flush pending frames and streams.
 * @return {*}: MPP_OK on success
 */
S32 al_enc_flush(ALBaseContext *ctx);

/**
 * @description: destroy the encoder context.
 */
void al_enc_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*AL_INTERFACE_ENC_H*/
