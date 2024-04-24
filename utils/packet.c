/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-24 16:43:56
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "packet.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "log.h"

static S32 num_of_unfree_packet = 0;
static S32 num_of_unfree_packet_data = 0;

struct _MppPacket {
  /**
   * parent class
   */
  MppData eBaseData;

  /**
   * video parameter
   */
  MppPixelFormat ePixelFormat;
  S32 nWidth;
  S32 nHeight;
  S32 nLineStride;
  S32 nFrameRate;

  /**
   * packet parameter
   */
  U8 *pData;
  S32 nTotalSize;  // total size that PACKET_Alloc
  S32 nLength;     // data length, nLength <= nTotalSize
  void *pMetaData;
  S32 nID;
  S64 nPts;
  S64 nDts;
  BOOL bEos;

  // environment variable
  BOOL bEnableUnfreePacketDebug;
  // S64    nPcr;
  // S32    bIsFirstPart;
  // S32    bIsLastPart;
};

MppPacket *PACKET_Create() {
  MppPacket *packet = (MppPacket *)malloc(sizeof(MppPacket));
  if (!packet) {
    error("can not malloc MppPacket, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(packet, 0, sizeof(MppPacket));

  mpp_env_get_u32("MPP_PRINT_UNFREE_PACKET",
                  &(packet->bEnableUnfreePacketDebug), 0);

  if (packet->bEnableUnfreePacketDebug) {
    num_of_unfree_packet++;
    info("++++++++++ debug packet memory: num of unfree packet: %d",
         num_of_unfree_packet);
  }

  return packet;
}

MppPacket *PACKET_Copy(MppPacket *src_packet) {
  S32 ret = 0;

  if (!src_packet) {
    error("src_packet is NULL, please check!");
    return NULL;
  }

  MppPacket *dst_packet = (MppPacket *)malloc(sizeof(MppPacket));
  if (!dst_packet) {
    error("can not malloc MppPacket, please check! (%s)", strerror(errno));
    return NULL;
  }

  if (src_packet->bEnableUnfreePacketDebug) {
    num_of_unfree_packet++;
    info("++++++++++ debug packet memory: num of unfree packet: %d",
         num_of_unfree_packet);
  }

  memset(dst_packet, 0, sizeof(MppPacket));
  memcpy(dst_packet, src_packet, sizeof(MppPacket));

  if (src_packet->nTotalSize) {
    ret = PACKET_Alloc(dst_packet, src_packet->nTotalSize);
  } else {
    ret = PACKET_Alloc(dst_packet, src_packet->nLength);
  }

  if (MPP_OK != ret) {
    error("alloc packet, but can not alloc data, please check!");
    free(dst_packet);

    if (src_packet->bEnableUnfreePacketDebug) {
      num_of_unfree_packet--;
      info("---------- debug packet memory: num of unfree packet: %d",
           num_of_unfree_packet);
    }

    return NULL;
  }

  PACKET_SetLength(dst_packet, src_packet->nLength);
  memcpy(dst_packet->pData, src_packet->pData, src_packet->nLength);

  return dst_packet;
}

S32 PACKET_GetStructSize() { return sizeof(MppPacket); }

RETURN PACKET_Alloc(MppPacket *packet, S32 size) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (size <= 0) {
    error("input para size <= 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->pData = (U8 *)malloc(size);
  if (!packet->pData) {
    error("can not malloc MppPacket->pData, please check! (%s)",
          strerror(errno));
    return MPP_MALLOC_FAILED;
  }
  packet->nTotalSize = size;
  packet->nLength = 0;

  if (packet->bEnableUnfreePacketDebug) {
    num_of_unfree_packet_data++;
    info("++++++++++ debug packet memory: num of unfree packet data: %d",
         num_of_unfree_packet_data);
  }

  return MPP_OK;
}

RETURN PACKET_Free(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (packet->pData) {
    free(packet->pData);
    packet->pData = NULL;
  }

  packet->nLength = 0;
  packet->nTotalSize = 0;

  if (packet->bEnableUnfreePacketDebug) {
    num_of_unfree_packet_data--;
    info("---------- debug packet memory: num of unfree packet data: %d",
         num_of_unfree_packet_data);
  }

  return MPP_OK;
}

MppData *PACKET_GetBaseData(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return NULL;
  }

  return &(packet->eBaseData);
}

MppPacket *PACKET_GetPacket(MppData *base_data) {
  if (!base_data) {
    error("input para MppData is NULL, please check!");
    return NULL;
  }

  return (MppPacket *)base_data;
}

RETURN PACKET_SetLength(MppPacket *packet, S32 length) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (length < 0) {
    error("input para length < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->nLength = length;

  return MPP_OK;
}

S32 PACKET_GetLength(MppPacket *packet) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nLength;
}

void *PACKET_GetDataPointer(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return NULL;
  }

  if (!packet->pData) {
    error("input para MppPacket->pData is NULL, please check!");
    return NULL;
  }

  return (void *)packet->pData;
}

RETURN PACKET_SetDataPointer(MppPacket *packet, U8 *data_pointer) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }
  /*
    if (!data_pointer) {
      error("input para data_pointer is NULL, please check!");
      return MPP_NULL_POINTER;
    }
  */
  packet->pData = data_pointer;

  return MPP_OK;
}

