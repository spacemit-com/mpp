/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: ZRong(zhirong.li@spacemit.com)
 * @Date: 2023-11-20 14:08:38
 * @LastEditTime: 2023-12-05 10:55:42
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "ringbuffer.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
//#include <time.h>
#include <sys/time.h>

#include "log.h"

struct _MppRingBuffer {
  U32 size;                // capacity bytes size
  atomic_size_t dataSize;  // occupied data size
  U32 tailOffset;          // head offset, the oldest byte position offset
  U32 headOffset;          // tail offset, the lastest byte position offset
  void *buffer;

  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

void ringbuffer_reset(MppRingBuffer *rBuf) {
  rBuf->headOffset = -1;
  rBuf->tailOffset = -1;
  rBuf->dataSize = ATOMIC_VAR_INIT(0);
}

MppRingBuffer *RingBufferCreate(U32 size) {
  U32 totalSize = sizeof(struct _MppRingBuffer) + size;
  void *p = malloc(totalSize);
  if (!p) {
    error("malloc size: %u , fail", totalSize);
    return NULL;
  }

  MppRingBuffer *buffer = (MppRingBuffer *)p;
  buffer->buffer = p + sizeof(struct _MppRingBuffer);
  buffer->size = size;

  pthread_mutex_init(&buffer->mutex, NULL);
  pthread_cond_init(&buffer->cond, NULL);

  ringbuffer_reset(buffer);

  // create a dataqueue

  debug("succese to create a ring buffer, size is %u", size);

  return buffer;
}

void RingBufferFree(MppRingBuffer *rBuf) {
  pthread_cond_broadcast(&rBuf->cond);

  ringbuffer_reset(rBuf);
  pthread_mutex_destroy(&rBuf->mutex);
  pthread_cond_destroy(&rBuf->cond);

  rBuf->dataSize = ATOMIC_VAR_INIT(0);
  rBuf->size = 0;
  rBuf->buffer = NULL;
  free(rBuf);
}

U32 RingBufferGetCapacity(MppRingBuffer *rBuf) { return rBuf->size; }

U32 RingBufferGetSize(MppRingBuffer *rBuf) { return rBuf->size; }
void *RingBufferGetTailAddr(MppRingBuffer *rBuf) {
  return rBuf->buffer + rBuf->tailOffset;
}

U32 RingBufferGetDataSize(MppRingBuffer *rBuf) {
  return atomic_load(&rBuf->dataSize);
}

U32 RingBufferRemainSize(MppRingBuffer *rBuf) {
  return rBuf->size - atomic_load(&rBuf->dataSize);
}

U32 RingBufferPush(MppRingBuffer *rBuf, void *src, U32 length) {
  U32 writableLen = length;
  void *pSrc = src;
  U32 remainSize;
  U32 try_count = 0;

  if (writableLen > rBuf->size || length == 0) {
    error("push paras error(%u, %u), please check!", writableLen, rBuf->size);
    return -1;
  }

try_loop:
  remainSize = RingBufferRemainSize(rBuf);  // remainSize will bigger
  if (remainSize < writableLen) {
    if (try_count++ == 4) {
      error("error, tried to wait max times:%d", try_count);
      return -1;
    }

    debug("wait for space to push(%u, %u, %u)", rBuf->tailOffset,
          rBuf->headOffset, try_count);

    pthread_mutex_lock(&rBuf->mutex);
#if 0
    pthread_cond_wait(&rBuf->cond, &rBuf->mutex);
#else

    struct timespec tm;
    struct timeval now;

    gettimeofday(&now, NULL);

    tm.tv_sec = now.tv_sec + 5;
    tm.tv_nsec = now.tv_usec * 1000;

    if (pthread_cond_timedwait(&rBuf->cond, &rBuf->mutex, &tm) == ETIMEDOUT) {
      error("error, wait for space timeout(%u, %u)", remainSize, writableLen);
      pthread_mutex_unlock(&rBuf->mutex);
      return -1;
    }

#endif
    pthread_mutex_unlock(&rBuf->mutex);

    goto try_loop;
  }

  if (rBuf->tailOffset == -1) {
    remainSize = rBuf->size;
    rBuf->tailOffset = 0;
  }

  BOOL toHead = MPP_FALSE;
  if (rBuf->tailOffset + writableLen > rBuf->size) {
    debug("push data mode: toHead is true (%u, %u, %u)", rBuf->tailOffset,
          rBuf->headOffset, writableLen);
    toHead = MPP_TRUE;
  }

  if (!toHead) {
    debug("push data (%u, %u)", rBuf->tailOffset, rBuf->headOffset);
    memcpy(rBuf->buffer + rBuf->tailOffset, pSrc, writableLen);

    rBuf->tailOffset += writableLen;
    atomic_store(&rBuf->dataSize, atomic_load(&rBuf->dataSize) + writableLen);

    if (rBuf->tailOffset == rBuf->size) {
      rBuf->tailOffset = 0;
    } else if (rBuf->tailOffset > rBuf->size) {
      error("error, tailOffset cross the border(%u)", rBuf->headOffset);
      return -1;
    }

  } else {  // in case the tailOffset will be restart after adding the data

    debug("push restart data(%u, %u)", rBuf->tailOffset, rBuf->headOffset);

    // the remain size for head
    U32 toHeadSize = rBuf->size - rBuf->tailOffset;
    debug("finish push restart data000 (%u, %u %u)", rBuf->tailOffset,
          toHeadSize, rBuf->size);

    memcpy(rBuf->buffer + rBuf->tailOffset, pSrc, toHeadSize);
    debug("finish push restart data111 (%u, %u %u)", rBuf->tailOffset,
          toHeadSize, rBuf->size);

    // size of data to continue from the beginning
    U32 headSize = writableLen - toHeadSize;
    memcpy(rBuf->buffer, pSrc + toHeadSize, headSize);
    debug("finish push restart data222 (%u, %u, %u)", rBuf->tailOffset,
          toHeadSize, headSize);

    rBuf->tailOffset = headSize;
    atomic_store(&rBuf->dataSize, atomic_load(&rBuf->dataSize) + writableLen);

    debug("finish push restart data (%u, %u, %u)", rBuf->tailOffset, toHeadSize,
          headSize);
  }

  return writableLen;
}

U32 inter_ringbuffer_read(MppRingBuffer *rBuf, U32 length, void *dataOut,
                          BOOL resetHead) {
  //  if (rBuf->dataSize == 0 || length == 0) return 0;
  if (atomic_load(&rBuf->dataSize) == 0 || length == 0) return 0;

  U32 drLen = length;
  U32 dataSize = RingBufferGetDataSize(rBuf);
  U32 cSize = RingBufferGetSize(rBuf);

  if (dataSize < drLen) {
    // blocking && wake up && check && blocking again?
    // for mpp , this case is err
    error("error, no need to wait for reading data(%u, %u)", rBuf->tailOffset,
          rBuf->headOffset);
    return -1;
  }

  BOOL toHead = MPP_FALSE;
  if (rBuf->headOffset == -1) rBuf->headOffset = 0;

  if (rBuf->headOffset + drLen > rBuf->size) {
    toHead = MPP_TRUE;
  }

  if (!toHead) {
    debug("read data(%u, %u)", rBuf->tailOffset, rBuf->headOffset);
    memcpy(dataOut, rBuf->buffer + rBuf->headOffset, drLen);

    rBuf->headOffset += drLen;
    atomic_store(&rBuf->dataSize, atomic_load(&rBuf->dataSize) - drLen);

    if (rBuf->headOffset == rBuf->size) {
      rBuf->headOffset = 0;
    } else if (rBuf->headOffset > rBuf->size) {
      error("error, headOffset cross the border(%u)", rBuf->headOffset);
      return -1;
    }
  } else {  // in case the headOffset will be restart after reading the data

    debug("read restart data(%u, %u)", rBuf->tailOffset, rBuf->headOffset);
    U32 toHeadSize = cSize - rBuf->headOffset;  // the remain size for head
    memcpy(dataOut, rBuf->buffer + rBuf->headOffset, toHeadSize);

    U32 headSize =
        drLen - toHeadSize;  // size of data to continue from the beginning
    memcpy(dataOut + toHeadSize, rBuf->buffer, headSize);

    rBuf->headOffset = headSize;
    atomic_store(&rBuf->dataSize, atomic_load(&rBuf->dataSize) - drLen);

    debug("finish read restart data (%u, %u, %u)", rBuf->headOffset, toHeadSize,
          headSize);
  }
  pthread_cond_signal(&rBuf->cond);

  return drLen;
}

U32 RingBufferPop(MppRingBuffer *rBuf, U32 length, void *dataOut) {
  return inter_ringbuffer_read(rBuf, length, dataOut, MPP_TRUE);
}

U32 RingBufferRead(MppRingBuffer *rBuf, U32 length, void *dataOut) {
  // todo
  return inter_ringbuffer_read(rBuf, length, dataOut, MPP_FALSE);
}

// print Ring buffer's content into str,
void RingBufferPrint(MppRingBuffer *rBuf, BOOL hex) {
  char *b = rBuf->buffer;
  U32 cSize = RingBufferGetSize(rBuf);
  char *str = malloc(2 * cSize + 1);
  U32 dataSize = RingBufferGetDataSize(rBuf);
  char c;

  for (U32 i = 0; i < cSize; i++) {
    if (RingBufferGetDataSize(rBuf) == 0) {
      c = '_';
    } else if (rBuf->headOffset == -1 && rBuf->tailOffset != -1) {
      if (i >= rBuf->tailOffset && dataSize != cSize)
        c = '_';
      else
        c = b[i];
    } else if (rBuf->headOffset < rBuf->tailOffset) {
      if (i >= rBuf->headOffset && i < rBuf->tailOffset)
        c = b[i];
      else
        c = '_';
    } else if (rBuf->headOffset > rBuf->tailOffset) {
      if (i >= rBuf->tailOffset && i < rBuf->headOffset) {
        c = '_';
      } else
        c = b[i];
    } else {
      c = b[i];
    }

    if (hex)
      sprintf(str + i * 2, "%02X|", c);
    else
      sprintf(str + i * 2, "%c|", c);
  }

  debug("RingBuffer: %s <size %u dataSize:%u>", str, RingBufferGetSize(rBuf),
        RingBufferGetDataSize(rBuf));

  free(str);
}

void TestRingBuffer(void) {
  MppRingBuffer *cb = RingBufferCreate(8);
  char *a = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int i, j, len = 0, offset = 0, outLen = 0;
  int random_number;
  char b[128];

  srand(time(NULL));

  debug("==================START 1111 TEST===================");

  for (j = 0; j < 10; j++) {
    for (i = 0; i < 3; i++) {
      // push
      debug("=====================================");
      len = random_number = rand() % 3 + 1;
      ;
      debug("++push=> %d bytes chars:(%c - %c)", len, *(a + offset),
            *(a + offset + len - 1));
      RingBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      RingBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 3; i++) {
      // pop
      debug("=====================================");
      len = random_number = rand() % 3 + 1;
      memset(b, '\0', 128);
      outLen = (int)RingBufferPop(cb, len, b);
      debug("--pop=> %d , read: %s ", outLen, b);
      // RingBufferPrint(cb,MPP_FALSE);
    }
  }
  debug("==================FINISH 1111 TEST===================");

  debug("==================START 2222 TEST===================");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      debug("=====================================");
      len = random_number = rand() % 2 + 1;
      ;
      debug("++push=> %d bytes chars:(%c - %c)", len, *(a + offset),
            *(a + offset + len - 1));
      RingBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      RingBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      debug("=====================================");
      len = random_number = rand() % 2 + 1;
      memset(b, '\0', 128);
      outLen = (int)RingBufferPop(cb, len, b);
      debug("--pop=> %d , read: %s ", outLen, b);
      // RingBufferPrint(cb,MPP_FALSE);
    }
  }

  debug("==================FINISH 2222 TEST===================");

  debug("==================START 3333 TEST===================");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      debug("=====================================");
      len = random_number = rand() % 4 + 1;
      ;
      debug("++push=> %d bytes chars:(%c - %c)", len, *(a + offset),
            *(a + offset + len - 1));
      RingBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      RingBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      debug("=====================================");
      len = random_number = rand() % 4 + 1;
      memset(b, '\0', 128);
      outLen = (int)RingBufferPop(cb, len, b);
      debug("--pop=> %d , read: %s ", outLen, b);
      RingBufferPrint(cb, MPP_FALSE);
    }
  }

  debug("==================FINISH 3333 TEST===================");

  debug("==================START 4444 TEST===================");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      debug("=====================================");
      len = random_number = rand() % 7 + 1;
      ;
      debug("++push=> %d bytes chars:(%c - %c)", len, *(a + offset),
            *(a + offset + len - 1));
      RingBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      RingBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      debug("=====================================");
      len = random_number = rand() % 7 + 1;
      memset(b, '\0', 128);
      outLen = (int)RingBufferPop(cb, len, b);
      debug("--pop=> %d , read: %s ", outLen, b);
      RingBufferPrint(cb, MPP_FALSE);
    }
  }

  debug("==================FINISH 4444 TEST===================");
}
