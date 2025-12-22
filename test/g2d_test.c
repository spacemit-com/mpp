/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2025-12-11 19:55:08
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

  MppG2dCmd eV2DCmd;
  S32 eInputPixelFormat;
  S32 eOutputPixelFormat;
  MppModuleType eVpsType;
  MppG2dCtx *pG2dCtx;
  MppG2dPara *pG2dPara;
  MppFrame *pSinkFrame;
  MppFrame *pSrcFrame;


  S32 nInputWidth;
  S32 nInputHeight;
  S32 nOutputWidth;
  S32 nOutputHeight;

  S32 nFrontRotate;
  S32 nBackRotate;


  S32 nBackRectx;
  S32 nBackRecty;
  S32 nBackRectWidth;
  S32 nBackRectHeight;

  S32 nFrontRectx;
  S32 nFrontRecty;
  S32 nFrontRectWidth;
  S32 nFrontRectHeight;

  S32 nFrontCSCMode;
  S32 nBackCSCMode;


} TestG2dContext;

// static const MppArgument ArgumentMapping[] = {
//     {"-H", "--help", HELP, "Print help"},
//     {"-i", "--input", INPUT, "Input file path"},
//     {"-c", "--codingtype", CODING_TYPE, "Coding type"},
//     {"-m", "--moduletype", MODULE_TYPE, "Module type"},
//     {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},
//     {"-w", "--width", WIDTH, "Video width"},
//     {"-h", "--height", HEIGHT, "Video height"},
//     {"-f", "--format", FORMAT, "Video PixelFormat"},
// };

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input file path"},
    {"-v", "--v2d_cmd", V2D_CMD, "V2D Cmd"},
    {"-m", "--moduletype", MODULE_TYPE, "Module type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file path"},

    //input/output width height
    {"-iw", "--input_width", INPUT_WIDTH, "Input Video width"},
    {"-ih", "--input_height", INPUT_HEIGHT, "Input Video height"},
    {"-ow", "--output width", OUTPUT_WIDTH, "Output Video PixelFormat"},
    {"-oh", "--ouput height", OUTPUT_HEIGHT, "Output Video PixelFormat"},

    //format
    {"-if", "--input format", INPUT_FORMAT, "Input Video PixelFormat"},
    {"-of", "--output format", OUTPUT_FORMAT, "Output Video PixelFormat"},

    //Rotate angle
    {"-fr", "--front rotate angle", FRONT_ROTATE_ANGLE, "Front Rotate Angle"},
    {"-br", "--Bakcground rotate angle", BACK_ROTATE_ANGLE, "Bakcground Rotate Angle"},

    //backgroud rect
    {"-brx", "--Bakcground rect x", BACK_RECT_X, "Bakcground rect x"},
    {"-bry", "--Bakcground rect y", BACK_RECT_Y, "Bakcground rect y"},
    {"-brw", "--Bakcground rect width", BACK_RECT_WIDTH, "Bakcground rect width"},
    {"-brh", "--Bakcground rect height", BACK_RECT_HEIGHT, "Bakcground rect height"},

    //front rect
    {"-frx", "--front rect x", FRONT_RECT_X, "front rect x"},
    {"-fry", "--front rect y", FRONT_RECT_Y, "front rect y"},
    {"-frw", "--front rect width", FRONT_RECT_WIDTH, "front rect width"},
    {"-frh", "--front rect height", FRONT_RECT_HEIGHT, "front rect height"},

    //csc mode
    {"-fcsc", "--front csc mode", FRONT_CSC_MODE, "front csc mode"},
    {"-bcsc", "--Bakcground csc mode", BACK_CSC_MODE, "Bakcground csc mode"},


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
    case V2D_CMD:
      sscanf(value, "%d", (S32 *)&(context->eV2DCmd));
      break;
    case MODULE_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eVpsType));
      debug(" module type is : %s", mpp_moduletype2str(context->eVpsType));
      break;
    case SAVE_FRAME_FILE:
      sscanf(value, "%2048s", context->pOutputFileName);
      debug(" get output file : %s", context->pOutputFileName);
      break;
    case INPUT_WIDTH:
      sscanf(value, "%d", &(context->nInputWidth));
      debug(" input video width is : %d", context->nWidth);
      break;
    case INPUT_HEIGHT:
      sscanf(value, "%d", &(context->nInputHeight));
      debug(" input video height is : %d", context->nHeight);
      break;
    case OUTPUT_WIDTH:
      sscanf(value, "%d", &(context->nOutputWidth));
      debug(" output video width is : %d", context->nWidth);
      break;
    case OUTPUT_HEIGHT:
      sscanf(value, "%d", &(context->nOutputHeight));
      debug(" output video height is : %d", context->nHeight);
      break;
    case INPUT_FORMAT:
      sscanf(value, "%d", &(context->eInputPixelFormat));
      debug(" input video pixel format is : %s",
            mpp_pixelformat2str(context->eOutputPixelFormat));
      break;
    case OUTPUT_FORMAT:
      sscanf(value, "%d", &(context->eOutputPixelFormat));
      debug(" output video pixel format is : %s",
            mpp_pixelformat2str(context->eOutputPixelFormat));
      break;
    case FRONT_ROTATE_ANGLE:
      sscanf(value, "%d", &(context->nFrontRotate));
      break;
    case BACK_ROTATE_ANGLE:
      sscanf(value, "%d", &(context->nBackRotate));
      break;

    case BACK_RECT_X:
      sscanf(value, "%d", &(context->nBackRectx));
      break;
    case BACK_RECT_Y:
      sscanf(value, "%d", &(context->nBackRecty));
      break;
    case BACK_RECT_WIDTH:
      sscanf(value, "%d", &(context->nBackRectWidth));
      break;
    case BACK_RECT_HEIGHT:
      sscanf(value, "%d", &(context->nBackRectHeight));
      break;

    case FRONT_RECT_X:
      sscanf(value, "%d", &(context->nFrontRectx));
      break;
    case FRONT_RECT_Y:
      sscanf(value, "%d", &(context->nFrontRecty));
      break;
    case FRONT_RECT_WIDTH:
      sscanf(value, "%d", &(context->nFrontRectWidth));
      break;
    case FRONT_RECT_HEIGHT:
      sscanf(value, "%d", &(context->nFrontRectHeight));
      break;

    case FRONT_CSC_MODE:
      sscanf(value, "%d", &(context->nFrontCSCMode));
      break;
    case BACK_CSC_MODE:
      sscanf(value, "%d", &(context->nBackCSCMode));
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

  context->eV2DCmd = MPP_G2D_CMD_BLEND;
  context->eInputPixelFormat = PIXEL_FORMAT_NV12;
  context->eOutputPixelFormat = PIXEL_FORMAT_RGB_888;
  context->eVpsType = VPS_K1_V2D;
  context->nInputWidth = 1920;
  context->nInputHeight = 1080;
  context->nOutputWidth = 1920;
  context->nOutputHeight = 1080;
  context->nFrontRotate = MPP_ROTATE_0;
  context->nBackRotate = MPP_ROTATE_0;
  context->nBackRectx = 0;
  context->nBackRecty = 0;
  context->nBackRectWidth = 1920;
  context->nBackRectHeight = 1080;
  context->nFrontRectx = 0;
  context->nFrontRecty = 0;
  context->nFrontRectWidth = 1920;
  context->nFrontRectHeight = 1080;
  context->nFrontCSCMode = MPP_G2D_MODE_BUTT;
  context->nBackCSCMode = MPP_G2D_MODE_BT709NARROW_2_RGB;

  return context;
}

