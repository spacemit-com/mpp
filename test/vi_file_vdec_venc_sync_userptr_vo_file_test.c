/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-30 14:30:18
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
#include "parse.h"
#include "type.h"
#include "vdec.h"
#include "venc.h"
#include "vi.h"
#include "vo.h"

typedef struct _TestContext {
  /**
   * path of input file with stream
   */
  U8 *pInputFileName;

  /**
   * path of output file, write stream to it
   */
  U8 *pOutputFileName;

  /**
   * path of middle file, write frame to it, for debug
   */
  U8 *pMidFileName;

  FILE *pMidFile;
  FILE *pOutputFile;

  /**
   * used for save para from cmd
   */
  MppCodingType eCodingType;
  S32 eOutputPixelFormat;

  MppModuleType eViType;
  MppModuleType eVdecType;
  MppModuleType eVencType;
  MppModuleType eVoType;

  /**
   * used for decoder and encoder
   */
  MppViCtx *pViCtx;
  MppVdecCtx *pVdecCtx;
  MppVdecPara *pVdecPara;
  MppVencCtx *pVencCtx;
  MppVoCtx *pVoCtx;

  MppPacket *pInputPacket;
  MppPacket *pOutputPacket;
  MppFrame *pFrame;
  S32 nWidth;
  S32 nHeight;
  S64 nTimeStamp;
  BOOL bIsDestoryed;
  pthread_t parse_thread;
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
    case INPUT:
      sscanf(value, "%2048s", context->pInputFileName);
      debug(" get input file : %s", context->pInputFileName);
      break;
    case CODING_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodingType));
      debug(" coding type is : %s", mpp_codingtype2str(context->eCodingType));
      break;
    case MODULE_TYPE:
      sscanf(value, "%d,%d,%d,%d", (S32 *)&(context->eViType),
             (S32 *)&(context->eVdecType), (S32 *)&(context->eVencType),
             (S32 *)&(context->eVoType));
      debug(" vi type is : %s", mpp_moduletype2str(context->eViType));
      debug(" vdec type is : %s", mpp_moduletype2str(context->eVdecType));
      debug(" venc type is : %s", mpp_moduletype2str(context->eVencType));
      debug(" vo type is : %s", mpp_moduletype2str(context->eVoType));
      break;
    case SAVE_FRAME_FILE:
      sscanf(value, "%2048s", context->pOutputFileName);
      debug(" get output file: %s", context->pOutputFileName);
      sscanf(value, "%2048s", context->pMidFileName);
      strcat(context->pMidFileName, ".yuv");
      debug(" get middle file: %s", context->pMidFileName);
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
      sscanf(value, "%d", &(context->eOutputPixelFormat));
      debug(" video pixel format is : %s",
            mpp_pixelformat2str(context->eOutputPixelFormat));
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

  context->pMidFileName = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pMidFileName) {
    error("Can not malloc context->pMidFileName, please check !");
    free(context->pInputFileName);
    free(context);
    return NULL;
    ;
  }
  memset(context->pMidFileName, 0, DEMO_FILE_NAME_LEN);

  context->nTimeStamp = 0;

  return context;
}

void *do_parse(void *private_data) {
  debug("------------------new thread : do_parse-------------------");
  TestContext *context = (TestContext *)private_data;
  S32 ret = 0;
  BOOL eos = MPP_FALSE;

  while (1) {
    ret = VI_RequestOutputData(context->pViCtx,
                               PACKET_GetBaseData(context->pInputPacket));
    if (ret == MPP_CODER_EOS) eos = MPP_TRUE;

    do {
      ret = -1;
      VDEC_GetParam(context->pVdecCtx, &(context->pVdecPara));
      if (!context->pVdecPara->nInputQueueLeftNum) continue;
      ret = VDEC_Decode(context->pVdecCtx,
                        PACKET_GetBaseData(context->pInputPacket));
    } while (ret != 0);

    ret = VI_ReturnOutputData(context->pViCtx,
                              PACKET_GetBaseData(context->pInputPacket));

    if (eos) break;
  }

  debug("do_parse thread exit=============================");
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
  context->pViCtx->stViPara.ePixelFormat = context->eOutputPixelFormat;
  context->pViCtx->stViPara.eCodingType = context->eCodingType;
  context->pViCtx->stViPara.pInputFileName = context->pInputFileName;
  context->pViCtx->stViPara.bIsFrame = MPP_FALSE;

  // init vi
  ret = VI_Init(context->pViCtx);
  if (ret) {
    error("VI_init failed, please check!");
    return -1;
  }

  return 0;
}

