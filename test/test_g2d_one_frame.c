/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2023-11-14 11:16:54
 * @Description:
 */

#define ENABLE_DEBUG 0

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "g2d.h"
#include "type.h"

#define DEMO_FILE_NAME_LEN (2 * 1024)

typedef struct _TestG2dContext {
  U8 *pInputFile;
  U8 *pOutputFile;
} TestG2dContext;

static const MppArgument ArgumentMapping[] = {
    {"-h", "--help", HELP, "Print this help"},
    {"-i", "--input", INPUT, "Input file"},
    {"-n", "--decode_frame_num", DECODE_FRAME_NUM, "Decode n frames and stop"},
    {"-ss", "--save_frame_start", SAVE_FRAME_START,
     "After display ss frames, saving pictures begin"},
    {"-sn", "--save_frame_num", SAVE_FRAME_NUM,
     "After sn frames saved, stop saving picture"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file"},
    {"-cn", "--cost_dram_thread_num", COST_DRAM_THREAD_NUM,
     "create cn threads to cost dram, or disturb cpu, test decoder robust"},
};

void ParseArgument(TestG2dContext *context, char *argument, char *value,
                   int num) {
  ARGUMENT arg;
  int len = value == NULL ? 0 : strlen(value);
  if (len > DEMO_FILE_NAME_LEN) {
    error("value is too long, fix it !");
    return;
  }
  arg = get_argument(ArgumentMapping, argument, num);
  switch (arg) {
    case HELP:
      print_demo_usage(ArgumentMapping, num);
      exit(-1);
    case INPUT:
      sscanf(value, "%2048s", context->pInputFile);
      debug(" get input file: %s", context->pInputFile);
      break;
    case DECODE_FRAME_NUM:
      // sscanf(value, "%d", &Decoder->nFinishNum);
      break;
    case SAVE_FRAME_START:
      // sscanf(value, "%d", &Decoder->nSavePictureStartNumber);
      break;
    case SAVE_FRAME_NUM:
      // sscanf(value, "%d", &Decoder->nSavePictureNumber);
      break;
    case COST_DRAM_THREAD_NUM:
      // sscanf(value, "%d", &Decoder->nDramCostThreadNum);
      break;
    case SAVE_FRAME_FILE:
      sscanf(value, "%2048s", context->pOutputFile);
      debug(" get output file: %s", context->pOutputFile);
      break;
    case INVALID:
    default:
      error("Unknowed argument :  %s", argument);
      break;
  }
}

int main(int argc, char **argv) {
  TestG2dContext *context = NULL;
  int argument_num = 0;

  // MppData *sink_data = NULL;
  // MppData *src_data = NULL;

  MppFrame *sink_frame = NULL;
  MppFrame *src_frame = NULL;

  MppG2dPara *g2d_para = NULL;

  FILE *fp = NULL;
  FILE *fp1 = NULL;

  context = (TestG2dContext *)malloc(sizeof(TestG2dContext));
  if (!context) {
    error("Can not malloc TestG2dContext, please check !");
    // goto finish;
  }
  memset(context, 0, sizeof(TestG2dContext));

  context->pInputFile = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pInputFile) {
    error("Can not malloc context->pInputFile, please check !");
    // goto finish;
  }
  memset(context->pInputFile, 0, DEMO_FILE_NAME_LEN);

  context->pOutputFile = (U8 *)malloc(DEMO_FILE_NAME_LEN);
  if (!context->pOutputFile) {
    error("Can not malloc context->pOutputFile, please check !");
    // goto finish;
  }
  memset(context->pOutputFile, 0, DEMO_FILE_NAME_LEN);

  argument_num = sizeof(ArgumentMapping) / sizeof(MppArgument);

  if (argc >= 2) {
    for (int i = 1; i < (int)argc; i += 2) {
      ParseArgument(context, argv[i], argv[i + 1], argument_num);
    }
  } else {
    error("There is no arguments, We need more arguments !");
    print_demo_usage(ArgumentMapping, argument_num);
    return 0;
  }

  MppG2dCtx *ctx = G2D_CreateChannel();

  G2D_Init(ctx);

  sink_frame = FRAME_Create();
  FRAME_Alloc(sink_frame, 3, 1280, 720);

  src_frame = FRAME_Create();
  FRAME_Alloc(src_frame, 3, 640, 360);

  fp = fopen(context->pInputFile, "r");
  if (!fp) {
    error("can not open context->pInputFile, please check !");
  }

  fp1 = fopen(context->pOutputFile, "w+");
  if (!fp1) {
    error("can not open context->pOutputFile, please check !");
  }

  int a = fread(FRAME_GetDataPointer(sink_frame, 0), 1, 1280 * 720, fp);
  int b = fread(FRAME_GetDataPointer(sink_frame, 1), 1, 1280 * 720 / 4, fp);
  int c = fread(FRAME_GetDataPointer(sink_frame, 2), 1, 1280 * 720 / 4, fp);
  // sink_packet->nLength = 19633;

  G2D_Convert(ctx, FRAME_GetBaseData(sink_frame));

  // src_frame->pData0 = (unsigned char*)malloc(1280*720);
  // src_frame->pData1 = (unsigned char*)malloc(1280*720/4);
  // src_frame->pData2 = (unsigned char*)malloc(1280*720/4);
  // src_packet->nLength = 120000;
  // src_packet->pData = (unsigned char*)malloc(120000);

  G2D_RequestOutputFrame(ctx, FRAME_GetBaseData(src_frame));

  fwrite(FRAME_GetDataPointer(src_frame, 0), 640 * 360, 1, fp1);
  fwrite(FRAME_GetDataPointer(src_frame, 1), 640 * 360 / 4, 1, fp1);
  fwrite(FRAME_GetDataPointer(src_frame, 2), 640 * 360 / 4, 1, fp1);
  fflush(fp1);

  if (fp1) {
    fflush(fp1);
    fclose(fp1);
    fp1 = NULL;
  }

  if (fp) {
    // fflush(fp);
    fclose(fp);
    fp = NULL;
  }

  FRAME_Destory(sink_frame);
  FRAME_Destory(src_frame);
}