static S32 G2DPrepare(TestG2dContext *context) {
  S32 ret = 0;
  // create vi channel
  context->pG2dCtx = G2D_CreateChannel();
  if (!context->pG2dCtx) {
    error("Can not create MppViCtx, please check!");
    return -1;
  }

  // set G2D para
  context->pG2dCtx->eVpsType = context->eVpsType;
  context->pG2dCtx->stG2dPara.nInputWidth = context->nInputWidth;
  context->pG2dCtx->stG2dPara.nInputHeight = context->nInputHeight;
  context->pG2dCtx->stG2dPara.nOutputWidth = context->nOutputWidth;
  context->pG2dCtx->stG2dPara.nOutputHeight = context->nOutputHeight;
  context->pG2dCtx->stG2dPara.eInputPixelFormat = context->eInputPixelFormat;
  context->pG2dCtx->stG2dPara.eOutputPixelFormat = context->eOutputPixelFormat;

  context->pG2dCtx->stG2dPara.nBackRectx = context->nBackRectx;
  context->pG2dCtx->stG2dPara.nBackRecty = context->nBackRecty;
  context->pG2dCtx->stG2dPara.nBackRectWidth = context->nBackRectWidth;
  context->pG2dCtx->stG2dPara.nBackRectHeight = context->nBackRectHeight;

  context->pG2dCtx->stG2dPara.nFrontRectx = context->nFrontRectx;
  context->pG2dCtx->stG2dPara.nFrontRecty = context->nFrontRecty;
  context->pG2dCtx->stG2dPara.nFrontRectWidth = context->nFrontRectWidth;
  context->pG2dCtx->stG2dPara.nFrontRectHeight = context->nFrontRectHeight;


  context->pG2dCtx->stG2dPara.nFrontCSCMode = context->nFrontCSCMode;
  context->pG2dCtx->stG2dPara.nBackCSCMode = context->nBackCSCMode;

  context->pG2dCtx->stG2dPara.eG2dCmd = context->eV2DCmd;

  context->pG2dCtx->stG2dPara.sRotatePara.eRotate = context->nBackRotate;

  // init G2D
  ret = G2D_Init(context->pG2dCtx);
  if (ret) {
    G2D_DestoryChannel(context->pG2dCtx);
    error("G2D_Init failed, please check!");
    return -1;
  }

  return 0;
}

