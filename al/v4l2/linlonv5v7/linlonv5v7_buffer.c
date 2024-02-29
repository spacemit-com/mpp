/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-10-07 14:08:38
 * @LastEditTime: 2024-01-09 14:06:31
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "linlonv5v7_buffer.h"

struct _Buffer {
  struct v4l2_buffer stBufArr;
  struct v4l2_plane stBufPlanes[VIDEO_MAX_PLANES];
  U8 *pUserPtr[8];
  U32 nMemType;
  U32 nIndex;
  struct v4l2_format format;
  struct v4l2_crop crop;
  BOOL isRoiCfg;
  struct v4l2_mvx_roi_regions roi_cfg;
  S32 nQp;
  DmaBufWrapper *pDmaBufWrapper;
  S32 nTotalLength;                    // for V4L2_MEMORY_DMABUF
  S32 nPlaneOffset[VIDEO_MAX_PLANES];  // for V4L2_MEMORY_DMABUF
  S32 nPlaneLength[VIDEO_MAX_PLANES];  // for V4L2_MEMORY_DMABUF
};

Buffer *createBuffer(struct v4l2_buffer buf, S32 fd,
                     struct v4l2_format format) {
  Buffer *buffer_tmp = (Buffer *)malloc(sizeof(Buffer));
  if (!buffer_tmp) {
    error("can not malloc Buffer, please check! (%s)", strerror(errno));
    return NULL;
  }
  memset(buffer_tmp, 0, sizeof(Buffer));

  buffer_tmp->stBufArr = buf;
  buffer_tmp->format = format;
  buffer_tmp->nMemType = buf.memory;
  buffer_tmp->nIndex = buf.index;

  memset(buffer_tmp->pUserPtr, 0, sizeof(buffer_tmp->pUserPtr));

  if (V4L2_TYPE_IS_MULTIPLANAR(buf.type)) {
    memcpy(buffer_tmp->stBufPlanes, buf.m.planes,
           sizeof(buffer_tmp->stBufPlanes[0]) * buf.length);
    buffer_tmp->stBufArr.m.planes = buffer_tmp->stBufPlanes;
  }

  buffer_tmp->isRoiCfg = MPP_FALSE;
  buffer_tmp->nQp = 0;

  if (V4L2_MEMORY_DMABUF == buffer_tmp->nMemType) {
    buffer_tmp->pDmaBufWrapper = createDmaBufWrapper(DMA_HEAP_CMA);
  }

  memoryMap(buffer_tmp, fd);

  return buffer_tmp;
}

void destoryBuffer(Buffer *buf) {
  memoryUnmap(buf);
  destoryDmaBufWrapper(buf->pDmaBufWrapper);
  debug("- sfree buffer");
  free(buf);
}

struct v4l2_buffer *getV4l2Buffer(Buffer *buf) {
  return &(buf->stBufArr);
}

U8 *getUserPtr(Buffer *buf, S32 index) { return buf->pUserPtr[index]; }

void setUserPtr(Buffer *buf, S32 index, U8 *ptr) { buf->pUserPtr[index] = ptr; }

struct v4l2_format *getFormat(Buffer *buf) {
  return &(buf->format);
}

void setCrop(Buffer *buf, struct v4l2_crop crop) { buf->crop = crop; }

struct v4l2_crop getCrop(Buffer *buf) {
  return buf->crop;
}

void setBytesUsed(Buffer *buf, S32 iov_size, S32 iov[VIDEO_MAX_PLANES]) {
  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    if (iov_size > buf->stBufArr.length) {
      error(
          "iovec vector size is larger than V4L2 buffer number of planes. "
          "size=%u, planes=%u",
          iov_size, buf->stBufArr.length);
    }

    S32 i;
    for (i = 0; i < iov_size; ++i) {
      buf->stBufArr.m.planes[i].bytesused = iov[i];
    }

    for (; i < buf->stBufArr.length; ++i) {
      buf->stBufArr.m.planes[i].bytesused = 0;
    }
  } else {
    buf->stBufArr.bytesused = 0;

    for (S32 i = 0; i < iov_size; ++i) {
      buf->stBufArr.bytesused += iov[i];

      if (buf->stBufArr.bytesused > buf->stBufArr.length) {
        error("V4L2 buffer size too small. length=%u.", buf->stBufArr.length);
      }
    }
  }
}

S32 getBytesUsed(struct v4l2_buffer *buf) {
  S32 size = 0;

  if (V4L2_TYPE_IS_MULTIPLANAR(buf->type)) {
    for (S32 i = 0; i < buf->length; ++i) {
      size += buf->m.planes[i].bytesused;
    }
  } else {
    size = buf->bytesused;
  }

  return size;
}

void clearBytesUsed(Buffer *buf) {
  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    for (S32 i = 0; i < buf->stBufArr.length; ++i) {
      buf->stBufArr.m.planes[i].bytesused = 0;
    }
  } else {
    buf->stBufArr.bytesused = 0;
  }
}

void resetVendorFlags(Buffer *buf) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_MVX_MASK;
}

