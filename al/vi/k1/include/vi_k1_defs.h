/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_defs.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI internal common definitions.
 *------------------------------------------------------------------------------
 */

#ifndef VI_K1_DEFS_H
#define VI_K1_DEFS_H

#include <string.h>
#include <stdbool.h>

#include "vi_k1.h"
#include "spm_cam_vi.h"
#include "spm_cam_ccic.h"
#include "spm_cam_isp.h"
#include "spm_comm_cam.h"
#include "spm_cam_sensors.h"
#include "spm_comm_sensors.h"
#include "cam_module_interface.h"
#include "log.h"

typedef struct spmVI_DEV_ATTR_S K1_ASR_VI_DEV_ATTR_S;
typedef struct spmVI_CHN_ATTR_S K1_ASR_VI_CHN_ATTR_S;
typedef CCIC_DEV_ATTR_S K1_ASR_CCIC_DEV_ATTR_S;
typedef CCIC_CHN_ATTR_S K1_ASR_CCIC_CHN_ATTR_S;

#define K1_VI_SUCCESS (0)
#define K1_VI_ERR_INVALID_PARAM (-1)
#define K1_VI_ERR_NOT_INIT (-2)
#define K1_VI_ERR_NOT_SUPPORT (-3)
#define K1_VI_ERR_BUSY (-4)

#define K1_VI_DEFAULT_ALIGN (16)
#define K1_VI_MAX_BUF_CNT (8)
#define K1_VI_DEFAULT_BUF_CNT (3)

typedef enum _K1_VI_BUF_STATE_E {
    K1_VI_BUF_STATE_IDLE = 0,
    K1_VI_BUF_STATE_IN_HW,
    K1_VI_BUF_STATE_READY,
    K1_VI_BUF_STATE_USER,
} K1_VI_BUF_STATE_E;

typedef struct _K1_VI_BUF_NODE_S {
    BOOL bValid;
    U32 u32Index;
    UL ulPoolId;
    UL ulBufferId;
    IMAGE_BUFFER_S stImageBuffer;
    VideoFrameInfo stFrameInfo;
    K1_VI_BUF_STATE_E enState;
} K1_VI_BUF_NODE_S;

BOOL K1_VI_IsValidDev(VI_DEV ViDev);
BOOL K1_VI_IsValidChn(VI_CHN ViChn);
BOOL K1_VI_IsValidSize(U32 u32Width, U32 u32Height);

#endif /* VI_K1_DEFS_H */
