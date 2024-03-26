/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-03-26 14:27:56
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "vdec.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "argument.h"
#include "const.h"
#include "parse.h"
#include "type.h"

typedef struct _TestVdecContext {
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

  /**
   * used for demuxer
   */
  S32 nFileOffset;
  MppParseContext *pParseCtx;

  /**
   * used for save para from cmd
   */
  MppCodingType eCodingType;
  S32 eOutputPixelFormat;
  MppModuleType eCodecType;

  /**
   * used for decoder
   */
  MppVdecCtx *pVdecCtx;
  MppVdecPara *pVdecPara;
  MppPacket *pPacket;
  MppFrame *pFrame;
  S32 nWidth;
  S32 nHeight;
  S64 nTimeStamp;
  pthread_t parse_thread;
} TestVdecContext;

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

static S32 parse_argument(TestVdecContext *context, char *argument, char *value,
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

  context->nFileOffset = 0;
  context->nTimeStamp = 0;

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

  stream_data = (U8 *)malloc(MPP_PACKET_PARSE_REGION_SIZE);
  tmp_stream_data = stream_data;

  fseek(context->pInputFile, 0, SEEK_END);
  fileSize = ftell(context->pInputFile);
  rewind(context->pInputFile);
  debug("start do_parse: %d", fileSize);

  while (1) {
    stream_data = tmp_stream_data;
    stream_length = fread(stream_data, 1, MPP_PACKET_PARSE_REGION_SIZE,
                          context->pInputFile);
    debug("stream_length = %d length = %d, offset = %d", stream_length, length,
          context->nFileOffset);
    if (length == stream_length &&
        (context->nFileOffset + stream_length) == fileSize) {
      debug("It is the last data, handle it and then quit!");
      need_drain = 1;
    }

    if (0 == stream_length) {
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
      if (0 == ret || need_drain) {
        if (need_drain) {
          PACKET_SetEos(context->pPacket, MPP_TRUE);
          debug("length = %d here, ret: %d", length, ret);
        }
        stream_data += length;
        stream_length -= length;
        context->nFileOffset += length;

        PACKET_SetLength(context->pPacket, length);
        PACKET_SetPts(context->pPacket, context->nTimeStamp);
        context->nTimeStamp += 1000000;

        debug("we get a packet, length = %d, ret = %d %p %x %x %x %x", length,
              ret, PACKET_GetDataPointer(context->pPacket),
              *(S32 *)PACKET_GetDataPointer(context->pPacket),
              *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 4),
              *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 8),
              *(S32 *)(PACKET_GetDataPointer(context->pPacket) + 12));

        do {
          ret = -1;
          VDEC_GetParam(context->pVdecCtx, &(context->pVdecPara));
          if (!context->pVdecPara->nInputQueueLeftNum) continue;
          ret = VDEC_Decode(context->pVdecCtx,
                            PACKET_GetBaseData(context->pPacket));
        } while (ret != 0);

        need_drain = 0;
      } else {
        length = stream_length;
        fseek(context->pInputFile, context->nFileOffset, SEEK_SET);
        debug("fileoffset = %d", context->nFileOffset);
        break;
      }
    }
  }

  debug("do_parse thread exit=============================");
}

void save_one_plane_image(TestVdecContext *context, S32 width, S32 height,
                          S32 stride, U8 *ptr) {
  FILE *fd = context->pOutputFile;

  for (S32 i = 0; i < height; i++) {
    fwrite(ptr, width, 1, fd);
    ptr += stride;
  }

  fflush(fd);
}

