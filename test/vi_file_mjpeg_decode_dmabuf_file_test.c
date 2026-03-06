/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: Simplified MJPEG decoder test using VI and VDEC modules.
 *               Read from file via VI, decode to dmabuf, save via VO with mmap
 */

#define ENABLE_DEBUG 1

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "argument.h"
#include "const.h"
#include "type.h"
#include "vdec.h"
#include "vi.h"
#include "vo.h"

typedef struct _TestContext {
  U8 *pInputFileName;
  U8 *pOutputFileName;

  MppModuleType eViType;
  MppModuleType eCodecType;

  MppViCtx *pViCtx;
  MppVdecCtx *pVdecCtx;

  MppPacket *pPacket;
  MppFrame *pFrame;
  
  S32 nWidth;
  S32 nHeight;
  S32 ePixelFormat;
  
  FILE *output_fp;
} TestContext;

static const MppArgument ArgumentMapping[] = {
    {"-H", "--help", HELP, "Print help"},
    {"-i", "--input", INPUT, "Input MJPEG file path"},
    {"-o", "--output", SAVE_FRAME_FILE, "Output YUV file path"},
    {"-w", "--width", WIDTH, "Video width"},
    {"-h", "--height", HEIGHT, "Video height"},
};

static S32 parse_argument(TestContext *context, char *argument, char *value, S32 num) {
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
  }
  memset(context->pOutputFileName, 0, DEMO_FILE_NAME_LEN);

  // 模块类型: VI_FILE 和 CODEC_V4L2_LINLONV5V7
  context->eViType = VI_FILE;      // 203
  context->eCodecType = CODEC_V4L2_LINLONV5V7;  // 9
  
  // 默认输出使用 NV12 格式
  context->ePixelFormat = PIXEL_FORMAT_NV12;

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

  // set vi para for MJPEG file input
  context->pViCtx->eViType = context->eViType;
  context->pViCtx->stViPara.nWidth = context->nWidth;
  context->pViCtx->stViPara.nHeight = context->nHeight;
  context->pViCtx->stViPara.ePixelFormat = context->ePixelFormat;
  context->pViCtx->stViPara.eCodingType = CODING_MJPEG;
  context->pViCtx->stViPara.pInputFileName = context->pInputFileName;
  context->pViCtx->stViPara.bIsFrame = MPP_FALSE;

  // init vi
  ret = VI_Init(context->pViCtx);
  if (ret) {
    error("VI_init failed, please check!");
    return -1;
  }

  debug("VI initialized successfully");
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

  // set para for MJPEG decoder
  context->pVdecCtx->stVdecPara.eCodingType = CODING_MJPEG;
  context->pVdecCtx->stVdecPara.nWidth = context->nWidth;
  context->pVdecCtx->stVdecPara.nHeight = context->nHeight;
  context->pVdecCtx->stVdecPara.nScale = 1;
  context->pVdecCtx->stVdecPara.eOutputPixelFormat = context->ePixelFormat;
  context->pVdecCtx->eCodecType = context->eCodecType;
  context->pVdecCtx->stVdecPara.nHorizonScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nVerticalScaleDownRatio = 1;
  context->pVdecCtx->stVdecPara.nRotateDegree = 0;
  context->pVdecCtx->stVdecPara.bThumbnailMode = 0;
  context->pVdecCtx->stVdecPara.bIsInterlaced = MPP_FALSE;
  context->pVdecCtx->stVdecPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;

  ret = VDEC_Init(context->pVdecCtx);
  if (ret) {
    error("VDEC_init failed, please check!");
    return -1;
  }

  debug("VDEC initialized successfully");
  return 0;
}

static S32 SaveFrameToFile(TestContext *context, MppFrame *frame) {
  if (!context->output_fp) {
    context->output_fp = fopen((char *)context->pOutputFileName, "wb");
    if (!context->output_fp) {
      error("Cannot open output file: %s", context->pOutputFileName);
      return -1;
    }
  }

  // 从 context 获取参数（因为 frame 可能还未设置这些值）
  S32 width = context->nWidth;
  S32 height = context->nHeight;
  S32 stride = width;  // 默认 stride 等于 width
  MppPixelFormat format = context->ePixelFormat;
  
  // 尝试从 frame 获取实际值（如果已设置）
  S32 frame_width = FRAME_GetWidth(frame);
  S32 frame_height = FRAME_GetHeight(frame);
  S32 frame_stride = FRAME_GetLineStride(frame);
  MppPixelFormat frame_format = FRAME_GetPixelFormat(frame);
  
  // 如果 frame 中有有效值，使用 frame 的值
  if (frame_width > 0) width = frame_width;
  if (frame_height > 0) height = frame_height;
  if (frame_stride > 0) stride = frame_stride;
  if (frame_format > 0) format = frame_format;
  
  debug("Frame info: %dx%d, stride=%d, format=%s", 
        width, height, stride, mpp_pixelformat2str(format));

  // Get dmabuf fd
  S32 fd = FRAME_GetFD(frame, 0);
  if (fd < 0) {
    error("Invalid dmabuf fd: %d", fd);
    return -1;
  }
  debug("Got dmabuf fd: %d", fd);

  // Calculate buffer size based on pixel format
  size_t buffer_size = 0;
  if (format == PIXEL_FORMAT_YV12 || format == PIXEL_FORMAT_NV12 || format == PIXEL_FORMAT_NV21) {
    buffer_size = stride * height * 3 / 2;
  } else if (format == PIXEL_FORMAT_YUV422P) {
    buffer_size = stride * height * 2;
  } else {
    // 默认按 YUV420 计算
    debug("Unknown pixel format %d, using YUV420 size calculation", format);
    buffer_size = stride * height * 3 / 2;
  }
  // mmap the dmabuf
  void *mapped_addr = mmap(NULL, buffer_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapped_addr == MAP_FAILED) {
    error("mmap failed for dmabuf fd %d, size=%zu", fd, buffer_size);
    return -1;
  }

  debug("Successfully mmap dmabuf, addr=%p, size=%zu", mapped_addr, buffer_size);

  // Write to file
  size_t written = fwrite(mapped_addr, 1, buffer_size, context->output_fp);
  if (written != buffer_size) {
    error("Write failed: wrote %zu of %zu bytes", written, buffer_size);
    munmap(mapped_addr, buffer_size);
    return -1;
  }

  debug("Wrote %zu bytes to output file", written);

  // Unmap
  munmap(mapped_addr, buffer_size);

  return 0;
}

