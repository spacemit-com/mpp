/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-17 13:48:01
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "frame.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "env.h"
#include "log.h"

S32 num_of_unfree_frame = 0;
S32 num_of_unfree_data = 0;

struct _MppFrame {
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
   * frame parameter
   */
  S64 nPts;
  MppFrameEos bEos;
  MppFrameBufferType eBufferType;
  S32 nDataUsedNum;
  S32 nID;
  U8 *pData0;
  U8 *pData1;
  U8 *pData2;
  U8 *pData3;
  void *pMetaData;
  S32 nFd[MPP_MAX_PLANES];
  U32 refCount;
  DmaBufWrapper *pDmaBufWrapper;

  // environment variable
  BOOL bEnableUnfreeFrameDebug;

  // S32     nStreamIndex;
  // S32     nTopOffset;
  // S32     nLeftOffset;
  // S32     nBottomOffset;
  // S32     nRightOffset;
  // S32     nFrameRate;
  // S32     nAspectRatio;
  // S32     bIsProgressive;
  // S32     bTopFieldFirst;
  // S32     bRepeatTopField;
  // S64     nPcr;
  // S32     bMafValid;
  // U8*     pMafData;
  // S32     nMafFlagStride;
  // S32     bPreFrmValid;
  // S32     nBufId;
  // S64     phyYBufAddr;
  // S64     phyCBufAddr;
  // void*      pPrivate;
  // S32     nBufStatus;
  // S32     bTopFieldError;
  // S32     bBottomFieldError;
  // S32     nColorPrimary;  // default value is 0xffffffff, valid value id
  // 0x0000xxyy
  //  xx: is video full range code
  //  yy: is matrix coefficient
};

MppFrame *FRAME_Create() {
  MppFrame *frame = (MppFrame *)malloc(sizeof(MppFrame));

  if (!frame) {
    error("can not malloc MppFrame! please check! (%s)", strerror(errno));
    return NULL;
  }
  memset(frame, 0, sizeof(MppFrame));

  mpp_env_get_u32("MPP_PRINT_UNFREE_FRAME", &(frame->bEnableUnfreeFrameDebug),
                  0);

  if (frame->bEnableUnfreeFrameDebug) {
    num_of_unfree_frame++;
    info("++++++++++ debug frame memory: num of unfree frame: %d",
         num_of_unfree_frame);
  }

  frame->nDataUsedNum = 1;
  frame->eBufferType = MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL;
  frame->refCount = 1;

  return frame;
}

S32 FRAME_GetStructSize() { return sizeof(MppFrame); }

