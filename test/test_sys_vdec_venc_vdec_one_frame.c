/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2023-11-09 14:36:20
 * @Description:
 */

#define ENABLE_DEBUG 0

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "sys.h"
#include "type.h"

#define DEMO_FILE_NAME_LEN (2 * 1024)

typedef struct _TestSysContext {
  U8 *pInputFile;
  U8 *pOutputFile;
} TestSysContext;

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

void ParseArgument(TestSysContext *context, char *argument, char *value,
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
  TestSysContext *context = NULL;
  int argument_num = 0;

  // MppData *sink_data = NULL;
  // MppData *src_data = NULL;

  MppPacket *sink_packet = NULL;
  MppFrame *src_frame = NULL;

  MppVencPara *venc_para = NULL;

  FILE *fp = NULL;
  FILE *fp1 = NULL;

  context = (TestSysContext *)malloc(sizeof(TestSysContext));
  if (!context) {
    error("Can not malloc TestSysContext, please check !");
    // goto finish;
  }
  memset(context, 0, sizeof(TestSysContext));

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

  MppProcessFlowCtx *ctx = SYS_CreateFlow();
  SYS_Init(ctx);

  MppProcessNode *vdec_node = SYS_CreateNode(VDEC);

  MppProcessNode *venc_node = SYS_CreateNode(VENC);
  MppProcessNode *vdec_node_1 = SYS_CreateNode(VDEC);

  SYS_Bind(ctx, vdec_node, venc_node);
  SYS_Bind(ctx, venc_node, vdec_node_1);

  venc_para = (MppVencPara *)malloc(sizeof(MppVencPara));
  memset(venc_para, 0, sizeof(MppVencPara));
  venc_para->nBitrate = 5000000;
  venc_para->nFrameRate = 30;
  venc_para->nHeight = 720;
  venc_para->nWidth = 1280;
  venc_para->nStride = 1280;
  VENC_SetParam((MppVencCtx *)venc_node, venc_para);

  sink_packet = PACKET_Create();
  PACKET_Alloc(sink_packet, 120000);

  fp = fopen(context->pInputFile, "r");
  if (!fp) {
    error("can not open context->pInputFile, please check !");
  }

  fp1 = fopen(context->pOutputFile, "w+");
  if (!fp1) {
    error("can not open context->pOutputFile, please check !");
  }

  int a = fread(PACKET_GetDataPointer(sink_packet), 1, 120000, fp);
  PACKET_SetLength(sink_packet, 19633);

  SYS_Handledata(ctx, PACKET_GetBaseData(sink_packet));

  src_frame = FRAME_Create();
  FRAME_Alloc(src_frame, 3, 1280, 720);

  SYS_Getresult(ctx, FRAME_GetBaseData(src_frame));

  fwrite(FRAME_GetDataPointer(src_frame, 0), 1280 * 720, 1, fp1);
  fwrite(FRAME_GetDataPointer(src_frame, 1), 1280 * 720 / 4, 1, fp1);
  fwrite(FRAME_GetDataPointer(src_frame, 2), 1280 * 720 / 4, 1, fp1);
  fflush(fp1);

  if (fp1) {
    fflush(fp1);
    fclose(fp1);
    fp1 = NULL;
  }

  if (fp) {
    fflush(fp);
    fclose(fp);
    fp = NULL;
  }

  FRAME_Destory(src_frame);
  PACKET_Destory(sink_packet);
}
