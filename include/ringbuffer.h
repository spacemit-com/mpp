/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: ZRong(zhirong.li@spacemit.com)
 * @Date: 2023-10-07 14:08:18
 * @LastEditTime: 2023-10-07 16:08:51
 * @Description:
 */

#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_
#include <stdio.h>
#include <stdlib.h>

#include "type.h"

/*
 * A Ring buffer(Ring queue, cyclic buffer or ring buffer), is a data
 * structure that uses a single, fixed-size buffer as if it were connected
 * end-to-end. This structure lends itself easily to buffering data streams.
 * visit https://en.wikipedia.org/wiki/Circular_buffer to see more information.
 */
typedef struct _MppRingBuffer MppRingBuffer;

// Construct RingBuffer with ‘size' in byte. You must call
// RingBufferFree() in balance for destruction.
MppRingBuffer *RingBufferCreate(U32 size);

// Destruct RingBuffer
void RingBufferFree(MppRingBuffer *rBuf);

// get the capacity of RingBuffer
U32 RingBufferGetCapacity(MppRingBuffer *rBuf);

// same as RingBufferGetCapacity, Just for compatibility with older versions
U32 RingBufferGetSize(MppRingBuffer *rBuf);

// get occupied data size of RingBuffer
U32 RingBufferGetDataSize(MppRingBuffer *rBuf);

// Push data to the tail of a Ring buffer from 'src' with 'length' size in
// byte.
U32 RingBufferPush(MppRingBuffer *rBuf, void *src, U32 length);
U32 RingBufferPop(MppRingBuffer *rBuf, U32 length, void *dataOut);
void *RingBufferGetTailAddr(MppRingBuffer *rBuf);

void TestRingBuffer(void);

#endif