RETURN FRAME_Alloc(MppFrame *frame, MppPixelFormat pixelformat, S32 width,
                   S32 height) {
  S32 size[3];
  S32 i;
  S32 ret = 0;
  S32 fd = 0;

  if (!frame) {
    error("input para MppFrame is NULL, please check!!");
    return MPP_NULL_POINTER;
  }

  if (pixelformat <= 0) {
    error("input para pixelformat <= 0, please check!!");
    return MPP_CHECK_FAILED;
  }

  if (width <= 0 || height <= 0) {
    error("input para width <= 0 or height <= 0, please check!!");
    return MPP_CHECK_FAILED;
  }

  if (MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL == frame->eBufferType) {
    switch (pixelformat) {
      case PIXEL_FORMAT_I420:
      case PIXEL_FORMAT_YV12:
        size[0] = width * height;
        size[1] = (width / 2) * (height / 2);
        size[2] = (width / 2) * (height / 2);
        frame->nDataUsedNum = 3;
        break;
      case PIXEL_FORMAT_YUV422P:
        size[0] = width * height;
        size[1] = (width / 2) * height;
        size[2] = (width / 2) * height;
        frame->nDataUsedNum = 3;
        break;
      case PIXEL_FORMAT_NV12:
      case PIXEL_FORMAT_NV21:
        size[0] = width * height;
        size[1] = (width / 2) * height;
        frame->nDataUsedNum = 2;
        break;
      case PIXEL_FORMAT_RGBA:
      case PIXEL_FORMAT_ARGB:
      case PIXEL_FORMAT_BGRA:
      case PIXEL_FORMAT_ABGR:
        size[0] = width * height * 4;
        frame->nDataUsedNum = 1;
        break;
      case PIXEL_FORMAT_YUYV:
      case PIXEL_FORMAT_UYVY:
        size[0] = width * height * 2;
        frame->nDataUsedNum = 1;
        break;
      default:
        error("Unsupported picture format (%d)! Please check!", pixelformat);
        return MPP_NOT_SUPPORTED_FORMAT;
    }

    frame->ePixelFormat = pixelformat;

    for (i = 0; i < frame->nDataUsedNum; i++) {
      if (i == 0) {
        frame->pData0 = (U8 *)malloc(size[i]);
        if (!frame->pData0) {
          error("can not malloc MppFrame->pData0, please check! (%s)",
                strerror(errno));
          return MPP_MALLOC_FAILED;
        }
        memset(frame->pData0, 0, size[i]);
      } else if (i == 1) {
        frame->pData1 = (U8 *)malloc(size[i]);
        if (!frame->pData1) {
          error("can not malloc MppFrame->pData1, please check! (%s)",
                strerror(errno));
          free(frame->pData0);
          return MPP_MALLOC_FAILED;
        }
        memset(frame->pData1, 0, size[i]);
      } else {
        frame->pData2 = (U8 *)malloc(size[i]);
        if (!frame->pData2) {
          error("can not malloc MppFrame->pData2, please check! (%s)",
                strerror(errno));
          free(frame->pData0);
          free(frame->pData1);
          return MPP_MALLOC_FAILED;
        }
        memset(frame->pData2, 0, size[i]);
      }
    }
  } else if (MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL == frame->eBufferType) {
    frame->nDataUsedNum = 1;

    frame->pDmaBufWrapper = createDmaBufWrapper(DMA_HEAP_CMA);
    if (!frame->pDmaBufWrapper) {
      return MPP_NULL_POINTER;
    }

    frame->nFd[0] = allocDmaBuf(frame->pDmaBufWrapper, width * height * 3 / 2);
    if (frame->nFd[0] < 0) {
      return MPP_IOCTL_FAILED;
    }
    debug("alloc dma buf success! fd = %d", frame->nFd[0]);

    frame->pData0 = (U8 *)mmapDmaBuf(frame->pDmaBufWrapper);
    debug("dma buf mmap success! pData0 = %p", frame->pData0);

    if (frame->bEnableUnfreeFrameDebug) num_of_unfree_data++;

    frame->nWidth = width;
    frame->nHeight = height;
  } else {
    error("unsupported buffertype, please check!!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  return MPP_OK;
}

RETURN FRAME_Free(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!!");
    return MPP_NULL_POINTER;
  }

  if (MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL == frame->eBufferType) {
    if (frame->pData0) {
      free(frame->pData0);
      frame->pData0 = NULL;
    }
    if (frame->pData1) {
      free(frame->pData1);
      frame->pData1 = NULL;
    }
    if (frame->pData2) {
      free(frame->pData2);
      frame->pData2 = NULL;
    }
    if (frame->pData3) {
      free(frame->pData3);
      frame->pData3 = NULL;
    }
  } else if (MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL == frame->eBufferType) {
    if (freeDmaBuf(frame->pDmaBufWrapper)) {
      error("can not free dmabuf, please check!");
      return MPP_CHECK_FAILED;
    }

    destoryDmaBufWrapper(frame->pDmaBufWrapper);

    if (frame->bEnableUnfreeFrameDebug) {
      num_of_unfree_data--;
      info("debug frame memory: num of unfree frame data: %d",
           num_of_unfree_data);
    }
  } else {
    error("unsupported buffertype, please check!!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  return MPP_OK;
}

RETURN FRAME_SetDataUsedNum(MppFrame *frame, S32 data_used_num) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (data_used_num <= 0 || data_used_num > MPP_MAX_PLANES) {
    error(
        "input para data_used_num <= 0 || data_used_num > MPP_MAX_PLANES, "
        "please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nDataUsedNum = data_used_num;

  return MPP_OK;
}

S32 FRAME_GetDataUsedNum(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nDataUsedNum;
}

MppData *FRAME_GetBaseData(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return NULL;
  }

  return &(frame->eBaseData);
}

MppFrame *FRAME_GetFrame(MppData *base_data) {
  if (!base_data) {
    error("input para MppData is NULL, please check!");
    return NULL;
  }

  return (MppFrame *)base_data;
}

void *FRAME_GetDataPointer(MppFrame *frame, S32 data_num) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return NULL;
  }

  if (data_num < 0 || data_num >= frame->nDataUsedNum) {
    error("data_num is not valid %d %d, please check!", data_num,
          frame->nDataUsedNum);
    return NULL;
  }

  switch (data_num) {
    case 0:
      return (void *)(frame->pData0);
      break;
    case 1:
      return (void *)(frame->pData1);
      break;
    case 2:
      return (void *)(frame->pData2);
      break;
    case 3:
      return (void *)(frame->pData3);
      break;
    default:
      break;
  }

  return NULL;
}

RETURN FRAME_SetDataPointer(MppFrame *frame, S32 data_num, U8 *data) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!data) {
    error("input para data is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (data_num < 0 || data_num >= frame->nDataUsedNum) {
    error("input para data_num is not valid %d %d, please check!", data_num,
          frame->nDataUsedNum);
    return MPP_CHECK_FAILED;
  }

  switch (data_num) {
    case 0:
      frame->pData0 = data;
      break;
    case 1:
      frame->pData1 = data;
      break;
    case 2:
      frame->pData2 = data;
      break;
    case 3:
      frame->pData3 = data;
      break;
    default:
      break;
  }

  return MPP_OK;
}

