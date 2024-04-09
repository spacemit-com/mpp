/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-25 09:26:08
 * @LastEditTime: 2024-04-09 16:38:37
 * @FilePath: \mpp\al\vcodec\v4l2\linlonv5v7\include\linlonv5v7_buffer.h
 * @Description:
 */

#ifndef _LINLONV5V7_BUFFER_H_
#define _LINLONV5V7_BUFFER_H_

#include <errno.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dmabufwrapper.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

typedef struct _Buffer Buffer;

/***
 * @description: create a Buffer struct
 * @param {v4l2_buffer} buf
 * @param {S32} fd
 * @param {v4l2_format} format
 * @return {*}
 */
Buffer *createBuffer(struct v4l2_buffer buf, S32 fd, struct v4l2_format format,
                     MppFrameBufferType buffer_type);

/***
 * @description: destory a Buffer struct
 * @param {v4l2_buffer} buf
 * @return {*}
 */
void destoryBuffer(Buffer *buf);

/**
 * @description: get the v4l2_buffer of Buffer
 * @param {Buffer} *buf
 * @return {*}
 */
struct v4l2_buffer *getV4l2Buffer(Buffer *buf);

/**
 * @description: get pUserPtr[index] of Buffer
 * @param {Buffer} *buf
 * @param {S32} index
 * @return {*}
 */
U8 *getUserPtr(Buffer *buf, S32 index);

/**
 * @description: get pUserPtr[index] of Buffer, only for Hevc and VP9
 * @param {Buffer} *buf
 * @param {S32} index
 * @return {*}
 */
U8 *getUserPtrForHevcAndVp9Encode(Buffer *buf, S32 index);

/**
 * @description: set pUserPtr[index] with ptr
 * @param {Buffer} *buf
 * @param {S32} index
 * @param {U8} *ptr
 * @return {*}
 */
void setUserPtr(Buffer *buf, S32 index, U8 *ptr);

/**
 * @description: set external dmabuf info to Buffer
 * @param {Buffer} *buf
 * @param {S32} fd
 * @param {U8} *ptr
 * @param {S32} extra_id
 * @return {*}
 */
S32 setExternalDmaBuf(Buffer *buf, S32 fd, U8 *ptr, S32 extra_id);

/**
 * @description: set external userptr info to Buffer
 * @param {Buffer} *buf
 * @param {U8} *ptr0
 * @param {U8} *ptr1
 * @param {U8} *ptr2
 * @param {S32} extra_id
 * @return {*}
 */
S32 setExternalUserPtrFrame(Buffer *buf, U8 *ptr0, U8 *ptr1, U8 *ptr2,
                            S32 extra_id);

/**
 * @description: get the v4l2_format of Buffer
 * @param {Buffer} *buf
 * @return {*}
 */
struct v4l2_format *getFormat(Buffer *buf);

/**
 * @description: set crop info to Buffer
 * @param {Buffer} *buf
 * @param {v4l2_crop} crop
 * @return {*}
 */
void setCrop(Buffer *buf, struct v4l2_crop crop);

/**
 * @description: get crop info from Buffer
 * @param {Buffer} *buf
 * @return {*}
 */
struct v4l2_crop *getCrop(Buffer *buf);

/**
 * @description: set bytesused info to Buffer
 * @param {Buffer} *buf
 * @param {S32} iov_size
 * @param {S32} iov
 * @return {*}
 */
void setBytesUsed(Buffer *buf, S32 iov_size, S32 iov[VIDEO_MAX_PLANES]);

/**
 * @description: get bytesused info from v4l2_buffer
 * @param {v4l2_buffer} *buf
 * @return {*}
 */
S32 getBytesUsed(struct v4l2_buffer *buf);

/**
 * @description: clear bytesused info of Buffer
 * @param {Buffer} *buf
 * @return {*}
 */
void clearBytesUsed(Buffer *buf);

/**
 * @description: clear vendor flags ofr Buffer
 * @param {Buffer} *buf
 * @return {*}
 */
void resetVendorFlags(Buffer *buf);

/**
 * @description: set codec config flag, SPS/PPS/VPS, etc
 * @param {Buffer} *buf
 * @param {BOOL} codecConfig
 * @return {*}
 */
void setCodecConfig(Buffer *buf, BOOL codecConfig);

/**
 * @description: set timestamp to Buffer
 * @param {Buffer} *buf
 * @param {S64} timeUs
 * @return {*}
 */
void setTimeStamp(Buffer *buf, S64 timeUs);

/**
 * @description: set end of frame flag
 * @param {Buffer} *buf
 * @param {BOOL} eof
 * @return {*}
 */
void setEndOfFrame(Buffer *buf, BOOL eof);

/**
 * @description: set end of stream flag
 * @param {Buffer} *buf
 * @param {BOOL} eos
 * @return {*}
 */
void setEndOfStream(Buffer *buf, BOOL eos);

/**
 * @description: update the v4l2_buffer info of Buffer
 * @param {Buffer} *buf
 * @param {v4l2_buffer} b
 * @return {*}
 */
void update(Buffer *buf, struct v4l2_buffer b);
void setInterlaced(Buffer *buf, BOOL interlaced);
void setTiled(Buffer *buf, BOOL tiled);
S32 setRotation(Buffer *buf, S32 rotation);
S32 setMirror(Buffer *buf, S32 mirror);
S32 setDownScale(Buffer *buf, S32 scale);
void setEndOfSubFrame(Buffer *buf, BOOL eos);
void setRoiCfg(Buffer *buf, struct v4l2_mvx_roi_regions roi);
BOOL getRoiCfgflag(Buffer *buf);
struct v4l2_mvx_roi_regions getRoiCfg(Buffer *buf);
void setSuperblock(Buffer *buf, BOOL superblock);
void setROIflag(Buffer *buf);
void setEPRflag(Buffer *buf);
void setQPofEPR(Buffer *buf, S32 data);
S32 getQPofEPR(Buffer *buf);
BOOL isGeneralBuffer(Buffer *buf);

void memoryMap(Buffer *buf, S32 fd);
S32 memoryUnmap(Buffer *buf);
S32 getLength(Buffer *buf, U32 plane);
S32 getExtraId(Buffer *buf);
S32 getExtraFd(Buffer *buf);
BOOL getIsQueued(Buffer *buf);
S32 setIsQueued(Buffer *buf, BOOL queued);

#endif /*_LINLONV5V7_BUFFER_H_*/
