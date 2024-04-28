/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-28 15:54:04
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "const.h"
#include "type.h"
#include "venc.h"

#define DEMO_FILE_NAME_LEN (2 * 1024)
#define FRAME_FREAD(data, size, count, fp)        \
  ({                                              \
    S32 total = (size) * (count);                 \
    S32 n = fread((data), (size), (count), (fp)); \
    if (n != total && ferror(fp)) {               \
      error("Failed to read frame from file");    \
      exit(EXIT_FAILURE);                         \
    }                                             \
    n;                                            \
  })

typedef struct _TestVencContext {
  U8 *pInputFileName;
  U8 *pOutputFileName;
  FILE *pInputFile;
  FILE *pOutputFile;
  MppVencCtx *pVencCtx;
  MppCodingType eCodingType;
  MppModuleType eCodecType;
  MppPacket *pPacket;
  MppFrame *pFrame;
  S32 nWidth;
  S32 nHeight;
  S32 ePixelFormat;
} TestVencContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input file path"},
    {"-c", "--codingtype", CODING_TYPE, "Coding type"},
    {"-m", "--moduletype", MODULE_TYPE, "Module type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
    {"-f", "--format", FORMAT, "Video PixelFormat"},
};

static S32 parse_argument(TestVencContext *context, char *argument, char *value,
                          S32 num) {
  ARGUMENT arg;
  S32 len = value == NULL ? 0 : strlen(value);
  if (len > DEMO_FILE_NAME_LEN) {
    error("value is too long, please check!");
    return -1;
  }

  if (!len && get_argument(ArgumentMapping, argument, num) != HELP) {
    error("argument need a value, please check!");
    return -1;
  }

  arg = get_argument(ArgumentMapping, argument, num);
  switch (arg) {
    case HELP:
      print_demo_usage(ArgumentMapping, num);
      print_para_enum();
      return -1;
    case INPUT:
      sscanf(value, "%2048s", context->pInputFileName);
      debug(" get input file : %s", context->pInputFileName);
      break;
    case CODING_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodingType));
      debug(" coding type is : %s", mpp_codingtype2str(context->eCodingType));
      break;
    case MODULE_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodecType));
      debug(" codec type is : %s", mpp_moduletype2str(context->eCodecType));
      break;
    case SAVE_FRAME_FILE:
      sscanf(value, "%2048s", context->pOutputFileName);
      debug(" get output file : %s", context->pOutputFileName);
      break;
    case WIDTH:
      sscanf(value, "%d", &(context->nWidth));
      debug(" video width is : %d", context->nWidth);
      break;
    case HEIGHT:
      sscanf(value, "%d", &(context->nHeight));
      debug(" video height is : %d", context->nHeight);
      break;
    case FORMAT:
      sscanf(value, "%d", &(context->ePixelFormat));
      debug(" video pixel format is : %s",
            mpp_pixelformat2str(context->ePixelFormat));
      break;
    case INVALID:
    default:
      error("Unknowed argument : %s, please check!", argument);
      return -1;
  }

  return 0;
}

S32 read_frame_from_file(TestVencContext *context) {
  S32 read_byte = 0, size[3], pnum, i;

  switch (context->ePixelFormat) {
    case PIXEL_FORMAT_I420:
      size[0] = context->nWidth * context->nHeight;
      size[1] = (context->nWidth / 2) * (context->nHeight / 2);
      size[2] = (context->nWidth / 2) * (context->nHeight / 2);
      pnum = 3;
      break;
    case PIXEL_FORMAT_YUV422P:
      size[0] = context->nWidth * context->nHeight;
      size[1] = (context->nWidth / 2) * (context->nHeight);
      size[2] = (context->nWidth / 2) * (context->nHeight);
      pnum = 3;
      break;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
      size[0] = context->nWidth * context->nHeight;
      size[1] = (context->nWidth / 2) * (context->nHeight);
      pnum = 2;
      break;
    case PIXEL_FORMAT_RGBA:
    case PIXEL_FORMAT_ARGB:
    case PIXEL_FORMAT_BGRA:
    case PIXEL_FORMAT_ABGR:
      size[0] = context->nWidth * context->nHeight * 4;
      pnum = 1;
      break;
    case PIXEL_FORMAT_YUYV:
    case PIXEL_FORMAT_UYVY:
      size[0] = context->nWidth * context->nHeight * 2;
      pnum = 1;
      break;
    default:
      error("Unsupported picture format (%d)", context->ePixelFormat);
      return -MPP_CHECK_FAILED;
  }

  for (i = 0; i < pnum; i++)
    read_byte += FRAME_FREAD(FRAME_GetDataPointer(context->pFrame, i), 1,
                             size[i], context->pInputFile);

  return read_byte;
}

