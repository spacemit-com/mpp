/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2023-11-14 10:19:25
 * @Description:
 */

#define ENABLE_DEBUG 0

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

typedef struct _FrameInfo {
  U32 nOffset;
  S32 nLength;
} FrameInfo;

typedef struct _TestVdecContext {
  U8 *pInputFileName;
  U8 *pOutputFileName;
  U8 *pMidFileName;
  FILE *pInputFile;
  FILE *pMidFile;
  FILE *pOutputFile;
  S32 nFileOffset;
  S32 nFrameNum;
  FrameInfo *pInfo;
  MppParseContext *pParseCtx;
  MppCodingType eCodingType;
  MppCodecType eCodecType;
  MppVdecCtx *pVdecCtx;
  MppVencCtx *pVencCtx;
  MppPacket *pPacket;
  MppPacket *pOutputPacket;
  MppFrame *pInputFrame;
  MppFrame *pOutputFrame;
  S32 nWidth;
  S32 nHeight;
  S32 eOutputPixelFormat;
  S64 nTimeStamp;
  BOOL bIsDestoryed;
} TestVdecContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print this help"},
    {"-i", "--input", INPUT, "Input file"},
    {"-c", "--codingtype", CODING_TYPE, "Coding type"},
    {"-ct", "--codectype", CODEC_TYPE, "Codec type"},
    {"-o", "--save_frame_file", SAVE_FRAME_FILE, "Saving picture file"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
    {"-f", "--format", FORMAT, "Video yuv formatt"},
};

static void parse_argument(TestVdecContext *context, char *argument,
                           char *value, S32 num) {
  ARGUMENT arg;
  S32 len = value == NULL ? 0 : strlen(value);
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
      sscanf(value, "%2048s", context->pInputFileName);
      debug(" get input file: %s", context->pInputFileName);
      break;
    case CODING_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodingType));
      debug(" coding type is : %d", context->eCodingType);
      break;
    case CODEC_TYPE:
      sscanf(value, "%d", (S32 *)&(context->eCodecType));
      debug(" codec type is : %d", context->eCodecType);
      break;
    case SAVE_FRAME_FILE:
      sscanf(value, "%2048s", context->pOutputFileName);
      debug(" get output file: %s", context->pOutputFileName);
      sscanf(value, "%2048s", context->pMidFileName);
      strcat(context->pMidFileName, ".yuv");
      debug(" get output file: %s", context->pMidFileName);
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
      debug(" video yuv format is : %d", context->eOutputPixelFormat);
      break;
    case INVALID:
    default:
      error("Unknowed argument :  %s", argument);
      break;
  }
}

static TestVdecContext *TestVdecContextCreate() {
  TestVdecContext *context = (TestVdecContext *)malloc(sizeof(TestVdecContext));
  if (!context) {
    error("Can not malloc TestVdecContext, please check !");
    return NULL;
  }
  memset(context, 0, sizeof(TestVdecContext));

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

  context->nFileOffset = 0;
  context->nFrameNum = 0;
  context->pInfo = (FrameInfo *)malloc(sizeof(FrameInfo) * MAX_FRAME_NUM);
  if (!context->pInfo) {
    error("Can not malloc context->pInfo, please check !");
    free(context->pOutputFileName);
    free(context->pInputFileName);
    free(context);
    return NULL;
  }
  memset(context->pInfo, 0, sizeof(FrameInfo) * MAX_FRAME_NUM);

  return context;
}

