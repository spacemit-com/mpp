/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-03-15 14:54:52
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "g2d.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "type.h"

#define DEMO_FILE_NAME_LEN (2 * 1024)

typedef struct _TestG2dContext {
  /**
   * path of input file with stream
   */
  U8 *pInputFileName;

  /**
   * path of output file, write frame to it
   */
  U8 *pOutputFileName;
  FILE *pInputFile;
  FILE *pOutputFile;

  MppCodingType eCodingType;
  S32 eInputPixelFormat;
  S32 eOutputPixelFormat;
  MppModuleType eCodecType;
  MppG2dCtx *pG2dCtx;
  MppG2dPara *pG2dPara;
  MppFrame *pSinkFrame;
  MppFrame *pSrcFrame;
  S32 nWidth;
  S32 nHeight;
} TestG2dContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input file path"},
    {"-c", "--codingtype", CODING_TYPE, "Coding type"},
    {"-ct", "--codectype", CODEC_TYPE, "Codec type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
    {"-f", "--format", FORMAT, "Video PixelFormat"},
};

static S32 parse_argument(TestG2dContext *context, char *argument, char *value,
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

static TestG2dContext *TestG2dContextCreate() {
  TestG2dContext *context = (TestG2dContext *)malloc(sizeof(TestG2dContext));
  if (!context) {
    error("Can not malloc TestG2dContext, please check !");
    return NULL;
  }
  memset(context, 0, sizeof(TestG2dContext));

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

int main(int argc, char **argv) {
  TestG2dContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;

  context = TestG2dContextCreate();
  if (!context) {
    error("can not create TestG2dContext, please check!");
    return -1;
  }

  argument_num = sizeof(ArgumentMapping) / sizeof(MppArgument);

  if (argc >= 2) {
    for (int i = 1; i < (int)argc; i += 2) {
      ret = parse_argument(context, argv[i], argv[i + 1], argument_num);
      if (ret < 0) {
        goto finish_after;
      }
    }
  } else {
    error("There is no arguments, We need more arguments !");
    print_demo_usage(ArgumentMapping, argument_num);
    free(context);
    goto finish_after;
  }

  context->pG2dCtx = G2D_CreateChannel();
  if (!context->pG2dCtx) {
    error("Can not create MppG2dCtx, please check!");
    goto finish_after;
  }

  ret = G2D_Init(context->pG2dCtx);
  if (!ret) {
    error("G2D_init failed, please check!");
    G2D_DestoryChannel(context->pG2dCtx);
    goto finish_after;
  }

  context->pSinkFrame = FRAME_Create();
  if (!context->pSinkFrame) {
    error("Can not malloc MppFrame, please check !");
    G2D_DestoryChannel(context->pG2dCtx);
    goto finish_after;
  }
  FRAME_SetBufferType(context->pSinkFrame,
                      MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL);
  FRAME_Alloc(context->pSinkFrame, context->eInputPixelFormat, context->nWidth,
              context->nHeight);

  context->pSrcFrame = FRAME_Create();
  if (!context->pSrcFrame) {
    error("Can not malloc MppFrame, please check !");
    FRAME_Free(context->pSinkFrame);
    FRAME_Destory(context->pSinkFrame);
    G2D_DestoryChannel(context->pG2dCtx);
    goto finish_after;
  }
  FRAME_SetBufferType(context->pSrcFrame, MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL);
  FRAME_Alloc(context->pSinkFrame, context->eOutputPixelFormat, context->nWidth,
              context->nHeight);

  context->pInputFile = fopen(context->pInputFileName, "r");
  if (!context->pInputFile) {
    error("can not open context->pInputFileName, please check !");
    goto finish;
  }

  context->pOutputFile = fopen(context->pOutputFileName, "w+");
  if (!context->pOutputFile) {
    error("can not open context->pOutputFileName, please check !");
    fflush(context->pInputFile);
    fclose(context->pInputFile);
    context->pInputFile = NULL;
    goto finish;
  }

  int a =
      fread(FRAME_GetDataPointer(context->pSinkFrame, 0), 1,
            context->nWidth * context->nHeight * 3 / 2, context->pInputFile);

  G2D_Process(context->pG2dCtx, FRAME_GetBaseData(context->pSinkFrame),
              FRAME_GetBaseData(context->pSrcFrame));

  fwrite(FRAME_GetDataPointer(context->pSrcFrame, 0),
         context->nWidth * context->nHeight * 3 / 2, 1, context->pOutputFile);
  fflush(context->pOutputFile);

finish_pre:
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

finish:
  FRAME_Free(context->pSinkFrame);
  FRAME_Destory(context->pSinkFrame);

  FRAME_Free(context->pSrcFrame);
  FRAME_Destory(context->pSrcFrame);

  G2D_DestoryChannel(context->pG2dCtx);

finish_after:
  if (context->pInputFileName) {
    free(context->pInputFileName);
    context->pInputFileName = NULL;
  }

  if (context->pOutputFileName) {
    free(context->pOutputFileName);
    context->pOutputFileName = NULL;
  }

  if (context) {
    free(context);
    context = NULL;
  }

  return 0;
}
