/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_al_ops.h
 * @Brief     :    VI AL plugin operations table (vtable).
 *
 * Every VI platform plugin must export one symbol:
 *
 *   const ViAlOps *al_vi_get_ops(void);
 *
 * The returned table describes what the plugin supports:
 *   - Required ops must be non-NULL; MPI checks them at load time.
 *   - Optional ops (rawdump, offline, platform extensions) may be NULL.
 *     MPI checks for NULL before calling and returns MPP_NOT_SUPPORT.
 *
 * All function signatures use only types from the public MPP include tree
 * (vi_type.h, vb_type.h, sys_type.h).  Platform-internal types such as
 * IMAGE_BUFFER_S (K1) or v4l2_buffer (K3) stay inside their plugin.
 * The plugin is responsible for converting VideoFrameInfo to whatever its
 * hardware needs.
 *------------------------------------------------------------------------------
 */

#ifndef __VI_AL_OPS_H__
#define __VI_AL_OPS_H__

#include "type.h"
#include "vi_type.h"
#include "vb_type.h"
#include "sys_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ViAlOps {
    /* ------------------------------------------------------------------ */
    /* Required — every plugin must implement all of these.                */
    /* ------------------------------------------------------------------ */

    S32 (*init)(VOID);
    S32 (*deinit)(VOID);

    S32 (*set_dev_attr)(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);
    S32 (*get_dev_attr)(VI_DEV ViDev, ViDevAttrS *pstDevAttr);
    S32 (*enable_dev)(VI_DEV ViDev);
    S32 (*disable_dev)(VI_DEV ViDev);

    S32 (*set_chn_attr)(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr);
    S32 (*get_chn_attr)(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr);
    S32 (*set_chn_framerate)(VI_DEV ViDev, VI_CHN ViChn,
                             const ViFrameRateCtrlS *pstFrameRateCtrl);
    S32 (*get_chn_framerate)(VI_DEV ViDev, VI_CHN ViChn,
                             ViFrameRateCtrlS *pstFrameRateCtrl);
    S32 (*enable_chn)(VI_DEV ViDev, VI_CHN ViChn);
    S32 (*disable_chn)(VI_DEV ViDev, VI_CHN ViChn);

    S32 (*dequeue_done_buffer)(VI_DEV ViDev, VI_CHN ViChn,
                               U32 *pu32Index, S32 s32MilliSec);
    S32 (*queue_buffer)(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index);

    S32 (*attach_bind_sink)(VI_DEV ViDev, VI_CHN ViChn,
                            const MppNode *pstSinkNode);
    S32 (*detach_bind_sink)(VI_DEV ViDev, VI_CHN ViChn,
                            const MppNode *pstSinkNode);

    /*
     * MPI passes the full VideoFrameInfo array (one entry per slot).
     * Each VideoFrameInfo already contains the dma-buf fd
     * (stVFrame.u32Fd[0]), virtual addresses (stVFrame.ulPlaneVirAddr[]),
     * plane sizes and strides.  The plugin extracts whatever it needs and
     * converts to its internal buffer representation.
     */
    S32 (*set_external_buf_pool)(VI_DEV ViDev, VI_CHN ViChn,
                                 UL ulPoolId, U32 u32BufCnt,
                                 const UL *paulBufferId,
                                 const VideoFrameInfo *pastFrameInfo);

    /* ------------------------------------------------------------------ */
    /* Optional: rawdump — set to NULL if not supported by this platform. */
    /* ------------------------------------------------------------------ */

    S32 (*trigger_raw_dump)(VI_DEV ViDev, VI_CHN ViChn);

    S32 (*get_raw_dump_frame)(VI_DEV ViDev, VI_CHN ViChn,
                              VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

    S32 (*release_raw_dump_frame)(VI_DEV ViDev, VI_CHN ViChn,
                                  const VideoFrameInfo *pstVideoFrame);

    /* Returns the chn attr that should be used to size the rawdump buffer. */
    S32 (*get_rawdump_attr)(VI_DEV ViDev, VI_CHN ViChn,
                            ViChnAttrS *pstRawAttr);

    /*
     * Import the pre-allocated rawdump buffer described by pstFrameInfo
     * into the plugin so it can be targeted by the ISP.
     * The plugin converts VideoFrameInfo to its internal buffer type.
     */
    S32 (*set_rawdump_buf)(VI_DEV ViDev, VI_CHN ViChn,
                           const VideoFrameInfo *pstFrameInfo);

    /* ------------------------------------------------------------------ */
    /* Optional: offline — set to NULL if not supported.                  */
    /* ------------------------------------------------------------------ */

    /*
     * Feed a raw frame from CPU memory into the offline ISP pipeline.
     * pstFrameInfo describes the destination VB buffer (already allocated
     * by MPI); the plugin converts it to its internal buffer type.
     * pu8RawVirAddr / u32RawSize are the source CPU data.
     */
    S32 (*offline_set_input_addr)(VI_DEV ViDev, VI_CHN ViChn,
                                  UL ulPoolId, UL ulBufferId,
                                  const VideoFrameInfo *pstFrameInfo,
                                  const U8 *pu8RawVirAddr, U32 u32RawSize);

    /* ------------------------------------------------------------------ */
    /* Optional: platform-specific extensions — set to NULL if unused.   */
    /* ------------------------------------------------------------------ */

    /*
     * Query per-slot DQBUF metadata captured at the last dequeue.
     * K3 extension: timestamp / sequence / bytesused per plane.
     */
    S32 (*query_dqbuf_meta)(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId,
                            U64 *pu64PtsUs, U32 *pu32Sequence,
                            U32 *pau32BytesUsed);

    /*
     * Query ISP frame metadata (exposure, WB, gains, …).
     * Returns zeroed struct on platforms that don't expose ISP stats.
     */
    S32 (*query_frame_meta)(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId,
                            ViFrameMetaInfo *pstFrameInfo);
} ViAlOps;

/*
 * Every platform plugin shared library must export this symbol.
 * The returned pointer must remain valid for the lifetime of the plugin.
 */
typedef const ViAlOps *(*PFN_al_vi_get_ops)(void);

#ifdef __cplusplus
}
#endif

#endif /* __VI_AL_OPS_H__ */