void *do_parse(void *private_data) {
  debug("------------------new thread-------------------");
  TestVdecContext *context = (TestVdecContext *)private_data;
  S32 ret = 0;
  U8 *stream_data = NULL;
  U8 *tmp_stream_data = NULL;
  S32 stream_length = 0;
  S32 need_drain = 0;
  S32 length = 0;
  S32 fileSize;
  S32 region_size;

  if (context->nHeight * context->nWidth <= 1920 * 1080)
    region_size = MPP_PACKET_MALLOC_SIZE;
  else
    region_size = MPP_PACKET_MALLOC_SIZE * 2;

  stream_data = (U8 *)malloc(region_size);
  tmp_stream_data = stream_data;

  fseek(context->pInputFile, 0, SEEK_END);
  fileSize = ftell(context->pInputFile);
  rewind(context->pInputFile);
  debug("start do_parse: %d", fileSize);

  while (1) {
    stream_data = tmp_stream_data;
    stream_length = fread(stream_data, 1, region_size, context->pInputFile);
    debug("stream_length = %d length = %d, offset = %d", stream_length, length,
          context->nFileOffset);
    if (length == stream_length &&
        (context->nFileOffset + stream_length) == fileSize) {
      debug("It is the last data, handle it and then quit!");
      need_drain = 1;
    }

    if (stream_length == 0) {
#if 0

      S32 flaggg = 1;
      while (flaggg > 0) {
        PACKET_SetEos(context->pPacket, MPP_TRUE);
        PACKET_SetLength(context->pPacket, 0);

        ret = VDEC_Decode(context->pVdecCtx,
                          PACKET_GetBaseData(context->pPacket));
        debug("last decode");
        flaggg--;
      }
#endif
      debug("There is no data, quit!");
      if (stream_data) {
        free(stream_data);
        stream_data = NULL;
      }
      break;
    }

    while (1) {
      ret = context->pParseCtx->ops->parse(
          context->pParseCtx, (U8 *)stream_data, stream_length,
          (U8 *)PACKET_GetDataPointer(context->pPacket), &length, 0,
          need_drain);
      if (ret == 0 || need_drain) {
        if (need_drain) {
          PACKET_SetEos(context->pPacket, MPP_TRUE);
          debug("length = %d here, ret: %d", length, ret);
        }
        stream_data += length;
        stream_length -= length;

        context->pInfo[context->nFrameNum].nLength = length;
        context->pInfo[context->nFrameNum].nOffset = context->nFileOffset;
        context->nFrameNum++;
        context->nFileOffset += length;
        PACKET_SetLength(context->pPacket, length);
        PACKET_SetPts(context->pPacket, context->nTimeStamp);
        context->nTimeStamp += 1000000;
        /*debug("we get a packet, length = %d, ret = %d %p %x %x %x %x",
               length, ret, PACKET_GetDataPointer(context->pPacket),
               *(S32 *)PACKET_GetDataPointer(context->pPacket),
               *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 4),
               *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 8),
               *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 12));*/

        // start decode
        ret = -1;

#if 0
                MppData * tmp = PACKET_GetBaseData(context->pPacket);
                tmp->bEos = need_drain;
#endif

        while (ret != 0) {
          ret = VDEC_Decode(context->pVdecCtx,
                            PACKET_GetBaseData(context->pPacket));
        }
        // get decode frame

        need_drain = 0;
      } else {
        length = stream_length;
        fseek(context->pInputFile, context->nFileOffset, SEEK_SET);
        debug("fileoffset = %d", context->nFileOffset);

        break;

        // stream_length = fread(stream_data, 1, MPP_PACKET_MALLOC_SIZE,
        // pInputFile);
      }
    }
#if 0
        debug("start to sleep");
        for(S32 ppp=0;ppp<100;ppp++)
        {
            usleep(30000);
            usleep(30000);
            usleep(30000);
            usleep(30000);
        }
        debug("finish to sleep");
#endif
  }

  debug("do_parse thread exit=============================");
}

void *do_decode(void *private_data) {
  TestVdecContext *context = (TestVdecContext *)private_data;
  S32 ret = 0;

  while (1) {
    if (context->bIsDestoryed) pthread_exit(NULL);
    ret = VENC_RequestOutputStreamBuffer(
        context->pVencCtx, PACKET_GetBaseData(context->pOutputPacket));
    if (ret == MPP_OK) {
      debug(
          "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX %d",
          PACKET_GetLength(context->pOutputPacket));
      fwrite(PACKET_GetDataPointer(context->pOutputPacket),
             PACKET_GetLength(context->pOutputPacket), 1, context->pOutputFile);
    } else {
      error("mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm ret = %d", ret);
      usleep(50000);
    }
  }
}