void *PACKET_GetMetaData(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return NULL;
  }

  if (!packet->pMetaData) {
    error("input para MppPacket->pMetaData is NULL, please check!");
    return NULL;
  }

  return (void *)packet->pMetaData;
}

RETURN PACKET_SetMetaData(MppPacket *packet, void *metadata_pointer) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!metadata_pointer) {
    error("input para metadata_pointer is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  packet->pMetaData = metadata_pointer;

  return MPP_OK;
}

S32 PACKET_GetID(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nID;
}

RETURN PACKET_SetID(MppPacket *packet, S32 id) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (id < 0) {
    error("id < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->nID = id;

  return MPP_OK;
}

S64 PACKET_GetPts(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nPts;
}

RETURN PACKET_SetPts(MppPacket *packet, S64 pts) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  packet->nPts = pts;

  return MPP_OK;
}

S64 PACKET_GetDts(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nDts;
}

RETURN PACKET_SetDts(MppPacket *packet, S64 dts) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  packet->nDts = dts;

  return MPP_OK;
}

BOOL PACKET_GetEos(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->bEos;
}

RETURN PACKET_SetEos(MppPacket *packet, BOOL eos) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  packet->bEos = eos;

  return MPP_OK;
}

RETURN PACKET_SetWidth(MppPacket *packet, S32 width) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (width < 0) {
    error("input para width < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->nWidth = width;

  return MPP_OK;
}

S32 PACKET_GetWidth(MppPacket *packet) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nWidth;
}

RETURN PACKET_SetHeight(MppPacket *packet, S32 height) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (height < 0) {
    error("input para height < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->nHeight = height;

  return MPP_OK;
}

S32 PACKET_GetHeight(MppPacket *packet) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nHeight;
}

RETURN PACKET_SetLineStride(MppPacket *packet, S32 line_stride) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (line_stride < 0) {
    error("input para line_stride < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->nLineStride = line_stride;

  return MPP_OK;
}

S32 PACKET_GetLineStride(MppPacket *packet) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->nLineStride;
}

RETURN PACKET_SetPixelFormat(MppPacket *packet, S32 pixel_format) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (pixel_format < 0) {
    error("input para pixel_format < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  packet->ePixelFormat = pixel_format;

  return MPP_OK;
}

S32 PACKET_GetPixelFormat(MppPacket *packet) {
  if (!packet) {
    error("input para packet is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return packet->ePixelFormat;
}

void PACKET_Destory(MppPacket *packet) {
  if (!packet) {
    error("input para MppPacket is NULL, please check!");
    return;
  }

  if (packet->bEnableUnfreePacketDebug) {
    num_of_unfree_packet--;
    info("---------- debug packet memory: num of unfree packet: %d",
         num_of_unfree_packet);
  }

  free(packet);
  // packet = NULL;
}