static TestVencContext *TestVencContextCreate() {
  TestVencContext *context = (TestVencContext *)malloc(sizeof(TestVencContext));
  if (!context) {
    error("Can not malloc TestVencContext, please check !");
    return NULL;
  }
  memset(context, 0, sizeof(TestVencContext));

  context->pInputFileName = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pInputFileName) {
    error("Can not malloc context->pInputFileName, please check !");
    free(context);
    return NULL;
  }
  memset(context->pInputFileName, 0, DEMO_FILE_NAME_LEN);

  context->pOutputFileName = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pOutputFileName) {
    error("Can not malloc context->pOutputFileName, please check !");
    free(context->pInputFileName);
    free(context);
    return NULL;
    ;
  }
  memset(context->pOutputFileName, 0, DEMO_FILE_NAME_LEN);

  return context;
}

S32 main(S32 argc, char **argv) {
  TestVencContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;
  S32 sleep_count = 0;
  MppVencPara *venc_para = NULL;
  S32 fileSize;
  S32 leaveSize;
  S32 tmpSize;
  S32 need_drain = 0;
  MppData *tmp;
  BOOL stop = MPP_FALSE;
  BOOL eos = MPP_FALSE;
  S32 id = 0;

  context = TestVencContextCreate();
  if (!context) {
    error("can not create TestVencContext, please check!");
    return -1;
  }

  argument_num = sizeof(ArgumentMapping) / sizeof(MppArgument);

  if (argc >= 2) {
    for (S32 i = 1; i < (int)argc; i += 2) {
      ret = parse_argument(context, argv[i], argv[i + 1], argument_num);
      if (ret < 0) {
        goto finish;
      }
    }
  } else {
    error("There is no arguments, We need more arguments!");
    print_demo_usage(ArgumentMapping, argument_num);
    goto finish;
  }

  // create venc channel
  context->pVencCtx = VENC_CreateChannel();
  if (!context->pVencCtx) {
    error("Can not create venc channel, please check !");
    goto finish;
  }

  context->pVencCtx->stVencPara.eCodingType = context->eCodingType;
  context->pVencCtx->stVencPara.nWidth = context->nWidth;
  context->pVencCtx->stVencPara.nHeight = context->nHeight;
  context->pVencCtx->stVencPara.PixelFormat = context->ePixelFormat;
  context->pVencCtx->stVencPara.eFrameBufferType =
      MPP_FRAME_BUFFERTYPE_NORMAL_EXTERNAL;
  context->pVencCtx->eCodecType = context->eCodecType;

  // venc init
  VENC_Init(context->pVencCtx);

  // create mpp packet
  context->pPacket = PACKET_Create();
  PACKET_Alloc(context->pPacket, MPP_PACKET_MALLOC_SIZE * 2);

  // create mpp frame
  context->pFrame = FRAME_Create();

  FRAME_Alloc(context->pFrame, context->ePixelFormat, context->nWidth,
              context->nHeight);

  context->pInputFile = fopen(context->pInputFileName, "r");
  if (!context->pInputFile) {
    error("can not open context->pInputFile, please check !");
    goto finish;
  }

  context->pOutputFile = fopen(context->pOutputFileName, "w+");
  if (!context->pOutputFile) {
    error("can not open context->pOutputFile, please check !");
    goto finish;
  }

  fseek(context->pInputFile, 0, SEEK_END);
  fileSize = ftell(context->pInputFile);
  leaveSize = tmpSize = fileSize;
  rewind(context->pInputFile);

  while (1) {
    ret = read_frame_from_file(context);
    leaveSize = tmpSize - ret;
    tmpSize = leaveSize;

    if (ret > 0) {
      FRAME_SetID(context->pFrame, id);
      id++;
      if (id == 12) id = 0;
      ret = VENC_SendInputFrame(context->pVencCtx,
                                FRAME_GetBaseData(context->pFrame));
      do {
        ret = VENC_GetOutputStreamBuffer(context->pVencCtx,
                                         PACKET_GetBaseData(context->pPacket));
        if (ret == MPP_OK) {
          fwrite(PACKET_GetDataPointer(context->pPacket),
                 PACKET_GetLength(context->pPacket), 1, context->pOutputFile);
          fflush(context->pOutputFile);
        }
      } while (ret != MPP_OK);

      if (context->eCodingType == CODING_VP8 ||
          context->eCodingType == CODING_VP9) {
        do {
          ret = VENC_GetOutputStreamBuffer(
              context->pVencCtx, PACKET_GetBaseData(context->pPacket));
          if (ret == MPP_OK) {
            fwrite(PACKET_GetDataPointer(context->pPacket),
                   PACKET_GetLength(context->pPacket), 1, context->pOutputFile);
            fflush(context->pOutputFile);
          }
        } while (ret != MPP_OK);
      }

      S32 index = -1;
      do {
        index = VENC_ReturnInputFrame(context->pVencCtx, NULL);
        if (index >= 0) {
          // MppFrame *frame = FRAME_Create();
          // FRAME_SetID(frame, index);
          // VDEC_ReturnOutputFrame(context->pVdecCtx,
          // FRAME_GetBaseData(frame)); FRAME_Destory(frame);
          error("okkkk! a frame return!");
        }
      } while (index == -1);
    } else {
      // Continue sending empty buffers after encoding
      if (0 == leaveSize && need_drain < 4) {
        FRAME_SetEos(context->pFrame, FRAME_EOS_WITHOUT_DATA);
        FRAME_SetID(context->pFrame, id);
        id++;
        if (id == 12) id = 0;
        ret = VENC_SendInputFrame(context->pVencCtx,
                                  FRAME_GetBaseData(context->pFrame));
        do {
          ret = VENC_GetOutputStreamBuffer(
              context->pVencCtx, PACKET_GetBaseData(context->pPacket));
          if (ret == MPP_OK) {
            fwrite(PACKET_GetDataPointer(context->pPacket),
                   PACKET_GetLength(context->pPacket), 1, context->pOutputFile);
            fflush(context->pOutputFile);
          }
        } while (ret != MPP_CODER_EOS);

        if (ret == MPP_CODER_EOS) {
          debug("final EOS");
          fwrite(PACKET_GetDataPointer(context->pPacket),
                 PACKET_GetLength(context->pPacket), 1, context->pOutputFile);
          fflush(context->pOutputFile);
          stop = MPP_TRUE;
        }

        S32 index = -1;
        do {
          index = VENC_ReturnInputFrame(context->pVencCtx, NULL);
          if (index >= 0) {
            // MppFrame *frame = FRAME_Create();
            // FRAME_SetID(frame, index);
            // VDEC_ReturnOutputFrame(context->pVdecCtx,
            // FRAME_GetBaseData(frame)); FRAME_Destory(frame);
            error("okkkk! a frame return!");
          }
        } while (index != -1);
        need_drain++;

        if (stop) break;
      }
    }
    debug("test encode finish ret = %d", ret);
  }

finish:
  debug("Here we finish the main!");

  if (context->pOutputFile) {
    fflush(context->pOutputFile);
    fclose(context->pOutputFile);
    context->pOutputFile = NULL;
  }

  if (context->pInputFile) {
    fflush(context->pInputFile);
    fclose(context->pInputFile);
    context->pInputFile = NULL;
  }

  if (context->pFrame) {
    FRAME_Destory(context->pFrame);
    context->pFrame = NULL;
  }

  if (context->pPacket) {
    PACKET_Free(context->pPacket);
    PACKET_Destory(context->pPacket);
    context->pPacket = NULL;
  }

  if (context->pVencCtx) {
    VENC_DestoryChannel(context->pVencCtx);
    context->pVencCtx = NULL;
  }

  if (context->pInputFileName) {
    free(context->pInputFileName);
    context->pInputFileName = NULL;
  }

  if (context->pOutputFileName) {
    free(context->pOutputFileName);
    context->pOutputFileName = NULL;
  }

  if (venc_para) {
    free(venc_para);
    venc_para = NULL;
  }

  if (context) {
    free(context);
    context = NULL;
  }

  return 0;
}
