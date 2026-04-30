/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: DmaBufWrapper - DMA buffer allocation and management
 */

#ifndef _DMABUF_WRAPPER_H_
#define _DMABUF_WRAPPER_H_

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "env.h"
#include "log.h"
#include "type.h"
#include "v4l2_utils.h"

typedef enum _DMAHEAP {
  DMA_HEAP_CMA = 0,
  DMA_HEAP_SYSTEM = 1,
} DMAHEAP;

typedef struct _DmaBuf {
  S32 nFd;
  S32 nSize;
  void *pVaddr;
} DmaBuf;

typedef struct _DmaBufWrapper {
  S32 nDmaHeapFd;
  DmaBuf sDmaBuf;
  U32 bEnableUnfreeDmaBufDebug;
} DmaBufWrapper;

DmaBufWrapper *createDmaBufWrapper(DMAHEAP heap);
S32 allocDmaBuf(DmaBufWrapper *context, S32 size);
void *mmapDmaBuf(DmaBufWrapper *context);
RETURN freeDmaBuf(DmaBufWrapper *context);
S32 getDmaHeapFd(DmaBufWrapper *context);
void destoryDmaBufWrapper(DmaBufWrapper *context);

#endif /*_DMABUF_WRAPPER_H_*/