S32 main(S32 argc, char **argv) {
  TestVdecContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;
  pthread_t parse_thread;
  pthread_t decode_thread;

  context = TestVdecContextCreate();
  if (!context) {
    error("can not create TestVdecContext, please check!");
    goto finish;
  }

  argument_num = sizeof(ArgumentMapping) / sizeof(MppArgument);
  if (argc >= 2) {
    for (S32 i = 1; i < (int)argc; i += 2) {
      parse_argument(context, argv[i], argv[i + 1], argument_num);
    }
  } else {
    error("There is no arguments, We need more arguments!");
    print_demo_usage(ArgumentMapping, argument_num);
    goto finish;
  }

  // create parser
  debug("coding type is : %d", context->eCodingType);
  context->pParseCtx = PARSE_Create(context->eCodingType);
  context->pParseCtx->ops->init(context->pParseCtx);

  // create vdec channel
  context->pVdecCtx = VDEC_CreateChannel();
  if (!context->pVdecCtx) {
    error("Can not create MppVdecCtx, please check !");
    goto finish;
  }

  // set para
  context->pVdecCtx->stVdecPara.eCodingType = context->eCodingType;  // 264
  context->pVdecCtx->stVdecPara.nWidth = context->nWidth;
  context->pVdecCtx->stVdecPara.nHeight = context->nHeight;
  context->pVdecCtx->stVdecPara.nScale = 1;
  context->pVdecCtx->stVdecPara.eOutputPixelFormat =
      context->eOutputPixelFormat;  // NV21
  context->pVdecCtx->eCodecType = context->eCodecType;
  context->nTimeStamp = 0;

  if (context->nWidth * context->nHeight >= 1920 * 1080) {
    context->pVdecCtx->stVdecPara.bInputBlockModeEnable = MPP_TRUE;
  }

  context->bIsDestoryed = MPP_FALSE;

  // vdec init
  VDEC_Init(context->pVdecCtx);

  // create vdec channel
  context->pVencCtx = VENC_CreateChannel();
  if (!context->pVencCtx) {
    error("Can not create MppVencCtx, please check !");
    goto finish;
  }

  context->pVencCtx->stVencPara.eCodingType = context->eCodingType;  // 264
  context->pVencCtx->stVencPara.nWidth = context->nWidth;
  context->pVencCtx->stVencPara.nHeight = context->nHeight;
  context->pVencCtx->stVencPara.PixelFormat = context->eOutputPixelFormat;
  context->pVencCtx->eCodecType = context->eCodecType;  // NV21

  // venc init
  VENC_Init(context->pVencCtx);

  // mpp packet init
  context->pPacket = PACKET_Create();
  if (!context->pPacket) {
    error("Can not malloc MppPacket, please check !");
    goto finish;
  }
  if (context->nWidth * context->nHeight > 1920 * 1080)
    PACKET_Alloc(context->pPacket, MPP_PACKET_MALLOC_SIZE * 2);
  else
    PACKET_Alloc(context->pPacket, MPP_PACKET_MALLOC_SIZE);

  // mpp packet init
  context->pOutputPacket = PACKET_Create();
  if (!context->pPacket) {
    error("Can not malloc MppPacket, please check !");
    goto finish;
  }
  if (context->nWidth * context->nHeight > 1920 * 1080)
    PACKET_Alloc(context->pOutputPacket, MPP_PACKET_MALLOC_SIZE * 2);
  else
    PACKET_Alloc(context->pOutputPacket, MPP_PACKET_MALLOC_SIZE);

  // mpp frame init
  context->pInputFrame = FRAME_Create();
  // mpp frame init
  context->pOutputFrame = FRAME_Create();

  context->pInputFile = fopen(context->pInputFileName, "r");
  if (!context->pInputFile) {
    error("can not open context->pInputFileName, please check !");
    goto finish;
  }

  context->pOutputFile = fopen(context->pOutputFileName, "w+");
  if (!context->pOutputFile) {
    error("can not open context->pOutputFileName, please check !");
    goto finish;
  }

  context->pMidFile = fopen(context->pMidFileName, "w+");
  if (!context->pMidFile) {
    error("can not open context->pMidFileName, please check !");
    goto finish;
  }

  ret = pthread_create(&parse_thread, NULL, do_parse, (void *)context);
  ret = pthread_create(&decode_thread, NULL, do_decode, (void *)context);

  // usleep(200000);

  S32 count111 = 0;
  while (1) {
    // get decode frame

    ret = VDEC_RequestOutputFrame(context->pVdecCtx,
                                  FRAME_GetBaseData(context->pInputFrame));
    S32 y_size = 1280 * 720;  // context->nWidth * context->nHeight;
    S32 uv_size = y_size / 4;
    if (context->pVdecCtx->stVdecPara.nScale == 2) y_size /= 4;

    // save decode frame to file
    if (ret == MPP_OK || ret == MPP_CODER_EOS) {
      /*            if (FRAME_GetDataUsedNum(context->pFrame) == 1) {
                    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size +
         uv_size
               * 2, 1, context->pOutputFile); } else if
               (FRAME_GetDataUsedNum(context->pFrame) == 2) {
                    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size, 1,
                           context->pOutputFile);
                    fwrite(FRAME_GetDataPointer(context->pFrame, 1), uv_size *
         2, 1,p context->pOutputFile); } else {
                    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size, 1,
                           context->pOutputFile);
                    fwrite(FRAME_GetDataPointer(context->pFrame, 1), uv_size, 1,
                           context->pOutputFile);
                    fwrite(FRAME_GetDataPointer(context->pFrame, 2), uv_size, 1,
                           context->pOutputFile);
                  }
                  fflush(context->pOutputFile);
      */
      debug("OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO %d %d %d %p %p %d",
            FRAME_GetFD(context->pInputFrame, 0),
            FRAME_GetFD(context->pInputFrame, 1),
            FRAME_GetFD(context->pInputFrame, 2),
            FRAME_GetDataPointer(context->pInputFrame, 0),
            FRAME_GetDataPointer(context->pInputFrame, 1), y_size);
      debug(
          "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA frame "
          "decoder->encoder");

      // fwrite(FRAME_GetDataPointer(context->pInputFrame, 0), y_size, 1,
      //        context->pMidFile);
      // fwrite(FRAME_GetDataPointer(context->pInputFrame, 1), uv_size * 2, 1,
      //        context->pMidFile);
      // fflush(context->pMidFile);

      VENC_SendInputFrame(context->pVencCtx,
                          FRAME_GetBaseData(context->pInputFrame));

      // fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size, 1,
      //        context->pOutputFile);
      // fwrite(FRAME_GetDataPointer(context->pFrame, 1), uv_size * 2, 1,
      //        context->pOutputFile);
      //  fwrite(FRAME_GetDataPointer(context->pFrame, 2), uv_size, 1,
      //          context->pOutputFile);
      // fflush(context->pOutputFile);

      // VDEC_ReturnOutputFrame(context->pVdecCtx,
      //                        FRAME_GetBaseData(context->pFrame));
      if (ret == MPP_CODER_EOS) {
        debug("get eos msg, go out of the main while!");
        // goto finish;
        //#if 0
        usleep(20000000);
        goto finish;
        //#endif
      }
    } else if (ret == MPP_CODER_NO_DATA || ret == MPP_RESOLUTION_CHANGED) {
      continue;
    } else {
      error("get something wronggg(%d), go out of the main while!", ret);
      goto finish;
    }

    ret = VENC_ReturnInputFrame(context->pVencCtx,
                                FRAME_GetBaseData(context->pOutputFrame));
    if (ret == MPP_OK) {
      debug(
          "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB frame "
          "encoder->decoder");
      VDEC_ReturnOutputFrame(context->pVdecCtx,
                             FRAME_GetBaseData(context->pOutputFrame));
    } else {
      error("mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm ret = %d", ret);
      usleep(50000);
      continue;
    }
  }

finish:
  debug("Here we finish the main!");

  context->bIsDestoryed = MPP_TRUE;

  pthread_join(decode_thread, NULL);

  if (context->pOutputFile) {
    fflush(context->pOutputFile);
    fclose(context->pOutputFile);
    context->pOutputFile = NULL;
  }
  if (context->pMidFile) {
    fflush(context->pMidFile);
    fclose(context->pMidFile);
    context->pMidFile = NULL;
  }

  if (context->pInputFile) {
    fflush(context->pInputFile);
    fclose(context->pInputFile);
    context->pInputFile = NULL;
  }

  FRAME_Destory(context->pInputFrame);
  FRAME_Destory(context->pOutputFrame);

  PACKET_Destory(context->pPacket);
  debug("Here we finish the main 4!");
  VDEC_DestoryChannel(context->pVdecCtx);
  VENC_DestoryChannel(context->pVencCtx);
  debug("Here we finish the main 5!");
  PARSE_Destory(context->pParseCtx);

  if (context->pInfo) {
    free(context->pInfo);
    context->pInfo = NULL;
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