static S32 VdecPrepare(TestContext *context) {
  S32 ret = 0;
  // create vdec channel
  context->pVdecCtx = VDEC_CreateChannel();
  if (!context->pVdecCtx) {
    error("Can not create MppVdecCtx, please check!");
    return -1;
  }

  // set vdec para
  context->pVdecCtx->stVdecPara.eCodingType = context->eCodingType;
  context->pVdecCtx->stVdecPara.nWidth = context->nWidth;
  context->pVdecCtx->stVdecPara.nHeight = context->nHeight;
  if (context->nWidth >= 3840 || context->nHeight >= 2160) {
    debug("4K video, downscale!\n");
    context->pVdecCtx->stVdecPara.nScale = 2;
  } else {
    context->pVdecCtx->stVdecPara.nScale = 1;
  }
  context->pVdecCtx->stVdecPara.eOutputPixelFormat =
      context->eOutputPixelFormat;
  context->pVdecCtx->eCodecType = context->eVdecType;
  context->pVdecCtx->stVdecPara.nHorizonScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nVerticalScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nRotateDegree = 0;
  context->pVdecCtx->stVdecPara.bThumbnailMode = 0;
  context->pVdecCtx->stVdecPara.bIsInterlaced = MPP_FALSE;
  context->pVdecCtx->stVdecPara.eFrameBufferType =
      MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL;
  context->bIsDestoryed = MPP_FALSE;

  // vdec init
  ret = VDEC_Init(context->pVdecCtx);
  if (ret) {
    error("VDEC_init failed, please check!");
    return -1;
  }

  return 0;
}