S32 main(S32 argc, char **argv) {
  TestContext *context = NULL;
  S32 argument_num = 0;
  S32 ret = 0;
  S32 frame_count = 0;
  BOOL eos = MPP_FALSE;

  context = TestContextCreate();
  if (!context) {
    error("can not create TestContext, please check!");
    return -1;
  }

  argument_num = sizeof(ArgumentMapping) / sizeof(MppArgument);
  if (argc >= 2) {
    for (S32 i = 1; i < argc; i += 2) {
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

  // Initialize VI module
  if (ViPrepare(context)) {
    goto finish;
  }

  // Initialize VDEC module
  if (VdecPrepare(context)) {
    goto finish;
  }

  // Create packet for compressed data
  context->pPacket = PACKET_Create();
  if (!context->pPacket) {
    error("Can not malloc MppPacket, please check !");
    goto finish;
  }
  PACKET_Alloc(context->pPacket, MPP_PACKET_MALLOC_SIZE * 2);

  // Create frame for decoded data
  context->pFrame = FRAME_Create();
  if (!context->pFrame) {
    error("Can not malloc MppFrame, please check !");
    goto finish;
  }

  debug("Starting MJPEG decode loop...");

  // Main decode loop
  while (!eos) {
    // Step 1: Request packet from VI (read from file)
    ret = VI_RequestOutputData(context->pViCtx, PACKET_GetBaseData(context->pPacket));
    if (ret == MPP_CODER_EOS) {
      debug("VI reached end of file");
      eos = MPP_TRUE;
    } else if (ret != MPP_OK) {
      error("VI_RequestOutputData failed: %d", ret);
      break;
    }

    // Step 2: Decode packet to frame
    if (!eos) {
      ret = VDEC_Decode(context->pVdecCtx, PACKET_GetBaseData(context->pPacket));
      if (ret != 0) {
        error("VDEC_Decode failed: %d", ret);
        VI_ReturnOutputData(context->pViCtx, PACKET_GetBaseData(context->pPacket));
        continue;
      }
    }

    // Step 3: Return packet to VI
    VI_ReturnOutputData(context->pViCtx, PACKET_GetBaseData(context->pPacket));

    // Step 4: Request decoded frame from VDEC
    ret = VDEC_RequestOutputFrame(context->pVdecCtx, FRAME_GetBaseData(context->pFrame));
    
    if (ret == MPP_OK) {
      frame_count++;
      debug("Got frame #%d", frame_count);
      
      // Step 5: Get dmabuf fd and mmap to save to file
      if (SaveFrameToFile(context, context->pFrame) == 0) {
        debug("Frame #%d saved successfully via dmabuf mmap", frame_count);
      } else {
        error("Failed to save frame #%d", frame_count);
      }
      
      // Step 6: Return frame to VDEC
      VDEC_ReturnOutputFrame(context->pVdecCtx, FRAME_GetBaseData(context->pFrame));
      
    } else if (ret == MPP_CODER_EOS) {
      debug("VDEC reached end of stream");
      break;
    } else if (ret == MPP_CODER_NO_DATA) {
      // No data available yet, continue
      if (eos) break;
      continue;
    } else if (ret == MPP_CODER_NULL_DATA) {
      debug("null data, return");
      VDEC_ReturnOutputFrame(context->pVdecCtx, FRAME_GetBaseData(context->pFrame));
      continue;
    } else if (ret == MPP_RESOLUTION_CHANGED) {
      debug("resolution changed");
      continue;
    } else if (ret == MPP_ERROR_FRAME) {
      debug("error frame");
      continue;
    } else {
      error("VDEC_RequestOutputFrame failed: %d", ret);
      if (eos) break;
    }
  }

  debug("Decode completed. Total frames: %d", frame_count);

finish:
  // Cleanup
  if (context->output_fp) {
    fclose(context->output_fp);
    context->output_fp = NULL;
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

  if (context->pViCtx) {
    VI_DestoryChannel(context->pViCtx);
    context->pViCtx = NULL;
  }

  if (context->pVdecCtx) {
    VDEC_DestoryChannel(context->pVdecCtx);
    context->pVdecCtx = NULL;
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
