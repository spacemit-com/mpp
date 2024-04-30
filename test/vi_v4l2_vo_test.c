/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-26 11:30:30
 * @LastEditTime: 2024-04-30 15:47:35
 * @FilePath: \mpp\test\vi_v4l2_vo_test.c
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
#include "vi.h"
#include "vo.h"

#define NUM_OF_BUFFERS 12

typedef struct _TestContext {
  /**
   * path of video device
   */
  U8 *pVideoDeviceName;

  /**
   * path of output file, write frame to it
   */
  U8 *pOutputFileName;

  /**
   * used for save para from cmd
   */
  S32 ePixelFormat;

  MppModuleType eViType;
  MppModuleType eVoType;

  /**
   * used for vi
   */
  MppViCtx *pViCtx;
  MppViPara *pViPara;

  /**
   * used for vo
   */
  MppVoCtx *pVoCtx;
  MppVoPara *pVoPara;

  MppFrame *pFrame;
  S32 nWidth;
  S32 nHeight;
} TestContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input file path"},
    {"-c", "--codingtype", CODING_TYPE, "Coding type"},
    {"-m", "--moduletype", MODULE_TYPE, "Module type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
    {"-f", "--format", FORMAT, "Video PixelFormat"},
    {"-d", "--device", VIDEO_DEVICE, "Video Device Name"},
};

static S32 parse_argument(TestContext *context, char *argument, char *value,
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
    case MODULE_TYPE:
      sscanf(value, "%d,%d", (S32 *)&(context->eViType),
             (S32 *)&(context->eVoType));
      debug(" vi type is : %s", mpp_moduletype2str(context->eViType));
      debug(" vo type is : %s", mpp_moduletype2str(context->eVoType));
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

static TestContext *TestContextCreate() {
  TestContext *context = (TestContext *)malloc(sizeof(TestContext));
  if (!context) {
    error("Can not malloc TestContext, please check !");
    return NULL;
  }
  memset(context, 0, sizeof(TestContext));

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

static S32 ViPrepare(TestContext *context) {
  S32 ret = 0;
  // create vi channel
  context->pViCtx = VI_CreateChannel();
  if (!context->pViCtx) {
    error("Can not create MppViCtx, please check!");
    return -1;
  }

  // set vi para
  context->pViCtx->eViType = context->eViType;
  context->pViCtx->stViPara.nWidth = context->nWidth;
  context->pViCtx->stViPara.nHeight = context->nHeight;
  context->pViCtx->stViPara.ePixelFormat = context->ePixelFormat;
  context->pViCtx->stViPara.nBufferNum = NUM_OF_BUFFERS;
  context->pViCtx->stViPara.pVideoDeviceName = context->pVideoDeviceName;
  context->pViCtx->stViPara.bIsFrame = MPP_TRUE;

  // init vi
  ret = VI_Init(context->pViCtx);
  if (ret) {
    error("VI_init failed, please check!");
    return -1;
  }

  return 0;
}

static S32 VoPrepare(TestContext *context) {
  S32 ret = 0;
  // create vo channel
  context->pVoCtx = VO_CreateChannel();
  if (!context->pVoCtx) {
    error("Can not create MppVoCtx, please check!");
    return -1;
  }

  // set vo para
  context->pVoCtx->eVoType = context->eVoType;
  context->pVoCtx->stVoPara.nWidth = context->nWidth;
  context->pVoCtx->stVoPara.nHeight = context->nHeight;
  context->pVoCtx->stVoPara.nStride = context->nWidth;
  context->pVoCtx->stVoPara.ePixelFormat = context->ePixelFormat;
  context->pVoCtx->stVoPara.bIsFrame = MPP_TRUE;
  if (context->pVoCtx->eVoType == VO_FILE) {
    context->pVoCtx->stVoPara.pOutputFileName = context->pOutputFileName;
  }

  // init vo
  ret = VO_Init(context->pVoCtx);
  if (ret) {
    error("VO_init failed, please check!");
    return -1;
  }

  return 0;
}

S32 main(S32 argc, char **argv) {
  TestContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;

  context = TestContextCreate();
  if (!context) {
    error("can not create TestContext, please check!");
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

  if (ViPrepare(context)) {
    goto finish;
  }

  if (VoPrepare(context)) {
    goto finish;
  }

  context->pFrame = FRAME_Create();
  if (!context->pFrame) {
    error("Can not malloc MppFrame, please check !");
    goto finish;
  }

  while (1) {
    ret = VI_RequestOutputData(context->pViCtx,
                               FRAME_GetBaseData(context->pFrame));
    if (ret == MPP_OK) {
      VO_Process(context->pVoCtx, FRAME_GetBaseData(context->pFrame));

      VI_ReturnOutputData(context->pViCtx, FRAME_GetBaseData(context->pFrame));
    } else if (ret == MPP_CODER_NO_DATA) {
      error("no data, return");
      continue;
    } else {
      error("get something wrong(%d), go out of the main while!", ret);
      goto finish;
    }
  }

finish:

  if (context->pFrame) {
    FRAME_Destory(context->pFrame);
    context->pFrame = NULL;
  }

  if (context->pVoCtx) {
    VO_DestoryChannel(context->pVoCtx);
    context->pVoCtx = NULL;
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
