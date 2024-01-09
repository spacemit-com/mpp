/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-10-07 14:08:18
 * @LastEditTime: 2023-10-07 16:08:51
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
Buffer *createBuffer(struct v4l2_buffer buf, S32 fd, struct v4l2_format format);
void destoryBuffer(Buffer *buf);

struct v4l2_buffer *getV4l2Buffer(Buffer *buf);
U8 *getUserPtr(Buffer *buf, S32 index);
void setUserPtr(Buffer *buf, S32 index, U8 *ptr);
struct v4l2_format *getFormat(Buffer *buf);
void setCrop(Buffer *buf, struct v4l2_crop crop);
struct v4l2_crop getCrop(Buffer *buf);
void setBytesUsed(Buffer *buf, S32 iov_size, S32 iov[VIDEO_MAX_PLANES]);
S32 getBytesUsed(struct v4l2_buffer *buf);
void clearBytesUsed(Buffer *buf);
void resetVendorFlags(Buffer *buf);
void setCodecConfig(Buffer *buf, BOOL codecConfig);
void setTimeStamp(Buffer *buf, S64 timeUs);
void setEndOfFrame(Buffer *buf, BOOL eof);
void setEndOfStream(Buffer *buf, BOOL eos);
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

#endif /*_LINLONV5V7_BUFFER_H_*/