static S32 VencPrepare(TestContext *context) {
  S32 ret = 0;
  // create venc channel
  context->pVencCtx = VENC_CreateChannel();
  if (!context->pVencCtx) {
    error("Can not create MppVencCtx, please check !");
    return -1;
  }

  context->pVencCtx->stVencPara.eCodingType = context->eCodingType;
  context->pVencCtx->stVencPara.nWidth = context->nWidth;
  context->pVencCtx->stVencPara.nHeight = context->nHeight;
  context->pVencCtx->stVencPara.PixelFormat = context->eOutputPixelFormat;
  context->pVencCtx->stVencPara.eFrameBufferType =
      MPP_FRAME_BUFFERTYPE_NORMAL_EXTERNAL;
  context->pVencCtx->eCodecType = context->eVencType;

  // venc init
  ret = VENC_Init(context->pVencCtx);
  if (ret) {
    error("VENC_init failed, please check!");
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
  context->pVoCtx->stVoPara.ePixelFormat = context->eOutputPixelFormat;
  context->pVoCtx->stVoPara.bIsFrame = MPP_FALSE;
  context->pVoCtx->stVoPara.pOutputFileName = context->pOutputFileName;

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

  if (VdecPrepare(context)) {
    goto finish;
  }

  if (VencPrepare(context)) {
    goto finish;
  }

  if (VoPrepare(context)) {
    goto finish;
  }

  // mpp input packet init
  context->pInputPacket = PACKET_Create();
  if (!context->pInputPacket) {
    error("Can not malloc input MppInputPacket, please check !");
    goto finish;
  }

  PACKET_Alloc(context->pInputPacket, MPP_PACKET_MALLOC_SIZE * 2);

  // mpp output packet init
  context->pOutputPacket = PACKET_Create();
  if (!context->pOutputPacket) {
    error("Can not malloc output MppPacket, please check !");
    goto finish;
  }
  PACKET_Alloc(context->pOutputPacket, MPP_PACKET_MALLOC_SIZE * 2);

  // mpp frame init
  context->pFrame = FRAME_Create();

  context->pMidFile = fopen(context->pMidFileName, "w+");
  if (!context->pMidFile) {
    error("can not open context->pMidFileName, please check !");
    goto finish;
  }

  ret =
      pthread_create(&(context->parse_thread), NULL, do_parse, (void *)context);

  while (1) {
    if (context->pVdecCtx->stVdecPara.eDataTransmissinMode ==
        MPP_INPUT_SYNC_OUTPUT_ASYNC) {
      ret = VDEC_RequestOutputFrame(context->pVdecCtx,
                                    FRAME_GetBaseData(context->pFrame));
      if (ret == MPP_OK) {
        FRAME_SetPts(context->pFrame, context->nTimeStamp);
        context->nTimeStamp += 1000000;

        ret = VENC_SendInputFrame(context->pVencCtx,
                                  FRAME_GetBaseData(context->pFrame));
        do {
          ret = VENC_GetOutputStreamBuffer(
              context->pVencCtx, PACKET_GetBaseData(context->pOutputPacket));
          if (ret == MPP_OK) {
            VO_Process(context->pVoCtx,
                       PACKET_GetBaseData(context->pOutputPacket));
          }
        } while (ret != MPP_OK);

        S32 index = -1;
        do {
          index = VENC_ReturnInputFrame(context->pVencCtx, NULL);
          if (index >= 0) {
            MppFrame *frame = FRAME_Create();
            FRAME_SetID(frame, index);
            VDEC_ReturnOutputFrame(context->pVdecCtx, FRAME_GetBaseData(frame));
            FRAME_Destory(frame);
          }
        } while (index == -1);
      } else if (ret == MPP_CODER_EOS) {
        FRAME_SetPts(context->pFrame, context->nTimeStamp);
        FRAME_SetEos(context->pFrame, FRAME_EOS_WITHOUT_DATA);
        ret = VENC_SendInputFrame(context->pVencCtx,
                                  FRAME_GetBaseData(context->pFrame));
        do {
          ret = VENC_GetOutputStreamBuffer(
              context->pVencCtx, PACKET_GetBaseData(context->pOutputPacket));
          if (ret == MPP_OK) {
            VO_Process(context->pVoCtx,
                       PACKET_GetBaseData(context->pOutputPacket));
          }
        } while (ret != MPP_CODER_EOS);

        if (ret == MPP_CODER_EOS) {
          debug("final EOS");
          VO_Process(context->pVoCtx,
                     PACKET_GetBaseData(context->pOutputPacket));
        }

        S32 index = -1;
        do {
          index = VENC_ReturnInputFrame(context->pVencCtx, NULL);
          if (index >= 0) {
            MppFrame *frame = FRAME_Create();
            FRAME_SetID(frame, index);
            VDEC_ReturnOutputFrame(context->pVdecCtx, FRAME_GetBaseData(frame));
            FRAME_Destory(frame);
          }
        } while (index != -1);
        debug("get eos msg, go to flush!");
        goto flush;
      } else if (ret == MPP_CODER_NO_DATA) {
        // do nothing
        continue;
      } else if (ret == MPP_CODER_NULL_DATA) {
        error("null data, return");
        VDEC_ReturnOutputFrame(context->pVdecCtx,
                               FRAME_GetBaseData(context->pFrame));
        continue;
      } else if (ret == MPP_RESOLUTION_CHANGED) {
        debug("resolution changed");
        continue;
      } else if (ret == MPP_ERROR_FRAME) {
        debug("error frame");
        continue;
      } else {
        error("get something wrong(%d), go out of the main while!", ret);
        goto finish;
      }
    }
  }
flush:

finish:
  debug("Here we finish the main!");

  context->bIsDestoryed = MPP_TRUE;

  if (context->pMidFile) {
    fflush(context->pMidFile);
    fclose(context->pMidFile);
    context->pMidFile = NULL;
  }

  if (context->pFrame) {
    FRAME_Destory(context->pFrame);
    context->pFrame = NULL;
  }

  if (context->pInputPacket) {
    PACKET_Free(context->pInputPacket);
    PACKET_Destory(context->pInputPacket);
    context->pInputPacket = NULL;
  }

  if (context->pOutputPacket) {
    PACKET_Free(context->pOutputPacket);
    PACKET_Destory(context->pOutputPacket);
    context->pOutputPacket = NULL;
  }

  if (context->pViCtx) {
    VI_DestoryChannel(context->pViCtx);
    context->pViCtx = NULL;
  }

  if (context->pVdecCtx) {
    VDEC_DestoryChannel(context->pVdecCtx);
    context->pVdecCtx = NULL;
  }

  if (context->pVencCtx) {
    VENC_DestoryChannel(context->pVencCtx);
    context->pVencCtx = NULL;
  }

  if (context->pVoCtx) {
    VO_DestoryChannel(context->pVoCtx);
    context->pVoCtx = NULL;
  }

  if (context->pInputFileName) {
    free(context->pInputFileName);
    context->pInputFileName = NULL;
  }

  if (context->pOutputFileName) {
    free(context->pOutputFileName);
    context->pOutputFileName = NULL;
  }
  if (context->pMidFileName) {
    free(context->pMidFileName);
    context->pMidFileName = NULL;
  }

  if (context) {
    free(context);
    context = NULL;
  }

  return 0;
}
