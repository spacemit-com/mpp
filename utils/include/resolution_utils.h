/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-10 15:52:19
 * @LastEditTime: 2023-02-02 14:53:49
 * @Description:
 */

#ifndef _MPP_RESOLUTION_UTILS_H_
#define _MPP_RESOLUTION_UTILS_H_

#include <stdint.h>
#include <stdio.h>

#include "type.h"

#define NAL_SPS 0x07     /* Sequence Parameter Set */
#define NAL_AUD 0x09     /* Access Unit Delimiter */
#define NAL_END_SEQ 0x0a /* End of Sequence */

#define IS_NAL_SPS(buf) \
  ((buf)[0] == 0 && (buf)[1] == 0 && (buf)[2] == 1 && (buf)[3] == NAL_SPS)
#define IS_NAL_AUD(buf) \
  ((buf)[0] == 0 && (buf)[1] == 0 && (buf)[2] == 1 && (buf)[3] == NAL_AUD)
#define IS_NAL_END_SEQ(buf) \
  ((buf)[0] == 0 && (buf)[1] == 0 && (buf)[2] == 1 && (buf)[3] == NAL_END_SEQ)

typedef struct mpeg_rational_s {
  int num;
  int den;
} mpeg_rational_t;

typedef struct video_size_s {
  uint16_t width;
  uint16_t height;
  mpeg_rational_t pixel_aspect;
} video_size_t;

typedef struct {
  uint16_t width;
  uint16_t height;
  mpeg_rational_t pixel_aspect;
  uint8_t profile;
  uint8_t level;
} h264_sps_data_t;

struct video_size_s;

/*
 * input: start of NAL SPS ( 00 00 01 07 or 00 00 00 01 67 0r 67)
 */
int h264_parse_sps(uint8_t *buf, int len, h264_sps_data_t *sps);
S32 get_resolution_from_stream(U8 *buf, S32 buf_length, S32 *width,
                               S32 *height);

#endif /*_MPP_RESOLUTION_UTILS_H_*/