S32 save_yuv_to_file(TestVdecContext *context, S32 *stride) {
  S32 height = context->nHeight;
  S32 width = context->nWidth;
  MppPixelFormat pixel = context->eOutputPixelFormat;
  S32 plane_num = 0;
  S32 ret = MPP_OK;

  switch (pixel) {
    case PIXEL_FORMAT_I420:
      if (stride != NULL) {
        save_one_plane_image(context, width, height, stride[0],
                             FRAME_GetDataPointer(context->pFrame, 0));
        save_one_plane_image(context, width / 2, height / 2, stride[1],
                             FRAME_GetDataPointer(context->pFrame, 1));
        save_one_plane_image(context, width / 2, height / 2, stride[1],
                             FRAME_GetDataPointer(context->pFrame, 2));
        return ret;
      }
      break;
    case PIXEL_FORMAT_YUV422P:
      break;
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV21:
      break;
    default:
      error("Unsupported picture format (%d)! Please check!", pixel);
      return MPP_CHECK_FAILED;
  }

  S32 y_size = width * height;
  S32 uv_size = y_size / 4;

  if (1 == FRAME_GetDataUsedNum(context->pFrame)) {
    fwrite(FRAME_GetDataPointer(context->pFrame, 0), y_size + uv_size * 2, 1,
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
  TestVdecContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;

  context = TestVdecContextCreate();
  if (!context) {
    error("can not create TestVdecContext, please check!");
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

  // create parser
  context->pParseCtx = PARSE_Create(context->eCodingType);
  if (!context->pParseCtx) {
    error("create context->pParseCtx failed, please check!");
    goto finish;
  }
  context->pParseCtx->ops->init(context->pParseCtx);

  // create vdec channel
  context->pVdecCtx = VDEC_CreateChannel();
  if (!context->pVdecCtx) {
    error("Can not create MppVdecCtx, please check!");
    goto finish;
  }

  // set para
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
  context->pVdecCtx->eCodecType = context->eCodecType;
  context->pVdecCtx->stVdecPara.nHorizonScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nVerticalScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nRotateDegree = 0;
  context->pVdecCtx->stVdecPara.bThumbnailMode = 0;
  context->pVdecCtx->stVdecPara.bIsInterlaced = MPP_FALSE;

  ret = VDEC_Init(context->pVdecCtx);
  if (ret) {
    error("VDEC_init failed, please check!");
    goto finish;
  }

  context->pPacket = PACKET_Create();
  if (!context->pPacket) {
    error("Can not malloc MppPacket, please check !");
    goto finish;
  }

  PACKET_Alloc(context->pPacket, MPP_PACKET_MALLOC_SIZE * 2);

  context->pFrame = FRAME_Create();
  if (!context->pFrame) {
    error("Can not malloc MppFrame, please check !");
    goto finish;
  }

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

  ret =
      pthread_create(&(context->parse_thread), NULL, do_parse, (void *)context);

  while (1) {
    if (context->pVdecCtx->stVdecPara.eDataTransmissinMode ==
        MPP_INPUT_SYNC_OUTPUT_ASYNC) {
      ret = VDEC_RequestOutputFrame(context->pVdecCtx,
                                    FRAME_GetBaseData(context->pFrame));
      if (ret == MPP_OK) {
        if (context->eCodecType == CODEC_OPENH264)
          save_yuv_to_file(context, (S32 *)FRAME_GetMetaData(context->pFrame));
        else
          save_yuv_to_file(context, NULL);

        VDEC_ReturnOutputFrame(context->pVdecCtx,
                               FRAME_GetBaseData(context->pFrame));
      } else if (ret == MPP_CODER_EOS) {
        if (context->eCodecType == CODEC_OPENH264)
          save_yuv_to_file(context, (S32 *)FRAME_GetMetaData(context->pFrame));
        else
          save_yuv_to_file(context, NULL);

        VDEC_ReturnOutputFrame(context->pVdecCtx,
                               FRAME_GetBaseData(context->pFrame));
        debug("get eos msg, go out of the main while!");
        goto finish;
      } else if (ret == MPP_CODER_NO_DATA) {
        error("no data, return");
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

finish:
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

  if (context->pVdecCtx) {
    VDEC_DestoryChannel(context->pVdecCtx);
    context->pVdecCtx = NULL;
  }

  if (context->pParseCtx) {
    PARSE_Destory(context->pParseCtx);
    context->pParseCtx = NULL;
  }

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
