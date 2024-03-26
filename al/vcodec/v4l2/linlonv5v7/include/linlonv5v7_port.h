/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-20 19:29:29
 * @LastEditTime: 2024-03-26 09:46:18
 * @FilePath: \mpp\al\vcodec\v4l2\linlonv5v7\include\linlonv5v7_port.h
 * @Description:
 */

#ifndef _LINLONV5V7_PORT_H_
#define _LINLONV5V7_PORT_H_

#include <errno.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "data.h"
#include "frame.h"
#include "linlonv5v7_buffer.h"
#include "linlonv5v7_constant.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "packet.h"
#include "v4l2_utils.h"

typedef enum _DIRECTION {
  INPUT = 0,
  OUTPUT = 1,
} DIRECTION;

typedef struct _Port Port;

/**
 * @description: create a port for decode or encode
 * @return {*}: context of port
 */
Port *createPort(S32 fd, enum v4l2_buf_type type, U32 format_fourcc,
                 U32 memtype, U32 buffer_num, MppFrameBufferType buffer_type);

/**
 * @description: destory the port after using
 * @param {Port} *port: context of the port
 * @return {*}
 */
void destoryPort(Port *port);

/**
 * @description: get Buffer by index, linlonv5v7_dec and linlonv5v7_enc use it,
 * in fact, dec and enc should not operate Buffer directly, optimize later
 * @param {Port} *port: context of the port
 * @param {S32} index: index of the Buffer you want
 * @return {*}: context of the Buffer
 */
Buffer *getBuffer(Port *port, S32 index);

/**
 * @description: get the buf type of the port
 * @param {Port} *port: context of the port
 * @return {*}: buf type of the port
 */
enum v4l2_buf_type getV4l2BufType(Port *port);

/**
 * @description: get fourcc of the port
 * @param {Port} *port: context of the port
 * @return {*}: fourcc of the port
 */
U32 getFormatFourcc(Port *port);

/**
 * @description: enumerate formats the port supports
 * @param {Port} *port: context of the port
 * @return {*}
 */
void enumerateFormats(Port *port);

/**
 * @description: get format of the port
 * @param {Port} *port: context of the port
 * @return {*}: v4l2_format
 */
struct v4l2_format getPortFormat(Port *port);

/**
 * @description: try format on the port
 * @param {Port} *port: context of the port
 * @param {v4l2_format} format: v4l2_format that will be tryed
 * @return {*}
 */
void tryFormat(Port *port, struct v4l2_format format);

/**
 * @description: set format for the port
 * @param {Port} *port: context of the port
 * @param {v4l2_format} format: v4l2_format that will be set
 * @return {*}
 */
void setFormat(Port *port, struct v4l2_format format);

/**
 * @description: get/try/set format together for the port
 * @param {Port} *port: context of the port
 * @param {S32} width: width set to driver
 * @param {S32} height: height set to driver
 * @param {U32} pixel_format: pixel format set to driver
 * @param {BOOL} interlaced: whether interlaced
 * @return {*}
 */
void getTrySetFormat(Port *port, S32 width, S32 height, U32 pixel_format,
                     BOOL interlaced);

/**
 * @description: use for debug
 * @param {v4l2_format} format: v4l2_format to be printed
 * @return {*}
 */
void printFormat(const struct v4l2_format format);

/**
 * @description: get crop info of the port
 * @param {Port} *port: context of the port
 * @return {*}: v4l2_crop
 */
struct v4l2_crop getPortCrop(Port *port);

/**
 * @description: allocate buffers for the port
 * @param {Port} *port: context of the port
 * @param {S32} count: num of buffers need to be allocated
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 allocateBuffers(Port *port, S32 count);

/**
 * @description: free buffers of the port
 * @param {Port} *port: context of the port
 * @return {*}
 */
void freeBuffers(Port *port);

/**
 * @description: get buffer num of the port
 * @param {Port} *port: context of the port
 * @return {*}: num of buffer num
 */
U32 getBufferCount(Port *port);

/**
 * @description: queue all buffers to driver
 * @param {Port} *port: context of the port
 * @param {BOOL} eof: end of file
 * @return {*}
 */
void queueBuffers(Port *port, BOOL eof);

/**
 * @description: queue one buffer of the port to driver
 * @param {Port} *port: context of the port
 * @param {Buffer} *buf: buffer need to be queued
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 queueBuffer(Port *port, Buffer *buf);

/**
 * @description: dequeue one buffer from driver
 * @param {Port} *port: context of the port
 * @return {*}: context of the buffer
 */
