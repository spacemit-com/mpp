/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: ZRong(zirong.li@spacemit.com)
 * @Date: 2023-10-07 14:08:18
 * @LastEditTime: 2023-10-07 16:08:51
 * @Description:
 */

#ifndef _MPP_CIRCULARBUFFER_H_
#define _MPP_CIRCULARBUFFER_H_
#include <stdio.h>
#include <stdlib.h>

#include "type.h"

/*
 * A circular buffer(circular queue, cyclic buffer or ring buffer), is a data
 * structure that uses a single, fixed-size buffer as if it were connected
 * end-to-end. This structure lends itself easily to buffering data streams.
 * visit https://en.wikipedia.org/wiki/Circular_buffer to see more information.
 */
typedef struct s_circularBuffer *CircularBuffer;

// Construct CircularBuffer with ‘size' in byte. You must call
// CircularBufferFree() in balance for destruction.
CircularBuffer CircularBufferCreate(U32 size);

// Destruct CircularBuffer
void CircularBufferFree(CircularBuffer cBuf);

// get the capacity of CircularBuffer
U32 CircularBufferGetCapacity(CircularBuffer cBuf);

// same as CircularBufferGetCapacity, Just for compatibility with older versions
U32 CircularBufferGetSize(CircularBuffer cBuf);

// get occupied data size of CircularBuffer
U32 CircularBufferGetDataSize(CircularBuffer cBuf);

// Push data to the tail of a circular buffer from 'src' with 'length' size in
// byte.
U32 CircularBufferPush(CircularBuffer cBuf, void *src, U32 length);
U32 CircularBufferPop(CircularBuffer cBuf, U32 length, void *dataOut);
void *CircularBufferGetTailAddr(CircularBuffer cBuf);

void TestCircularBuffer(void);

#endif /*_MPP_CIRCULARBUFFER_H_*/
