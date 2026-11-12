/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
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

#ifndef VI_AL_OPS_H
#define VI_AL_OPS_H

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
    S32 (*enable_chn)(VI_DEV ViDev, VI_CHN ViChn);
    S32 (*disable_chn)(VI_DEV ViDev, VI_CHN ViChn);

    S32 (*dequeue_done_buffer)(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32Index, S32 s32MilliSec);
    S32 (*queue_buffer)(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index);

    /*
     * MPI passes the full VideoFrameInfo array (one entry per slot).
     * Each VideoFrameInfo already contains the dma-buf fd
     * (stVFrame.u32Fd[0]), virtual addresses (stVFrame.ulPlaneVirAddr[]),
     * plane sizes and strides.  The plugin extracts whatever it needs and
     * converts to its internal buffer representation.
     */
    S32 (*set_external_buf_pool)(
        VI_DEV ViDev,
        VI_CHN ViChn,
        UL ulPoolId,
        U32 u32BufCnt,
        const UL *paulBufferId,
        const VideoFrameInfo *pastFrameInfo);

    /* Optional: output layout negotiation.  MPI pre-fills pstFrameInfo with
     * the neutral pixel layout, then the backend adjusts it to what its
     * hardware actually writes (K1 appends DWT sub-band tails; K3 needs no
     * adjustment).  NULL = the neutral layout is already exact. */
    S32 (*calc_frame_layout)(
        VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo);

    /* Optional: rawdump.  The backend owns the buffer: it allocates it from
     * its own ISP raw layout, targets it and recycles it.  The frame returned
     * by get_raw_dump_frame stays valid until the next trigger_raw_dump. */
    S32 (*trigger_raw_dump)(VI_DEV ViDev, VI_CHN ViChn);

    S32 (*get_raw_dump_frame)(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

    S32 (*release_raw_dump_frame)(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);

    /* Optional: offline.  Feed a raw frame from CPU memory into the offline
     * ISP pipeline.  The backend owns the staging buffer: it allocates it
     * from the device input format, copies the user data once and submits. */
    S32 (*offline_set_input_addr)(VI_DEV ViDev, VI_CHN ViChn, const U8 *pu8RawVirAddr, U32 u32RawSize);

    /* ------------------------------------------------------------------ */
    /* Optional: platform-specific extensions — set to NULL if unused.   */
    /* ------------------------------------------------------------------ */

    /*
     * Query per-slot DQBUF metadata captured at the last dequeue.
     * K3: timestamp / sequence / bytesused per plane from V4L2.
     * K1: timestamp / frameId captured in the SDK buffer callback.
     */
    S32 (*query_dqbuf_meta)(
        VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId, U64 *pu64PtsUs, U32 *pu32Sequence, U32 *pau32BytesUsed);

    /*
     * Query ISP frame metadata (exposure, WB, gains, …).
     * Returns zeroed struct on platforms that don't expose ISP stats.
     */
    S32 (*query_frame_meta)(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId, ViFrameMetaInfo *pstFrameInfo);
} ViAlOps;

/*
 * Every platform plugin shared library must export this symbol.
 * The returned pointer must remain valid for the lifetime of the plugin.
 */
typedef const ViAlOps *(*PFN_al_vi_get_ops)(void);

#ifdef __cplusplus
}
#endif

#endif /* VI_AL_OPS_H */
