/*
* Copyright 2022-2023 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @Description: MppRingBuffer - circular buffer implementation
*/

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdio.h>
#include <stdlib.h>

#include "type.h"

typedef struct _MppRingBuffer MppRingBuffer;

MppRingBuffer *RingBufferCreate(U32 size);
void RingBufferFree(MppRingBuffer *rBuf);
U32 RingBufferGetCapacity(MppRingBuffer *rBuf);
U32 RingBufferGetSize(MppRingBuffer *rBuf);
U32 RingBufferGetDataSize(MppRingBuffer *rBuf);
U32 RingBufferPush(MppRingBuffer *rBuf, void *src, U32 length);
U32 RingBufferPop(MppRingBuffer *rBuf, U32 length, void *dataOut);
void *RingBufferGetTailAddr(MppRingBuffer *rBuf);
void TestRingBuffer(void);

#endif