Buffer *dequeueBuffer(Port *port);

/**
 * @description: use for debug, printf the buffer status
 * @param {Port} *port: context of the port
 * @param {v4l2_buffer} buf: buffer need to be printed
 * @param {U8} *prefix
 * @return {*}
 */
void printBuffer(Port *port, struct v4l2_buffer buf, const U8 *prefix);

BOOL handleBuffer(Port *port, BOOL eof, MppData *data);
S32 handleInputBuffer(Port *port, BOOL eof, MppData *data);
S32 handleOutputBuffer(Port *port, BOOL eof, MppData *data);

/**
 * @description: handle resolution changed info
 * @param {Port} *port: context of the port
 * @param {BOOL} eof: end of file
 * @return {*}
 */
void handleResolutionChange(Port *port, BOOL eof);

void streamon(Port *port);
void streamoff(Port *port);

void sendEncStopCommand(Port *port);
void sendDecStopCommand(Port *port);

void setH264DecIntBufSize(Port *port, U32 ibs);
void setNALU(Port *port, enum v4l2_nalu_format nalu);
void setEncFramerate(Port *port, U32 frame_rate);
void setEncBitrate(Port *port, U32 bit_rate);
void setEncPFrames(Port *port, U32 pframes);
void setEncBFrames(Port *port, U32 pframes);
void setEncSliceSpacing(Port *port, U32 spacing);
void setH264EncForceChroma(Port *port, U32 fmt);
void setH264EncBitdepth(Port *port, U32 bd);
void setH264EncIntraMBRefresh(Port *port, U32 period);
void setEncProfile(Port *port, U32 profile);
void setEncLevel(Port *port, U32 level);
void setEncConstrainedIntraPred(Port *port, U32 cip);
void setH264EncEntropyMode(Port *port, U32 ecm);
void setH264EncGOPType(Port *port, U32 gop);
void setH264EncMinQP(Port *port, U32 minqp);
void setH264EncMaxQP(Port *port, U32 maxqp);
void setH264EncFixedQP(Port *port, U32 fqp);
void setH264EncFixedQPI(Port *port, U32 fqp);
void setH264EncFixedQPP(Port *port, U32 fqp);
void setH264EncFixedQPB(Port *port, U32 fqp);
void setH264EncBandwidth(Port *port, U32 bw);
void setHEVCEncMinQP(Port *port, U32 minqp);
void setHEVCEncMaxQP(Port *port, U32 maxqp);
void setHEVCEncEntropySync(Port *port, U32 es);
void setHEVCEncTemporalMVP(Port *port, U32 tmvp);
void setEncStreamEscaping(Port *port, U32 sesc);
void setEncHorizontalMVSearchRange(Port *port, U32 hmvsr);
void setEncVerticalMVSearchRange(Port *port, U32 vmvsr);
void setVP9EncTileCR(Port *port, U32 tcr);
void setJPEGEncQuality(Port *port, U32 q);
void setJPEGEncRefreshInterval(Port *port, U32 r);
void setPortInterlaced(Port *port, BOOL interlaced);
void setPortRotation(Port *port, S32 rotation);
void setPortMirror(Port *port, S32 mirror);
void setPortDownScale(Port *port, S32 scale);
void tryEncStopCmd(Port *port, BOOL tryStop);
void tryDecStopCmd(Port *port, BOOL tryStop);
void setDecFrameReOrdering(Port *port, U32 fro);
void setDecIgnoreStreamHeaders(Port *port, U32 ish);
void setFrameCount(Port *port, S32 frames);
void setRateControl(Port *port, struct v4l2_rate_control *rc);
void setCropLeft(Port *port, S32 left);
void setCropRight(Port *port, S32 right);
void setCropTop(Port *port, S32 top);
void setCropBottom(Port *port, S32 bottom);
void setVuiColourDesc(Port *port, struct v4l2_mvx_color_desc *color);
void setSeiUserData(Port *port, struct v4l2_sei_user_data *sei_user_data);
void setHRDBufferSize(Port *port, S32 size);
void setDSLFrame(Port *port, S32 width, S32 height);
void setDSLRatio(Port *port, S32 hor, S32 ver);
void setLongTermRef(Port *port, U32 mode, U32 period);
void setDSLMode(Port *port, S32 mode);

S32 getBufNum(Port *port);
S32 getBufWidth(Port *port);
S32 getBufHeight(Port *port);
S32 getBufFd(Port *port, U32 index);

void notifySourceChange(Port *port);

#endif /*_LINLONV5V7_PORT_H_*/
