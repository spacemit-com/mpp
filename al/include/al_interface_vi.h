/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: SPACEMIT
 * @Date: 2026-04-20
 * @LastEditTime: 2026-04-20
 * @Description: abstract layer interface of video input
 */

#ifndef _AL_INTERFACE_VI_H_
#define _AL_INTERFACE_VI_H_

#include "al_interface_base.h"
#include "processflow.h"
#include "vi/vi_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: initialize video input module
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_init(VOID);

/**
 * @description: deinitialize video input module
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_deinit(VOID);

/**
 * @description: set VI device attributes
 * @param {VI_DEV} ViDev
 * @param {ViDevAttrS} *pstDevAttr
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_set_dev_attr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);

/**
 * @description: get VI device attributes
 * @param {VI_DEV} ViDev
 * @param {ViDevAttrS} *pstDevAttr
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_get_dev_attr(VI_DEV ViDev, ViDevAttrS *pstDevAttr);

/**
 * @description: enable VI device
 * @param {VI_DEV} ViDev
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_enable_dev(VI_DEV ViDev);

/**
 * @description: disable VI device
 * @param {VI_DEV} ViDev
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_disable_dev(VI_DEV ViDev);

/**
 * @description: set VI channel attributes
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {ViChnAttrS} *pstChnAttr
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_set_chn_attr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr);

/**
 * @description: get VI channel attributes
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {ViChnAttrS} *pstChnAttr
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_get_chn_attr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr);

/**
 * @description: set VI channel frame rate
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {ViFrameRateCtrlS} *pstFrameRateCtrl
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_set_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: get VI channel frame rate
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {ViFrameRateCtrlS} *pstFrameRateCtrl
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_get_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: enable VI channel
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_enable_chn(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: disable VI channel
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_disable_chn(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: get one VI frame from channel
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {VideoFrameInfo} *pstVideoFrame
 * @param {S32} s32MilliSec
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_get_chn_frame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

/**
 * @description: query VI frame meta information
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {U32} u32FrameId
 * @param {ViFrameMetaInfo} *pstFrameInfo
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_query_frame_meta(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId, ViFrameMetaInfo *pstFrameInfo);

/**
 * @description: release one VI frame back to channel
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {VideoFrameInfo} *pstVideoFrame
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_release_chn_frame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);

/**
 * @description: trigger raw dump on VI channel
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_trigger_raw_dump(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: get one raw dump frame
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {VideoFrameInfo} *pstVideoFrame
 * @param {S32} s32MilliSec
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_get_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

/**
 * @description: release one raw dump frame
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {VideoFrameInfo} *pstVideoFrame
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_release_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);

/**
 * @description: set offline input raw address
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {U8} *pu8RawVirAddr
 * @param {U32} u32RawSize
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_offline_set_input_addr(VI_DEV ViDev,
							 VI_CHN ViChn,
							 UL ulPoolId,
							 UL ulBufferId,
							 const VideoFrameInfo *pstFrameInfo,
							 const IMAGE_BUFFER_S *pstImageBuffer,
							 const U8 *pu8RawVirAddr,
							 U32 u32RawSize);

/**
 * @description: attach a bind sink for VI output
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {MppNode} *pstSinkNode
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_attach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode);

/**
 * @description: detach a bind sink for VI output
 * @param {VI_DEV} ViDev
 * @param {VI_CHN} ViChn
 * @param {MppNode} *pstSinkNode
 * @return {*}: 0 on success, else error code.
 */
S32 al_vi_detach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_VI_H_*/
