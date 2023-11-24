/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-11-20 14:08:38
 * @LastEditTime: 2023-11-20 14:08:38
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "circularbuffer.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
//#include <time.h>
#include <sys/time.h>
#include "log.h"

struct s_circularBuffer {
  U32 size;                // capacity bytes size
  atomic_size_t dataSize;  // occupied data size
  U32 tailOffset;          // head offset, the oldest byte position offset
  U32 headOffset;          // tail offset, the lastest byte position offset
  void *buffer;

  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

void CircularBufferReset(CircularBuffer cBuf) {
  cBuf->headOffset = -1;
  cBuf->tailOffset = -1;
  cBuf->dataSize = ATOMIC_VAR_INIT(0);
}

CircularBuffer CircularBufferCreate(U32 size) {
  U32 totalSize = sizeof(struct s_circularBuffer) + size;
  void *p = malloc(totalSize);
  if (!p) {
    error("malloc size: %u , fail\n", totalSize);
    return NULL;
  }

  CircularBuffer buffer = (CircularBuffer)p;
  buffer->buffer = p + sizeof(struct s_circularBuffer);
  buffer->size = size;

  pthread_mutex_init(&buffer->mutex, NULL);
  pthread_cond_init(&buffer->cond, NULL);

  CircularBufferReset(buffer);
  debug("succese to create a circular buffer, size is %u", size);

  return buffer;
}

void CircularBufferFree(CircularBuffer cBuf) {
  pthread_cond_broadcast(&cBuf->cond);

  CircularBufferReset(cBuf);
  pthread_mutex_destroy(&cBuf->mutex);
  pthread_cond_destroy(&cBuf->cond);

  cBuf->dataSize = ATOMIC_VAR_INIT(0);
  cBuf->size = 0;
  cBuf->buffer = NULL;
  free(cBuf);
}

U32 CircularBufferGetCapacity(CircularBuffer cBuf) { return cBuf->size; }

U32 CircularBufferGetSize(CircularBuffer cBuf) { return cBuf->size; }
void *CircularBufferGetTailAddr(CircularBuffer cBuf) {
  return cBuf->buffer + cBuf->tailOffset;
}

U32 CircularBufferGetDataSize(CircularBuffer cBuf) {
  return atomic_load(&cBuf->dataSize);
}

U32 CircularBufferRemainSize(CircularBuffer cBuf) {
  return cBuf->size - atomic_load(&cBuf->dataSize);
}

U32 CircularBufferPush(CircularBuffer cBuf, void *src, U32 length) {
  U32 writableLen = length;
  void *pSrc = src;
  U32 remainSize;
  U32 try_count = 0;

  if (writableLen > cBuf->size || length == 0) {
    error("push paras error(%u, %u), please check!\n", writableLen, cBuf->size);
    return -1;
  }

try_loop:
  remainSize = CircularBufferRemainSize(cBuf);  // remainSize will bigger
  if (remainSize < writableLen) {
    if (try_count++ == 4) {
      error("error, tried to wait max times:%d\n", try_count);
      return -1;
    }

    debug("wait for space to push(%u, %u, %u)\n",
          cBuf->tailOffset, cBuf->headOffset, try_count);

    pthread_mutex_lock(&cBuf->mutex);

    pthread_cond_wait(&cBuf->cond, &cBuf->mutex);
/*
    struct timespec tm;
    struct timeval now;

    gettimeofday(&now, NULL);

    tm.tv_sec = now.tv_sec + 5;
    tm.tv_nsec = now.tv_usec * 1000;

    if (pthread_cond_timedwait(&cBuf->cond, &cBuf->mutex, &tm) == ETIMEDOUT) {
      error("error, wait for space timeout(%u, %u)\n", remainSize, writableLen);
      pthread_mutex_unlock(&cBuf->mutex);
      return -1;
    }
*/
    pthread_mutex_unlock(&cBuf->mutex);

    goto try_loop;
  }


  if (cBuf->tailOffset == -1) {
    remainSize = cBuf->size;
    cBuf->tailOffset = 0;
  }

  BOOL toHead = MPP_FALSE;
  if (cBuf->tailOffset + writableLen > cBuf->size) {
    // debug("push data mode: toHead is true (%u, %u)\n",  cBuf->tailOffset,
    // cBuf->headOffset);
    toHead = MPP_TRUE;
  }

  if (!toHead) {
    // debug("push data (%u, %u)\n",  cBuf->tailOffset,
    // cBuf->headOffset);
    memcpy(cBuf->buffer + cBuf->tailOffset, pSrc, writableLen);

    cBuf->tailOffset += writableLen;
    atomic_store(&cBuf->dataSize, atomic_load(&cBuf->dataSize) + writableLen);

    if (cBuf->tailOffset == cBuf->size) {
      cBuf->tailOffset = 0;
    } else if (cBuf->tailOffset > cBuf->size) {
      error("error, tailOffset cross the border(%u)\n", cBuf->headOffset);
      return -1;
    }

  } else  // in case the tailOffset will be restart after adding the data
  {
    // debug("push restart data(%u, %u)\n",
    // cBuf->tailOffset, cBuf->headOffset);
    U32 toHeadSize = cBuf->size - cBuf->tailOffset;  // the remain size for head
    memcpy(cBuf->buffer + cBuf->tailOffset, pSrc, toHeadSize);

    U32 headSize = writableLen -
                   toHeadSize;  // size of data to continue from the beginning
    memcpy(cBuf->buffer, pSrc + toHeadSize, headSize);

    cBuf->tailOffset = headSize;
    atomic_store(&cBuf->dataSize, atomic_load(&cBuf->dataSize) + writableLen);

    debug("finish push restart data (%u, %u, %u)\n",
          cBuf->tailOffset, toHeadSize, headSize);
  }

  return writableLen;
}

U32 inter_circularBuffer_read(CircularBuffer cBuf, U32 length, void *dataOut,
                              BOOL resetHead) {
  if (cBuf->dataSize == 0 || length == 0) return 0;

  if (atomic_load(&cBuf->dataSize) == 0 || length == 0) return 0;

  U32 drLen = length;
  U32 dataSize = CircularBufferGetDataSize(cBuf);
  U32 cSize = CircularBufferGetSize(cBuf);

  if (dataSize < drLen) {
    // blocking && wake up && check && blocking again?
    // for mpp , this case is err
    error("error, no need to wait for reading data(%u, %u)\n",
          cBuf->tailOffset, cBuf->headOffset);
    return -1;
  }

  BOOL toHead = MPP_FALSE;
  if (cBuf->headOffset == -1) cBuf->headOffset = 0;

  if (cBuf->headOffset + drLen > cBuf->size) {
    toHead = MPP_TRUE;
  }

  if (!toHead) {
    // debug("read data(%u, %u)\n",  cBuf->tailOffset,
    // cBuf->headOffset);
    memcpy(dataOut, cBuf->buffer + cBuf->headOffset, drLen);

    cBuf->headOffset += drLen;
    atomic_store(&cBuf->dataSize, atomic_load(&cBuf->dataSize) - drLen);

    if (cBuf->headOffset == cBuf->size) {
      cBuf->headOffset = 0;
    } else if (cBuf->headOffset > cBuf->size) {
      error("error, headOffset cross the border(%u)\n", cBuf->headOffset);
      return -1;
    }
  } else  // in case the headOffset will be restart after reading the data
  {
    // debug("read restart data(%u, %u)\n",
    // cBuf->tailOffset, cBuf->headOffset);
    U32 toHeadSize = cSize - cBuf->headOffset;  // the remain size for head
    memcpy(dataOut, cBuf->buffer + cBuf->headOffset, toHeadSize);

    U32 headSize =
        drLen - toHeadSize;  // size of data to continue from the beginning
    memcpy(dataOut + toHeadSize, cBuf->buffer, headSize);

    cBuf->headOffset = headSize;
    atomic_store(&cBuf->dataSize, atomic_load(&cBuf->dataSize) - drLen);

    debug("finish read restart data (%u, %u, %u)\n",
           cBuf->headOffset, toHeadSize, headSize);
  }
  pthread_cond_signal(&cBuf->cond);

  return drLen;
}

U32 CircularBufferPop(CircularBuffer cBuf, U32 length, void *dataOut) {
  return inter_circularBuffer_read(cBuf, length, dataOut, MPP_TRUE);
}

U32 CircularBufferRead(CircularBuffer cBuf, U32 length, void *dataOut) {
  // todo
  return inter_circularBuffer_read(cBuf, length, dataOut, MPP_FALSE);
}

// print circular buffer's content into str,
void CircularBufferPrint(CircularBuffer cBuf, BOOL hex) {
  char *b = cBuf->buffer;
  U32 cSize = CircularBufferGetSize(cBuf);
  char *str = malloc(2 * cSize + 1);
  U32 dataSize = CircularBufferGetDataSize(cBuf);
  char c;

  for (U32 i = 0; i < cSize; i++) {
    if (CircularBufferGetDataSize(cBuf) == 0) {
      c = '_';
    } else if (cBuf->headOffset == -1 && cBuf->tailOffset != -1) {
      if (i >= cBuf->tailOffset && dataSize != cSize)
        c = '_';
      else
        c = b[i];
    } else if (cBuf->headOffset < cBuf->tailOffset) {
//      debug("zrong ------------ jjjjj (%u, %u, %u)\n",
//            cBuf->headOffset, cBuf->tailOffset, i);
      if (i >= cBuf->headOffset && i < cBuf->tailOffset)
        c = b[i];
      else
        c = '_';
    } else if (cBuf->headOffset > cBuf->tailOffset) {
      if (i >= cBuf->tailOffset && i < cBuf->headOffset) {
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

  debug("CircularBuffer: %s <size %u dataSize:%u>\n", str,
        CircularBufferGetSize(cBuf), CircularBufferGetDataSize(cBuf));

  free(str);
}

void TestCircularBuffer(void) {
  CircularBuffer cb = CircularBufferCreate(8);
  char *a = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int i, j, len = 0, offset = 0, outLen = 0;
  int random_number;
  char b[128];

  srand(time(NULL));

  printf("\n==================START 1111 TEST===================\n");

  for (j = 0; j < 10; j++) {
    for (i = 0; i < 3; i++) {
      // push
      printf("\n=====================================\n");
      len = random_number = rand() % 3 + 1;
      ;
      printf("++push=> %d bytes chars:(%c - %c)\n", len, *(a + offset),
             *(a + offset + len - 1));
      CircularBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      CircularBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 3; i++) {
      // pop
      printf("\n=====================================\n");
      len = random_number = rand() % 3 + 1;
      memset(b, '\0', 128);
      outLen = (int)CircularBufferPop(cb, len, b);
      printf("--pop=> %d , read: %s \n", outLen, b);
      // CircularBufferPrint(cb,MPP_FALSE);
    }
  }
  printf("\n==================FINISH 1111 TEST===================\n");

  printf("\n==================START 2222 TEST===================\n");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      printf("\n=====================================\n");
      len = random_number = rand() % 2 + 1;
      ;
      printf("++push=> %d bytes chars:(%c - %c)\n", len, *(a + offset),
             *(a + offset + len - 1));
      CircularBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      CircularBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      printf("\n=====================================\n");
      len = random_number = rand() % 2 + 1;
      memset(b, '\0', 128);
      outLen = (int)CircularBufferPop(cb, len, b);
      printf("--pop=> %d , read: %s \n", outLen, b);
      // CircularBufferPrint(cb,MPP_FALSE);
    }
  }

  printf("\n==================FINISH 2222 TEST===================\n");

  printf("\n==================START 3333 TEST===================\n");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      printf("\n=====================================\n");
      len = random_number = rand() % 4 + 1;
      ;
      printf("++push=> %d bytes chars:(%c - %c)\n", len, *(a + offset),
             *(a + offset + len - 1));
      CircularBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      CircularBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      printf("\n=====================================\n");
      len = random_number = rand() % 4 + 1;
      memset(b, '\0', 128);
      outLen = (int)CircularBufferPop(cb, len, b);
      printf("--pop=> %d , read: %s \n", outLen, b);
      CircularBufferPrint(cb, MPP_FALSE);
    }
  }

  printf("\n==================FINISH 3333 TEST===================\n");

  printf("\n==================START 4444 TEST===================\n");

  for (j = 0; j < 30; j++) {
    for (i = 0; i < 1; i++) {
      // push
      printf("\n=====================================\n");
      len = random_number = rand() % 7 + 1;
      ;
      printf("++push=> %d bytes chars:(%c - %c)\n", len, *(a + offset),
             *(a + offset + len - 1));
      CircularBufferPush(cb, a + offset, len);
      offset += len;
      if (strlen(a) < offset + 3) offset = 0;
      CircularBufferPrint(cb, MPP_FALSE);
    }
    for (i = 0; i < 1; i++) {
      // pop
      printf("\n=====================================\n");
      len = random_number = rand() % 7 + 1;
      memset(b, '\0', 128);
      outLen = (int)CircularBufferPop(cb, len, b);
      printf("--pop=> %d , read: %s \n", outLen, b);
      CircularBufferPrint(cb, MPP_FALSE);
    }
  }

  printf("\n==================FINISH 4444 TEST===================\n");
}
