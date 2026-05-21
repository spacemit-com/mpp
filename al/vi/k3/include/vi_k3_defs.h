/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#ifndef __AL_VI_K3_DEFS_H__
#define __AL_VI_K3_DEFS_H__

#include "type.h"
#include "vi_type.h"
#include "log.h"
#include <stdio.h>

#define K3_VI_MAX_BUF_CNT 32
#define K3_VI_MAX_PLANE_CNT 3
#define K3_VI_MAX_DEV_NUM VI_MAX_DEV_NUM
#define K3_VI_MAX_CHN_NUM VI_MAX_CHN_NUM

#define K3_VI_SUCCESS           0
#define K3_VI_ERR_INVALID_PARAM (-1)   /* NULL pointer / bad arg / out-of-range  */
#define K3_VI_ERR_NOT_INIT      (-2)   /* module / ctx not initialized           */
#define K3_VI_ERR_NOT_SUPPORT   (-3)   /* feature not implemented by K3          */
#define K3_VI_ERR_BUSY          (-4)   /* resource busy / would-block / no data  */
#define K3_VI_ERR_OPEN_FAIL     (-5)   /* open(/dev/videoX) failed               */
#define K3_VI_ERR_IOCTL_FAIL    (-6)   /* generic / uncategorized ioctl failure  */
#define K3_VI_ERR_NO_MEM        (-7)   /* allocation / REQBUFS=0                 */
#define K3_VI_ERR_TIMEOUT       (-8)   /* select() timed out, no frame           */
#define K3_VI_ERR_BAD_STATE     (-9)   /* fd<0, not streaming, bad ctx state     */
#define K3_VI_ERR_BAD_FORMAT    (-10)  /* S_FMT / capability mismatch            */

/* --- fine-grained V4L2 ioctl failures (-20..-39) --- */
#define K3_VI_ERR_QUERYCAP      (-20)  /* VIDIOC_QUERYCAP failed                 */
#define K3_VI_ERR_S_FMT         (-21)  /* VIDIOC_S_FMT  failed (driver rejected) */
#define K3_VI_ERR_G_FMT         (-22)  /* VIDIOC_G_FMT  failed                   */
#define K3_VI_ERR_REQBUFS       (-23)  /* VIDIOC_REQBUFS failed                  */
#define K3_VI_ERR_QUERYBUF      (-24)  /* VIDIOC_QUERYBUF failed                 */
#define K3_VI_ERR_QBUF          (-25)  /* VIDIOC_QBUF   failed                   */
#define K3_VI_ERR_DQBUF         (-26)  /* VIDIOC_DQBUF  failed                   */
#define K3_VI_ERR_STREAMON      (-27)  /* VIDIOC_STREAMON  failed                */
#define K3_VI_ERR_STREAMOFF     (-28)  /* VIDIOC_STREAMOFF failed                */
#define K3_VI_ERR_SELECT        (-29)  /* select() returned -1 (other than EINTR)*/
#define K3_VI_ERR_NO_DEVICE     (-30)  /* ENODEV / ENXIO when talking to driver  */
#define K3_VI_ERR_PERM          (-31)  /* EACCES / EPERM                         */
#define K3_VI_ERR_DEV_BUSY      (-32)  /* EBUSY from driver (already streaming)  */
#define K3_VI_ERR_TRY_AGAIN     (-33)  /* EAGAIN from non-blocking ioctl         */

#endif /* __AL_VI_K3_DEFS_H__ */