void setCodecConfig(Buffer *buf, BOOL codecConfig) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_MVX_CODEC_CONFIG;
  buf->stBufArr.flags |= codecConfig ? V4L2_BUF_FLAG_MVX_CODEC_CONFIG : 0;
}

void setTimeStamp(Buffer *buf, S64 timeUs) {
  buf->stBufArr.flags |= V4L2_BUF_FLAG_TIMESTAMP_COPY;
  buf->stBufArr.timestamp.tv_sec = timeUs / 1000000;
  buf->stBufArr.timestamp.tv_usec = timeUs % 1000000;
}

void setEndOfFrame(Buffer *buf, BOOL eof) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_KEYFRAME;
  buf->stBufArr.flags |= eof ? V4L2_BUF_FLAG_KEYFRAME : 0;
}

void setEndOfSubFrame(Buffer *buf, BOOL eosf) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_END_OF_SUB_FRAME;
  buf->stBufArr.flags |= eosf ? V4L2_BUF_FLAG_END_OF_SUB_FRAME : 0;
}

S32 setRotation(Buffer *buf, S32 rotation) {
  if (rotation % 90) {
    error("input para rotation is not valid");
    return MPP_CHECK_FAILED;
  }

  switch (rotation % 360) {
    case 90:
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_ROTATION_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_ROTATION_90;
      break;
    case 180:
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_ROTATION_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_ROTATION_180;
      break;
    case 270:
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_ROTATION_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_ROTATION_270;
      break;
    default:
      break;
  }
  return MPP_OK;
}

S32 setDownScale(Buffer *buf, S32 scale) {
  if (1 == scale) {
    // debug("no need to set scale");
    return MPP_OK;
  }

  debug("need to set scale: %d", scale);
  switch (scale) {
    case 2:
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_SCALING_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_SCALING_2;
      break;
    case 4:
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_SCALING_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_SCALING_4;
      break;
    default:
      error("do not support this scale factor :%d", scale);
      break;
  }
  return MPP_OK;
}

S32 setMirror(Buffer *buf, S32 mirror) {
  if (0 == mirror) {
    // debug("no need to set mirror");
    return MPP_OK;
  } else {
    if (1 == mirror) {
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_MIRROR_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_MIRROR_HORI;
    } else if (2 == mirror) {
      buf->stBufArr.flags &= ~V4L2_BUF_FRAME_FLAG_MIRROR_MASK;
      buf->stBufArr.flags |= V4L2_BUF_FRAME_FLAG_MIRROR_VERT;
    }
  }
  return MPP_OK;
}

void setEndOfStream(Buffer *buf, BOOL eos) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_LAST;
  buf->stBufArr.flags |= eos ? V4L2_BUF_FLAG_LAST : 0;
}

void setROIflag(Buffer *buf) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_MVX_BUFFER_ROI;
  buf->stBufArr.flags |= V4L2_BUF_FLAG_MVX_BUFFER_ROI;
}

void setEPRflag(Buffer *buf) {
  buf->stBufArr.flags &= ~V4L2_BUF_FLAG_MVX_BUFFER_EPR;
  buf->stBufArr.flags |= V4L2_BUF_FLAG_MVX_BUFFER_EPR;
}

void setQPofEPR(Buffer *buf, S32 data) { buf->nQp = data; }
S32 getQPofEPR(Buffer *buf) { return buf->nQp; }

BOOL isGeneralBuffer(Buffer *buf) {
  return (buf->stBufArr.flags & V4L2_BUF_FLAG_MVX_BUFFER_EPR) ==
         V4L2_BUF_FLAG_MVX_BUFFER_EPR;
}

void update(Buffer *buf, struct v4l2_buffer b) {
  buf->stBufArr = b;

  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    buf->stBufArr.m.planes = buf->stBufPlanes;
    for (S32 i = 0; i < buf->stBufArr.length; ++i) {
      buf->stBufArr.m.planes[i] = b.m.planes[i];
    }
  }
}

