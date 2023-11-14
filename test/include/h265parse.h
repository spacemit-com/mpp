/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-01 19:35:45
 * @Description:
 */

#ifndef _MPP_H265PARSE_H_
#define _MPP_H265PARSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "type.h"

typedef struct ACCStreamPacket {
  S8 type;
  U8 *data;
  U32 datalen;
  U32 seq;
  U32 capacity;
  U64 timestamp;
} ACCStreamPacket;

typedef enum NALUnitType {
  NAL_TRAIL_N = 0,
  NAL_TRAIL_R = 1,
  NAL_TSA_N = 2,
  NAL_TSA_R = 3,
  NAL_STSA_N = 4,
  NAL_STSA_R = 5,
  NAL_RADL_N = 6,
  NAL_RADL_R = 7,
  NAL_RASL_N = 8,
  NAL_RASL_R = 9,

  NAL_BLA_W_LP = 16,
  NAL_BLA_W_RADL = 17,
  NAL_BLA_N_LP = 18,
  NAL_IDR_W_RADL = 19,
  NAL_IDR_N_LP = 20,
  NAL_CRA_NUT = 21,

  NAL_VPS = 32,
  NAL_SPS = 33,
  NAL_PPS = 34,
  NAL_AUD = 35,
  NAL_EOS_NUT = 36,
  NAL_EOB_NUT = 37,
  NAL_FD_NUT = 38,
  NAL_SEI_PREFIX = 39,
  NAL_SEI_SUFFIX = 40,
} HevcNalUnitType;

#endif /*_MPP_H265PARSE_H_*/
