/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-09-26 19:29:22
 * @LastEditTime: 2023-09-26 19:34:41
 * @Description:
 */

#ifndef _LINLONV5V7_CODEC_H_
#define _LINLONV5V7_CODEC_H_

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "linlonv5v7_buffer.h"
#include "linlonv5v7_port.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

enum NaluFormat {
  NALU_FORMAT_START_CODES,
  NALU_FORMAT_ONE_NALU_PER_BUFFER,
  NALU_FORMAT_ONE_BYTE_LENGTH_FIELD,
  NALU_FORMAT_TWO_BYTE_LENGTH_FIELD,
  NALU_FORMAT_FOUR_BYTE_LENGTH_FIELD
};

typedef struct _Codec Codec;

Codec *createCodec(S32 fd, S32 width, S32 height, BOOL isInterlaced,
                   enum v4l2_buf_type inputType, enum v4l2_buf_type outputType,
                   U32 input_format_fourcc, U32 output_format_fourcc,
                   U32 input_memtype, U32 output_memtype, U32 input_buffer_num,
                   U32 output_buffer_num, BOOL block);
void destoryCodec(Codec *codec);

Port *getInputPort(Codec *codec);
Port *getOutputPort(Codec *codec);
BOOL getCsweo(Codec *codec);
U32 getFps(Codec *codec);
U32 getBps(Codec *codec);
U32 getMinqp(Codec *codec);
U32 getMaxqp(Codec *codec);
U32 getFixedqp(Codec *codec);
void setCsweo(Codec *codec, BOOL csweo);
void setFps(Codec *codec, U32 fps);
void setBps(Codec *codec, U32 bps);
void setMinqp(Codec *codec, U32 minqp);
void setMaxqp(Codec *codec, U32 maxqp);
void setFixedqp(Codec *codec, U32 fixedqp);

S32 stream(Codec *codec);

BOOL isVPx(U32 format);
BOOL isAFBC(U32 format);

void enumerateCodecFormats(Codec *codec);

void openDev(Codec *codec);
void closeDev(Codec *codec);

void queryCapabilities(Codec *codec);
void enumerateFramesizes(Codec *codec, U32 format);
void setFormats(Codec *codec);

struct v4l2_mvx_color_desc getColorDesc(Codec *codec);

void subscribeEvents(Codec *codec);
void subscribeEvent(Codec *codec, U32 event);
void unsubscribeEvents(Codec *codec);
void unsubscribeEvent(Codec *codec, U32 event);

void allocateCodecBuffers(Codec *codec);
void freeCodecBuffers(Codec *codec);
void queueCodecBuffers(Codec *codec, BOOL eof);

void streamonCodec(Codec *codec);
void streamoffCodec(Codec *codec);

// void runPoll();
// void runThreads();
// static void *runThreadInput(void *arg);
// static void *runThreadOutput(void *arg);

BOOL handleEvent(Codec *codec);
void handleFlush(Codec *codec, BOOL eof);

S32 runPoll(Codec *codec, struct pollfd *p);

#endif /*_LINLONV5V7_CODEC_H_*/