void memoryMap(Buffer *buf, S32 fd) {
  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    for (S32 i = 0; i < buf->stBufArr.length; ++i) {
      struct v4l2_plane *p = &(buf->stBufArr.m.planes[i]);

      if (p->length > 0) {
        if (V4L2_MEMORY_MMAP == buf->nMemType) {
          buf->pUserPtr[i] = mmap(NULL, p->length, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fd, p->m.mem_offset);
          if (buf->pUserPtr[i] == MAP_FAILED) {
            error("Failed to mmap multi plane memory (%s)", strerror(errno));
          }
        }

        buf->nPlaneLength[i] = p->length;
        buf->nTotalLength += p->length;
      }
    }

    debug("nTotalLength = %d", buf->nTotalLength);

    if (V4L2_MEMORY_DMABUF == buf->nMemType) {
      buf->stBufArr.m.planes[0].m.fd =
          allocDmaBuf(buf->pDmaBufWrapper, buf->nTotalLength);
      buf->pUserPtr[0] = mmap(NULL, buf->nTotalLength, PROT_READ | PROT_WRITE,
                              MAP_SHARED, buf->stBufArr.m.planes[0].m.fd, 0);
      if (buf->pUserPtr[0] == MAP_FAILED) {
        error("Failed to mmap multi plane memory (%s)", strerror(errno));
      }
      buf->nPlaneOffset[0] = 0;

      for (S32 j = 1; j < buf->stBufArr.length; j++) {
        struct v4l2_plane *p = &(buf->stBufArr.m.planes[j]);
        S32 k;
        S32 offset = 0;
        for (k = j; k > 0; k--) {
          offset += buf->nPlaneLength[j - k];
        }
        buf->nPlaneOffset[j] = offset;
        if (p->length > 0) {
          buf->pUserPtr[j] = buf->pUserPtr[0] + offset;
          buf->stBufArr.m.planes[j].m.fd = buf->stBufArr.m.planes[0].m.fd;
        }
      }
    }

  } else {
    if (buf->stBufArr.length > 0) {
      if (V4L2_MEMORY_MMAP == buf->nMemType) {
        buf->pUserPtr[0] =
            mmap(NULL, buf->stBufArr.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                 fd, buf->stBufArr.m.offset);
      } else if (V4L2_MEMORY_DMABUF == buf->nMemType) {
        buf->nTotalLength = buf->stBufArr.length;
        buf->stBufArr.m.fd =
            allocDmaBuf(buf->pDmaBufWrapper, buf->stBufArr.length);
        buf->pUserPtr[0] =
            mmap(NULL, buf->stBufArr.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                 buf->stBufArr.m.fd, 0);
      }
      if (buf->pUserPtr[0] == MAP_FAILED) {
        error("Failed to mmap single plane memory (%s)", strerror(errno));
      }
    }
  }
}

S32 memoryUnmap(Buffer *buf) {
  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    /*for (S32 i = 0; i < buf->stBufArr.length; ++i) {
      if (buf->pUserPtr[i] != 0) {
        if (munmap(buf->pUserPtr[i], buf->stBufArr.m.planes[i].length)) {
          error("dmabuf munmap dma buf fail, please check!! len:%d ptr:%p (%s)",
    buf->stBufArr.m.planes[i].length, buf->pUserPtr[i], strerror(errno)); return
    MPP_MUNMAP_FAILED;
        }
      }
    }*/
    if (buf->pUserPtr[0]) {
      if (munmap(buf->pUserPtr[0], buf->nTotalLength)) {
        error("dmabuf munmap dma buf fail, please check!! len:%d ptr:%p (%s)",
              buf->nTotalLength, buf->pUserPtr[0], strerror(errno));
        return MPP_MUNMAP_FAILED;
      }
      freeDmaBuf(buf->pDmaBufWrapper);
      close(buf->stBufArr.m.planes[0].m.fd);
    }
  } else {
    if (buf->pUserPtr[0]) {
      if (munmap(buf->pUserPtr[0], buf->stBufArr.length)) {
        error("munmap dma buf fail, please check!! (%s)", strerror(errno));
        return MPP_MUNMAP_FAILED;
      }
    }
  }
}

S32 getLength(Buffer *buf, U32 plane) {
  if (V4L2_TYPE_IS_MULTIPLANAR(buf->stBufArr.type)) {
    if (buf->stBufArr.length >= plane) {
      return 0;
    }

    return buf->stBufArr.m.planes[plane].length;
  } else {
    if (plane > 0) {
      return 0;
    }

    return buf->stBufArr.length;
  }
}

void setInterlaced(Buffer *buf, BOOL interlaced) {
  buf->stBufArr.field = interlaced ? V4L2_FIELD_SEQ_TB : V4L2_FIELD_NONE;
}

void setTiled(Buffer *buf, BOOL tiled) {
  buf->stBufArr.flags &= ~(V4L2_BUF_FLAG_MVX_AFBC_TILED_HEADERS |
                           V4L2_BUF_FLAG_MVX_AFBC_TILED_BODY);
  if (tiled) {
    buf->stBufArr.flags |= V4L2_BUF_FLAG_MVX_AFBC_TILED_HEADERS;
    buf->stBufArr.flags |= V4L2_BUF_FLAG_MVX_AFBC_TILED_BODY;
  }
}

void setRoiCfg(Buffer *buf, struct v4l2_mvx_roi_regions roi) {
  buf->roi_cfg = roi;
  buf->isRoiCfg = MPP_TRUE;
}

BOOL getRoiCfgflag(Buffer *buf) { return buf->isRoiCfg; }

struct v4l2_mvx_roi_regions getRoiCfg(Buffer *buf) {
  return buf->roi_cfg;
}

void setSuperblock(Buffer *buf, BOOL superblock) {
  buf->stBufArr.flags &= ~(V4L2_BUF_FLAG_MVX_AFBC_32X8_SUPERBLOCK);
  if (superblock) {
    buf->stBufArr.flags |= V4L2_BUF_FLAG_MVX_AFBC_32X8_SUPERBLOCK;
  }
}
