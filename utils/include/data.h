/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: MppData - base class of MppPacket and MppFrame
 */

#ifndef _MPP_DATA_H_
#define _MPP_DATA_H_

#include "para.h"
#include "type.h"

typedef enum _MppDataType {
  MPP_DATA_STREAM = 1,
  MPP_DATA_FRAME = 2,
  MPP_DATA_UNKNOWN = 1023
} MppDataType;

typedef struct _MppData {
  MppDataType eType;
} MppData;

#endif /*_MPP_DATA_H_*/