void *FRAME_GetMetaData(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return NULL;
  }

  return frame->pMetaData;
}

RETURN FRAME_SetMetaData(MppFrame *frame, void *meta_data) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!meta_data) {
    error("input para meta_data is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  frame->pMetaData = meta_data;

  return MPP_OK;
}

U32 FRAME_Ref(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  frame->refCount++;

  return frame->refCount;
}
U32 FRAME_UnRef(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (frame->refCount == 0) {
    error("frame unref error, please check!");
    return MPP_CHECK_FAILED;
  }

  if (frame->refCount == 1) {
    debug("frame unref to 0, need to be free.");
  }

  frame->refCount--;

  return frame->refCount;
}
U32 FRAME_GetRef(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->refCount;
}

S32 FRAME_GetID(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nID;
}

RETURN FRAME_SetID(MppFrame *frame, S32 id) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (id < 0) {
    error("input para id < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nID = id;

  return MPP_OK;
}

S64 FRAME_GetPts(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nPts;
}

RETURN FRAME_SetPts(MppFrame *frame, S64 pts) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  frame->nPts = pts;

  return MPP_OK;
}

MppFrameEos FRAME_GetEos(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->bEos;
}

RETURN FRAME_SetEos(MppFrame *frame, MppFrameEos eos) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  frame->bEos = eos;

  return MPP_OK;
}

RETURN FRAME_SetWidth(MppFrame *frame, S32 width) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (width < 0) {
    error("input para width < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nWidth = width;

  return MPP_OK;
}

S32 FRAME_GetWidth(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nWidth;
}

RETURN FRAME_SetHeight(MppFrame *frame, S32 height) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (height < 0) {
    error("input para height < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nHeight = height;

  return MPP_OK;
}

S32 FRAME_GetHeight(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nHeight;
}

RETURN FRAME_SetLineStride(MppFrame *frame, S32 line_stride) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (line_stride < 0) {
    error("input para line_stride < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nLineStride = line_stride;

  return MPP_OK;
}

S32 FRAME_GetLineStride(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->nLineStride;
}

RETURN FRAME_SetPixelFormat(MppFrame *frame, S32 pixel_format) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (pixel_format < 0) {
    error("input para pixel_format < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->ePixelFormat = pixel_format;

  return MPP_OK;
}

S32 FRAME_GetPixelFormat(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->ePixelFormat;
}

S32 FRAME_GetFD(MppFrame *frame, S32 index) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (index < 0 || index >= MPP_MAX_PLANES) {
    error("input para index < 0 || index >= MPP_MAX_PLANES, please check!");
    return MPP_CHECK_FAILED;
  }

  return frame->nFd[index];
}

RETURN FRAME_SetFD(MppFrame *frame, S32 fd, S32 index) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (fd < 0) {
    error("input para fd < 0, please check!");
    return MPP_CHECK_FAILED;
  }

  if (index < 0 || index >= MPP_MAX_PLANES) {
    error("input para index < 0 || index >= MPP_MAX_PLANES, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->nFd[index] = fd;

  return MPP_OK;
}

MppFrameBufferType FRAME_GetBufferType(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return frame->eBufferType;
}

RETURN FRAME_SetBufferType(MppFrame *frame, MppFrameBufferType eBufferType) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (eBufferType < 0 || eBufferType >= MPP_FRAME_BUFFERTYPE_TOTAL_NUM) {
    error("input para eBufferType is not valid, please check!");
    return MPP_CHECK_FAILED;
  }

  frame->eBufferType = eBufferType;
}

void FRAME_Destory(MppFrame *frame) {
  if (!frame) {
    error("input para MppFrame is NULL, please check!");
    return;
  }

  if (frame->bEnableUnfreeFrameDebug) {
    num_of_unfree_frame--;
    info("---------- debug frame memory: num of unfree frame: %d",
         num_of_unfree_frame);
  }

  free(frame);
}