S32 calculate_frame_size(int width, int height, MppPixelFormat format) {
    size_t size = 0;

    switch (format) {
        case PIXEL_FORMAT_RGB_888:
            size = (size_t)width * height * 3;
            break;
        case PIXEL_FORMAT_RGBA:
            size = (size_t)width * height * 4;
            break;
        case PIXEL_FORMAT_RGB_565:
            size = (size_t)width * height * 2;
            break;
        case PIXEL_FORMAT_NV12:
            // YUV420SP总大小为 width*height*1.5 (Y分量占width*height，UV分量共占width*height/2)
            size = (size_t)width * height * 3 / 2;
            break;
        default:
            // 未知格式默认按RGB888处理，或根据需求返回0/报错
            size = (size_t)width * height * 3 / 2;
            break;
    }

    return size;
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

  if (G2DPrepare(context)) {
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
  FRAME_Alloc(context->pSinkFrame, context->eInputPixelFormat, context->nInputWidth,
              context->nInputHeight);

  context->pSrcFrame = FRAME_Create();
  if (!context->pSrcFrame) {
    error("Can not malloc MppFrame, please check !");
    FRAME_Free(context->pSinkFrame);
    FRAME_Destory(context->pSinkFrame);
    G2D_DestoryChannel(context->pG2dCtx);
    goto finish_after;
  }

  FRAME_SetBufferType(context->pSrcFrame, MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL);
  FRAME_Alloc(context->pSrcFrame, context->eOutputPixelFormat, context->nOutputWidth,
              context->nOutputHeight);


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
           calculate_frame_size(context->nInputWidth, context->nInputHeight, context->eInputPixelFormat), context->pInputFile);

  G2D_Process(context->pG2dCtx, FRAME_GetBaseData(context->pSinkFrame),
              FRAME_GetBaseData(context->pSrcFrame));

  fwrite(FRAME_GetDataPointer(context->pSrcFrame, 0),
          calculate_frame_size(context->nOutputWidth, context->nOutputHeight, context->eOutputPixelFormat), 1, context->pOutputFile);
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
