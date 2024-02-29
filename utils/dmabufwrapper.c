/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-10-07 14:08:38
 * @LastEditTime: 2024-01-12 10:25:27
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "dmabufwrapper.h"

S32 num_of_unfree_dmabuf = 0;
S32 num_of_unfree_dmabufwrapper = 0;

DmaBufWrapper *createDmaBufWrapper(DMAHEAP heap) {
  DmaBufWrapper *wrapper_tmp = (DmaBufWrapper *)malloc(sizeof(DmaBufWrapper));
  if (!wrapper_tmp) {
    error("can not malloc DmaBufWrapper, please check! (%s)", strerror(errno));
    return NULL;
  }
  memset(wrapper_tmp, 0, sizeof(DmaBufWrapper));

  U8 *dma_heap_path[64] = {"/dev/dma_heap/linux,cma",
                           //"/dev/dma_heap/linux,cma@70000000",
                           "/dev/dma_heap/system"};

  wrapper_tmp->nDmaHeapFd = open(dma_heap_path[heap], O_RDONLY | O_CLOEXEC);
  if (wrapper_tmp->nDmaHeapFd < 0) {
    error("can not open (%s), fd < 0!!! (%s)", dma_heap_path[heap],
          strerror(errno));
    free(wrapper_tmp);
    return NULL;
  }

  mpp_env_get_u32("MPP_PRINT_UNFREE_DMABUF",
                  &(wrapper_tmp->bEnableUnfreeDmaBufDebug), 0);

  if (wrapper_tmp->bEnableUnfreeDmaBufDebug) {
    num_of_unfree_dmabufwrapper++;
    info("++++++++++ debug dmabufwrapper memory: num of unfree wrapper: %d",
         num_of_unfree_dmabufwrapper);
  }

  return wrapper_tmp;
}

S32 allocDmaBuf(DmaBufWrapper *context, S32 size) {
  if (!context) {
    error("input para context is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (size <= 0) {
    error("input para size <= 0, please check!");
    return MPP_CHECK_FAILED;
  }

  if (context->sDmaBuf.nFd > 0) {
    error("fd exists, sure to alloc again?");
    return MPP_CHECK_FAILED;
  }

  struct dma_heap_allocation_data heap_data;
  memset(&heap_data, 0, sizeof(struct dma_heap_allocation_data));
  heap_data.len = size;
  heap_data.fd_flags = O_RDWR | O_CLOEXEC;
  S32 ret = 0;

  ret = ioctl(context->nDmaHeapFd, DMA_HEAP_IOCTL_ALLOC, &heap_data);
  if (ret != 0) {
    error("can not alloc dma buf, ret < 0!!! (%s)", strerror(errno));
    return MPP_IOCTL_FAILED;
  }

  debug("alloc dma buf success! fd = %d", heap_data.fd);

  if (context->bEnableUnfreeDmaBufDebug) {
    num_of_unfree_dmabuf++;
    info("++++++++++ debug dmabufwrapper memory: num of unfree dmabuf: %d",
         num_of_unfree_dmabuf);
  }

  context->sDmaBuf.nFd = heap_data.fd;
  context->sDmaBuf.nSize = size;

  return heap_data.fd;
}

void *mmapDmaBuf(DmaBufWrapper *context) {
  if (!context) {
    error("input para context is NULL, please check!");
    return NULL;
  }

  if (!context->sDmaBuf.nFd || !context->sDmaBuf.nSize) {
    error("fd = 0 or size = 0, not alloc yet, should not mmap, please check!");
    return NULL;
  }

  context->sDmaBuf.pVaddr =
      (void *)mmap(0, context->sDmaBuf.nSize, PROT_READ | PROT_WRITE,
                   MAP_SHARED, context->sDmaBuf.nFd, 0);
  if (context->sDmaBuf.pVaddr == MAP_FAILED) {
    error("can not mmap dma buf, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(context->sDmaBuf.pVaddr, 0, context->sDmaBuf.nSize);

  return context->sDmaBuf.pVaddr;
}

RETURN freeDmaBuf(DmaBufWrapper *context) {
  if (!context) {
    error("input para context is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (context->sDmaBuf.pVaddr) {
    if (munmap(context->sDmaBuf.pVaddr, context->sDmaBuf.nSize)) {
      error("munmap dma buf fail, please check!! (%s)", strerror(errno));
      return MPP_MUNMAP_FAILED;
    }
  }

  if (context->sDmaBuf.nFd > 0) {
    if (close(context->sDmaBuf.nFd)) {
      error("close dma buf fd fail, please check!!(%s)", strerror(errno));
      return MPP_CLOSE_FAILED;
    }
  }

  if (context->bEnableUnfreeDmaBufDebug) {
    num_of_unfree_dmabuf--;
    info("---------- debug dmabufwrapper memory: num of unfree dmabuf: %d",
         num_of_unfree_dmabuf);
  }

  context->sDmaBuf.nFd = 0;
  context->sDmaBuf.nSize = 0;
  context->sDmaBuf.pVaddr = NULL;

  return MPP_OK;
}

S32 getDmaHeapFd(DmaBufWrapper *context) { return context->nDmaHeapFd; }

void destoryDmaBufWrapper(DmaBufWrapper *context) {
  if (context) {
    close(context->nDmaHeapFd);
    if (context->bEnableUnfreeDmaBufDebug) {
      num_of_unfree_dmabufwrapper--;
      info("---------- debug dmabufwrapper memory: num of unfree wrapper: %d",
           num_of_unfree_dmabufwrapper);
    }
    free(context);
  }
}
