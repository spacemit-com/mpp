/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-26 11:30:30
 * @LastEditTime: 2024-04-26 17:22:28
 * @FilePath: \mpp\test\vi_test.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "vi.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "const.h"
#include "parse.h"
#include "type.h"

#define NUM_OF_BUFFERS 12

typedef struct _TestViContext {
  /**
   * path of video device
   */
  U8 *pVideoDeviceName;

  /**
   * path of output file, write frame to it
   */
  U8 *pOutputFileName;

  FILE *pOutputFile;

  /**
   * used for save para from cmd
   */
  S32 ePixelFormat;
  MppModuleType eCodecType;

  /**
   * used for decoder
   */
  MppViCtx *pViCtx;
  MppViPara *pViPara;
  MppFrame *pFrame;
  S32 nWidth;
  S32 nHeight;
} TestViContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input file path"},
    {"-c", "--codingtype", CODING_TYPE, "Coding type"},
    {"-ct", "--codectype", CODEC_TYPE, "Codec type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
    {"-f", "--format", FORMAT, "Video PixelFormat"},
    {"-d", "--device", VIDEO_DEVICE, "Video Device Name"},
};

static S32 parse_argument(TestViContext *context, char *argument, char *value,
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
    case CODEC_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodecType));
      debug(" codec type is : %s", mpp_codectype2str(context->eCodecType));
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
    case VIDEO_DEVICE:
      sscanf(value, "%2048s", context->pVideoDeviceName);
      debug(" get video device name : %s", context->pVideoDeviceName);
      break;
    case INVALID:
    default:
      error("Unknowed argument : %s, please check!", argument);
      return -1;
  }

  return 0;
}

static TestViContext *TestViContextCreate() {
  TestViContext *context = (TestViContext *)malloc(sizeof(TestViContext));
  if (!context) {
    error("Can not malloc TestViContext, please check !");
    return NULL;
  }
  memset(context, 0, sizeof(TestViContext));

  context->pVideoDeviceName = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pVideoDeviceName) {
    error("Can not malloc context->pVideoDeviceName, please check !");
    free(context);
    return NULL;
    ;
  }
  memset(context->pVideoDeviceName, 0, DEMO_FILE_NAME_LEN);

  context->pOutputFileName = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pOutputFileName) {
    error("Can not malloc context->pOutputFileName, please check !");
    free(context);
    return NULL;
    ;
  }
  memset(context->pOutputFileName, 0, DEMO_FILE_NAME_LEN);

  return context;
}

S32 save_yuv_to_file(TestViContext *context, S32 *stride) {
  S32 height = context->nHeight;
  S32 width = context->nWidth;
  MppPixelFormat pixel = context->ePixelFormat;
  S32 plane_num = 0;
  S32 ret = MPP_OK;

  switch (pixel) {
    case PIXEL_FORMAT_I420:
      break;
    case PIXEL_FORMAT_YUV422P:
      break;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
      break;
    case PIXEL_FORMAT_YUYV:
    case PIXEL_FORMAT_YVYU:
      break;
    default:
      error("Unsupported picture format (%d)! Please check!", pixel);
      return MPP_CHECK_FAILED;
  }

  S32 y_size = width * height;
  S32 uv_size = y_size / 4;

  if (1 == FRAME_GetDataUsedNum(context->pFrame)) {
    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size + uv_size * 4, 1,
           context->pOutputFile);
  } else if (2 == FRAME_GetDataUsedNum(context->pFrame)) {
    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size, 1,
           context->pOutputFile);
    fwrite(FRAME_GetDataPointer(context->pFrame, 1), uv_size * 2, 1,
           context->pOutputFile);
  } else {
    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size, 1,
           context->pOutputFile);
    fwrite(FRAME_GetDataPointer(context->pFrame, 1), uv_size, 1,
           context->pOutputFile);
    fwrite(FRAME_GetDataPointer(context->pFrame, 2), uv_size, 1,
           context->pOutputFile);
  }
  fflush(context->pOutputFile);

  return ret;
}

S32 main(S32 argc, char **argv) {
  TestViContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;

  context = TestViContextCreate();
  if (!context) {
    error("can not create TestViContext, please check!");
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

  // create vdec channel
  context->pViCtx = VI_CreateChannel();
  if (!context->pViCtx) {
    error("Can not create MppViCtx, please check!");
    goto finish;
  }

  // set para
  context->pViCtx->stViPara.nWidth = context->nWidth;
  context->pViCtx->stViPara.nHeight = context->nHeight;
  context->pViCtx->stViPara.ePixelFormat = context->ePixelFormat;
  context->pViCtx->stViPara.nBufferNum = NUM_OF_BUFFERS;
  context->pViCtx->eViType = context->eCodecType;
  memcpy(context->pViCtx->stViPara.pVideoDeviceName, context->pVideoDeviceName,
         strlen(context->pVideoDeviceName));

  ret = VI_Init(context->pViCtx);
  if (ret) {
    error("VI_init failed, please check!");
    goto finish;
  }

  context->pFrame = FRAME_Create();
  if (!context->pFrame) {
    error("Can not malloc MppFrame, please check !");
    goto finish;
  }

  context->pOutputFile = fopen(context->pOutputFileName, "w+");
  if (!context->pOutputFile) {
    error("can not open context->pOutputFileName, please check !");
    goto finish;
  }

  while (1) {
    ret = VI_RequestOutputFrame(context->pViCtx,
                                FRAME_GetBaseData(context->pFrame));
    if (ret == MPP_OK) {
      save_yuv_to_file(context, NULL);

      VI_ReturnOutputFrame(context->pViCtx, FRAME_GetBaseData(context->pFrame));
    } else if (ret == MPP_CODER_NO_DATA) {
      error("no data, return");
      continue;
    } else {
      error("get something wrong(%d), go out of the main while!", ret);
      goto finish;
    }
  }

finish:
  if (context->pOutputFile) {
    fflush(context->pOutputFile);
    fclose(context->pOutputFile);
    context->pOutputFile = NULL;
  }

  if (context->pFrame) {
    FRAME_Destory(context->pFrame);
    context->pFrame = NULL;
  }

  if (context->pViCtx) {
    VI_DestoryChannel(context->pViCtx);
    context->pViCtx = NULL;
  }

  if (context->pOutputFileName) {
    free(context->pOutputFileName);
    context->pOutputFileName = NULL;
  }

  if (context->pVideoDeviceName) {
    free(context->pVideoDeviceName);
    context->pVideoDeviceName = NULL;
  }

  if (context) {
    free(context);
    context = NULL;
  }

  return 0;
}
