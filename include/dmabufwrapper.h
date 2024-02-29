/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-10-07 14:08:18
 * @LastEditTime: 2023-10-07 16:08:51
 * @Description:
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

/***
 *    +-----------------------------+
 *    |                             |
 *    |       DmaBufWrapper         |
 *    |                             |
 *    |    +-------------------+    |
 *    |    |     DmaBuf        |    |
 *    |    |                   |    |
 *    |    +-------------------+    |
 *    |                             |
 *    +-----------------------------+
 */

/***
 *       +-----------------------+
 *       |                       |
 *       |  createDmaBufWrapper  |
 *       |                       |
 *       +----------+------------+
 *                  |
 *                  |
 *       +----------v------------+
 *       |                       |
 *       |     allocDmaBuf       |
 *       |                       |
 *       +----------+------------+
 *                  |
 *                  |
 *       +----------v------------+
 *       |                       |
 *       |      mmapDmaBuf       |
 *       |                       |
 *       +----------+------------+
 *                  |
 *                  |
 *       +----------v------------+
 *       |                       |
 *       |      freeDmaBuf       |
 *       |                       |
 *       +----------+------------+
 *                  |
 *                  |
 *       +----------v------------+
 *       |                       |
 *       |  destoryDmaBufWrapper |
 *       |                       |
 *       +-----------------------+
 */

/***
 * @description: dma heap type.
 */
typedef enum _DMAHEAP {
  /***
   * /dev/dma_heap/linux,cma
   */
  DMA_HEAP_CMA = 0,

  /***
   * /dev/dma_heap/system
   */
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

  // environment variable
  BOOL bEnableUnfreeDmaBufDebug;
} DmaBufWrapper;

/***
 * @description: create a dmabuf wrapper(a fd), used to alloc dmabuf
 * @param {DMAHEAP} heap: SYSTEM or CMA
 * @return {*}
 */
DmaBufWrapper *createDmaBufWrapper(DMAHEAP heap);

/***
 * @description: alloc a dmabuf with a specific size
 * @param {DmaBufWrapper} *context
 * @param {S32} size: size of buffer needed
 * @return {*}
 */
S32 allocDmaBuf(DmaBufWrapper *context, S32 size);

/***
 * @description: mmap a dmabuf
 * @param {DmaBufWrapper} *context
 * @return {*}
 */
void *mmapDmaBuf(DmaBufWrapper *context);

/***
 * @description: free a dmabuf, include munmap and close fd
 * @param {DmaBufWrapper} *context
 * @return {*}
 */
RETURN freeDmaBuf(DmaBufWrapper *context);

/***
 * @description: get the fd of dma wrapper
 * @param {DmaBufWrapper} *context
 * @return {*}
 */
S32 getDmaHeapFd(DmaBufWrapper *context);

/***
 * @description: destory the dmabuf wrapper
 * @param {DmaBufWrapper} *context
 * @return {*}
 */
void destoryDmaBufWrapper(DmaBufWrapper *context);

#endif /*_DMABUF_WRAPPER_H_*/