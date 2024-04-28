/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-31 09:15:38
 * @LastEditTime: 2024-04-28 16:21:30
 * @Description:
 */

#ifndef _MPP_PARA_H_
#define _MPP_PARA_H_

#include "type.h"

/***
 * @description: all codec mpp support
 */

/*

+-----------------------+---------+---------+-----------+
|                       | DECODER | ENCODER | CONVERTER |
+=======================+=========+=========+===========+
| CODEC_OPENH264        | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_FFMPEG          | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_SFDEC           | √       | x       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_SFENC           | x       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_CODADEC         | √(jpeg) | x       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_SFOMX           | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_V4L2            | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_FAKEDEC         | √       | x       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_V4L2_LINLONV5V7 | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| CODEC_K1_JPU          | √       | √       | x         |
+-----------------------+---------+---------+-----------+
| VO_SDL2               | x       | x       | x         |
+-----------------------+---------+---------+-----------+
| VO_FILE               | x       | x       | x         |
+-----------------------+---------+---------+-----------+
| VI_V4L2               | x       | x       | x         |
+-----------------------+---------+---------+-----------+
| VI_K1_CAM             | x       | x       | x         |
+-----------------------+---------+---------+-----------+
| VPS_K1_V2D            | x       | x       | √         |
+-----------------------+---------+---------+-----------+

*/

typedef enum _MppModuleType {
  /***
   * auto mode, mpp select suitable codec.
   */
  CODEC_AUTO = 0,

  /***
   * use openh264 soft codec api, support decoder and encoder
   */
  CODEC_OPENH264,

  /***
   * use ffmpeg avcodec api, support decoder and encoder.
   */
  CODEC_FFMPEG,

  /***
   * use starfive wave511 vpu api for video decoder.
   */
  CODEC_SFDEC,

  /***
   * use starfive wave420l vpu api for video encoder.
   */
  CODEC_SFENC,

  /***
   * use starfive codaj12 vpu api for jpeg video decoder and encoder.
   */
  CODEC_CODADEC,

  /***
   * use starfive omx-il api for video decoder and encoder.
   */
  CODEC_SFOMX,

  /***
   * use V4L2 standard codec interface for video decoder and encoder.
   */
  CODEC_V4L2,

  /***
   * a fake decoder for test, send green frame to application layer.
   */
  CODEC_FAKEDEC,

  /***
   * use ARM LINLON VPU codec interface for video decoder and encoder.(K1)
   */
  CODEC_V4L2_LINLONV5V7,

  /***
   * use jpu for jpeg decoder and encoder (K1).
   */
  CODEC_K1_JPU,

  CODEC_MAX,

  /***
   * auto mode, mpp select suitable vo.
   */
  VO_AUTO = 100,

  /***
   * use sdl2 for output
   */
  VO_SDL2,
  VO_FILE,

  VO_MAX,

  /***
   * auto mode, mpp select suitable vi.
   */
  VI_AUTO = 200,

  /***
   * use standard v4l2 framework for input
   */
  VI_V4L2,

  /***
   * use K1 ISP for input
   */
  VI_K1_CAM,

  VI_MAX,

  /***
   * auto mode, mpp select suitable vi.
   */
  VPS_AUTO = 300,

  /***
   * use v2d for graphic 2D convert (K1).
   */
  VPS_K1_V2D,

  VPS_MAX,
} MppModuleType;

static inline const char* mpp_moduletype2str(int cmd) {
#define MPP_MODULETYPE2STR(cmd) \
  case cmd:                     \
    return #cmd

  switch (cmd) {
    MPP_MODULETYPE2STR(CODEC_AUTO);
    MPP_MODULETYPE2STR(CODEC_OPENH264);
    MPP_MODULETYPE2STR(CODEC_FFMPEG);
    MPP_MODULETYPE2STR(CODEC_SFDEC);
    MPP_MODULETYPE2STR(CODEC_SFENC);
    MPP_MODULETYPE2STR(CODEC_CODADEC);
    MPP_MODULETYPE2STR(CODEC_SFOMX);
    MPP_MODULETYPE2STR(CODEC_V4L2);
    MPP_MODULETYPE2STR(CODEC_FAKEDEC);
    MPP_MODULETYPE2STR(CODEC_V4L2_LINLONV5V7);
    MPP_MODULETYPE2STR(CODEC_K1_JPU);
    MPP_MODULETYPE2STR(VO_SDL2);
    MPP_MODULETYPE2STR(VO_FILE);
    MPP_MODULETYPE2STR(VI_V4L2);
    MPP_MODULETYPE2STR(VI_K1_CAM);
    MPP_MODULETYPE2STR(VPS_K1_V2D);
    default:
      return "UNKNOWN";
  }
}

typedef enum _MppCodingType {
  CODING_UNKNOWN = 0,
  CODING_H263,
  CODING_H264,

  /***
   * Multiview Video Coding, 3D, etc.
   */
  CODING_H264_MVC,

  /***
   * no start code
   */
  CODING_H264_NO_SC,
  CODING_H265,
  CODING_MJPEG,
  CODING_JPEG,
  CODING_VP8,
  CODING_VP9,
  CODING_AV1,
  CODING_AVS,
  CODING_AVS2,
  CODING_MPEG1,
  CODING_MPEG2,
  CODING_MPEG4,
  CODING_RV,

  /***
   * ANNEX_G, Advanced Profile
   */
  CODING_VC1,

  /***
   * ANNEX_L, Simple and Main Profiles
   */
  CODING_VC1_ANNEX_L,
  CODING_FWHT,
  CODING_MAX,
} MppCodingType;

typedef enum _MppProfileType {
  PROFILE_UNKNOWN = 0,
  PROFILE_MPEG2_422,
  PROFILE_MPEG2_HIGH,
  PROFILE_MPEG2_SS,
  PROFILE_MPEG2_SNR_SCALABLE,
  PROFILE_MPEG2_MAIN,
  PROFILE_MPEG2_SIMPLE,

  PROFILE_H264_BASELINE,
  PROFILE_H264_CONSTRAINED_BASELINE,
  PROFILE_H264_MAIN,
  PROFILE_H264_EXTENDED,
  PROFILE_H264_HIGH,
  PROFILE_H264_HIGH_10,
  PROFILE_H264_HIGH_10_INTRA,
  PROFILE_H264_MULTIVIEW_HIGH,
  PROFILE_H264_HIGH_422,
  PROFILE_H264_HIGH_422_INTRA,
  PROFILE_H264_STEREO_HIGH,
  PROFILE_H264_HIGH_444,
  PROFILE_H264_HIGH_444_PREDICTIVE,
  PROFILE_H264_HIGH_444_INTRA,
  PROFILE_H264_CAVLC_444,

  PROFILE_VC1_SIMPLE,
  PROFILE_VC1_MAIN,
  PROFILE_VC1_COMPLEX,
  PROFILE_VC1_ADVANCED,

  PROFILE_MPEG4_SIMPLE,
  PROFILE_MPEG4_SIMPLE_SCALABLE,
  PROFILE_MPEG4_CORE,
  PROFILE_MPEG4_MAIN,
  PROFILE_MPEG4_N_BIT,
  PROFILE_MPEG4_SCALABLE_TEXTURE,
  PROFILE_MPEG4_SIMPLE_FACE_ANIMATION,
  PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE,
  PROFILE_MPEG4_HYBRID,
  PROFILE_MPEG4_ADVANCED_REAL_TIME,
  PROFILE_MPEG4_CORE_SCALABLE,
  PROFILE_MPEG4_ADVANCED_CODING,
  PROFILE_MPEG4_ADVANCED_CORE,
  PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE,
  PROFILE_MPEG4_SIMPLE_STUDIO,
  PROFILE_MPEG4_ADVANCED_SIMPLE,

  PROFILE_JPEG2000_CSTREAM_RESTRICTION_0,
  PROFILE_JPEG2000_CSTREAM_RESTRICTION_1,
  PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION,
  PROFILE_JPEG2000_DCINEMA_2K,
  PROFILE_JPEG2000_DCINEMA_4K,

  PROFILE_VP9_0,
  PROFILE_VP9_1,
  PROFILE_VP9_2,
  PROFILE_VP9_3,

  PROFILE_HEVC_MAIN,
  PROFILE_HEVC_MAIN_10,
  PROFILE_HEVC_MAIN_STILL_PICTURE,
  PROFILE_HEVC_REXT,

  PROFILE_VVC_MAIN_10,
  PROFILE_VVC_MAIN_10_444,

  PROFILE_AV1_MAIN,
  PROFILE_AV1_HIGH,
  PROFILE_AV1_PROFESSIONAL,

  PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,
  PROFILE_MJPEG_HUFFMAN_EXTENDED_SEQUENTIAL_DCT,
  PROFILE_MJPEG_HUFFMAN_PROGRESSIVE_DCT,
  PROFILE_MJPEG_HUFFMAN_LOSSLESS,
  PROFILE_MJPEG_JPEG_LS,
} MppProfileType;

static inline const char* mpp_codingtype2str(int cmd) {
#define MPP_CODING2STR(cmd) \
  case cmd:                 \
    return #cmd

  switch (cmd) {
    MPP_CODING2STR(CODING_UNKNOWN);
    MPP_CODING2STR(CODING_H263);
    MPP_CODING2STR(CODING_H264);
    MPP_CODING2STR(CODING_H264_MVC);
    MPP_CODING2STR(CODING_H264_NO_SC);
    MPP_CODING2STR(CODING_H265);
    MPP_CODING2STR(CODING_MJPEG);
    MPP_CODING2STR(CODING_JPEG);
    MPP_CODING2STR(CODING_VP8);
    MPP_CODING2STR(CODING_VP9);
    MPP_CODING2STR(CODING_AV1);
    MPP_CODING2STR(CODING_AVS);
    MPP_CODING2STR(CODING_AVS2);
    MPP_CODING2STR(CODING_MPEG1);
    MPP_CODING2STR(CODING_MPEG2);
    MPP_CODING2STR(CODING_MPEG4);
    MPP_CODING2STR(CODING_RV);
    MPP_CODING2STR(CODING_VC1);
    MPP_CODING2STR(CODING_VC1_ANNEX_L);
    MPP_CODING2STR(CODING_FWHT);
    default:
      return "UNKNOWN";
  }
}

/***
 * @description: pixelformat mpp or some other platform may use.
 */
typedef enum _MppPixelFormat {
  PIXEL_FORMAT_UNKNOWN = 0,

  /***
   * YYYYYYYYVVUU
   */
  PIXEL_FORMAT_YV12,

  /***
   * YYYYYYYYUUVV  YU12/YUV420P is the same
   */
  PIXEL_FORMAT_I420,

  /***
   * YYYYYYYYVUVU
   */
  PIXEL_FORMAT_NV21,

  /***
   * YYYYYYYYUVUV
   */
  PIXEL_FORMAT_NV12,

  /***
   * 11111111 11000000, 16bit only use 10bit
   */
  PIXEL_FORMAT_YV12_P010,

  /***
   * 11111111 11000000, 16bit only use 10bit
   */
  PIXEL_FORMAT_I420_P010,

  /***
   * 11111111 11000000, 16bit only use 10bit
   */
  PIXEL_FORMAT_NV21_P010,

  /***
   * 11111111 11000000, 16bit only use 10bit
   */
  PIXEL_FORMAT_NV12_P010,
  PIXEL_FORMAT_YV12_P016,
  PIXEL_FORMAT_I420_P016,
  PIXEL_FORMAT_NV21_P016,
  PIXEL_FORMAT_NV12_P016,

  /***
   * YYYYUUVV, YU16 is the same
   */
  PIXEL_FORMAT_YUV422P,

  /***
   * YYYYVVUU
   */
  PIXEL_FORMAT_YV16,

  /***
   * YYYYUVUV  NV16 is the same
   */
  PIXEL_FORMAT_YUV422SP,

  /***
   * YYYYVUVU
   */
  PIXEL_FORMAT_NV61,
  PIXEL_FORMAT_YUV422P_P010,
  PIXEL_FORMAT_YV16_P010,
  PIXEL_FORMAT_YUV422SP_P010,
  PIXEL_FORMAT_NV61_P010,

  /***
   * YYUUVV
   */
  PIXEL_FORMAT_YUV444P,

  /***
   * YYUVUV
   */
  PIXEL_FORMAT_YUV444SP,
  PIXEL_FORMAT_YUYV,
  PIXEL_FORMAT_YVYU,
  PIXEL_FORMAT_UYVY,
  PIXEL_FORMAT_VYUY,
  PIXEL_FORMAT_YUV_MB32_420,
  PIXEL_FORMAT_YUV_MB32_422,
  PIXEL_FORMAT_YUV_MB32_444,
  PIXEL_FORMAT_YUV_MAX,

  PIXEL_FORMAT_RGB_MIN,
  PIXEL_FORMAT_RGBA,
  PIXEL_FORMAT_ARGB,
  PIXEL_FORMAT_ABGR,
  PIXEL_FORMAT_BGRA,
  PIXEL_FORMAT_RGBA_5658,
  PIXEL_FORMAT_ARGB_8565,
  PIXEL_FORMAT_ABGR_8565,
  PIXEL_FORMAT_BGRA_5658,
  PIXEL_FORMAT_RGBA_5551,
  PIXEL_FORMAT_ARGB_1555,
  PIXEL_FORMAT_ABGR_1555,
  PIXEL_FORMAT_BGRA_5551,
  PIXEL_FORMAT_RGBA_4444,
  PIXEL_FORMAT_ARGB_4444,
  PIXEL_FORMAT_ABGR_4444,
  PIXEL_FORMAT_BGRA_4444,
  PIXEL_FORMAT_RGB_888,
  PIXEL_FORMAT_BGR_888,
  PIXEL_FORMAT_RGB_565,
  PIXEL_FORMAT_BGR_565,
  PIXEL_FORMAT_RGB_555,
  PIXEL_FORMAT_BGR_555,
  PIXEL_FORMAT_RGB_444,
  PIXEL_FORMAT_BGR_444,
  PIXEL_FORMAT_RGB_MAX,

  PIXEL_FORMAT_AFBC_YUV420_8,
  PIXEL_FORMAT_AFBC_YUV420_10,
  PIXEL_FORMAT_AFBC_YUV422_8,
  PIXEL_FORMAT_AFBC_YUV422_10,

  PIXEL_FORMAT_MAX,
} MppPixelFormat;

static inline const char* mpp_pixelformat2str(int cmd) {
#define MPP_PIXELFORMAT2STR(cmd) \
  case cmd:                      \
    return #cmd

  switch (cmd) {
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_UNKNOWN);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YV12);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_I420);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV21);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV12);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YV12_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_I420_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV21_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV12_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YV12_P016);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_I420_P016);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV21_P016);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV12_P016);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV422P);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YV16);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV422SP);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV61);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV422P_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YV16_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV422SP_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_NV61_P010);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV444P);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV444SP);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUYV);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YVYU);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_UYVY);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_VYUY);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV_MB32_420);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV_MB32_422);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_YUV_MB32_444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGBA);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ARGB);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ABGR);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGRA);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGBA_5658);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ARGB_8565);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ABGR_8565);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGRA_5658);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGBA_5551);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ARGB_1555);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ABGR_1555);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGRA_5551);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGBA_4444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ARGB_4444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_ABGR_4444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGRA_4444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGB_888);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGR_888);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGB_565);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGR_565);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGB_555);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGR_555);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_RGB_444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_BGR_444);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_AFBC_YUV420_8);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_AFBC_YUV420_10);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_AFBC_YUV422_8);
    MPP_PIXELFORMAT2STR(PIXEL_FORMAT_AFBC_YUV422_10);
    default:
      return "UNKNOWN";
  }
}

typedef enum _MppReturnValue {
  MPP_OK = 0,

  /***
   * error about memory
   */
  MPP_OUT_OF_MEM = -1,
  MPP_MALLOC_FAILED = -2,
  MPP_MMAP_FAILED = -3,
  MPP_MUNMAP_FAILED = -4,
  MPP_NULL_POINTER = -5,

  /***
   * error about file
   */
  MPP_FILE_NOT_EXIST = -100,
  MPP_OPEN_FAILED = -101,
  MPP_IOCTL_FAILED = -102,
  MPP_CLOSE_FAILED = -103,
  MPP_POLL_FAILED = -104,

  /***
   * error about codec
   */
  MPP_NO_STREAM = -200,
  MPP_NO_FRAME = -201,
  MPP_DECODER_ERROR = -202,
  MPP_ENCODER_ERROR = -203,
  MPP_CONVERTER_ERROR = -204,
  MPP_CODER_EOS = -205,
  MPP_CODER_NO_DATA = -206,
  MPP_RESOLUTION_CHANGED = -207,
  MPP_ERROR_FRAME = -208,
  MPP_CODER_NULL_DATA = -209,

  /***
   * error about dataqueue
   */
  MPP_DATAQUEUE_FULL = -300,
  MPP_DATAQUEUE_EMPTY = -301,

  /***
   * other
   */
  MPP_INIT_FAILED = -400,
  MPP_CHECK_FAILED = -401,
  MPP_BIND_NOT_MATCH = -402,
  MPP_NOT_SUPPORTED_FORMAT = -403,

  /*unknown error*/
  MPP_ERROR_UNKNOWN = -1023
} MppReturnValue;

static inline const char* mpp_err2str(int cmd) {
#define MPP_ERR2STR(cmd) \
  case cmd:              \
    return #cmd

  switch (cmd) {
    MPP_ERR2STR(MPP_OUT_OF_MEM);
    MPP_ERR2STR(MPP_MALLOC_FAILED);
    MPP_ERR2STR(MPP_MMAP_FAILED);
    MPP_ERR2STR(MPP_MUNMAP_FAILED);
    MPP_ERR2STR(MPP_NULL_POINTER);

    MPP_ERR2STR(MPP_FILE_NOT_EXIST);
    MPP_ERR2STR(MPP_OPEN_FAILED);
    MPP_ERR2STR(MPP_IOCTL_FAILED);
    MPP_ERR2STR(MPP_CLOSE_FAILED);
    MPP_ERR2STR(MPP_POLL_FAILED);

    MPP_ERR2STR(MPP_NO_STREAM);
    MPP_ERR2STR(MPP_NO_FRAME);
    MPP_ERR2STR(MPP_DECODER_ERROR);
    MPP_ERR2STR(MPP_ENCODER_ERROR);
    MPP_ERR2STR(MPP_CONVERTER_ERROR);
    MPP_ERR2STR(MPP_CODER_EOS);
    MPP_ERR2STR(MPP_CODER_NO_DATA);
    MPP_ERR2STR(MPP_RESOLUTION_CHANGED);

    MPP_ERR2STR(MPP_DATAQUEUE_FULL);
    MPP_ERR2STR(MPP_DATAQUEUE_EMPTY);

    MPP_ERR2STR(MPP_INIT_FAILED);
    MPP_ERR2STR(MPP_CHECK_FAILED);
    MPP_ERR2STR(MPP_BIND_NOT_MATCH);
    MPP_ERR2STR(MPP_NOT_SUPPORTED_FORMAT);

    default:
      return "UNKNOWN";
  }
}

/***
 * @description: frame buffer type
 */
typedef enum _MppFrameBufferType {
  /***
   * normal buffer, allocated by mpp, created by malloc, maybe.
   */
  MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL = 0,

  /***
   * dma buffer, allocated by mpp from /dev/dma_heap/linux,cma, maybe.
   */
  MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL = 1,

  /***
   * external buffer, mpp just get the data pointer and use it.
   */
  MPP_FRAME_BUFFERTYPE_NORMAL_EXTERNAL = 2,

  /***
   * external buffer, mpp just get the fd and use it.
   */
  MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL = 3,

  MPP_FRAME_BUFFERTYPE_TOTAL_NUM = 4,
} MppFrameBufferType;

typedef enum _MppDataTransmissinMode {
  /***
   * send unhandled data and receive handled data in one interface.
   */
  MPP_SYNC = 0,

  /***
   * send unhandled data SYNC(one interface), and receive handled data SYNC(one
   * interface).
   */
  MPP_INPUT_SYNC_OUTPUT_SYNC = 1,

  /***
   * send unhandled data SYNC(one interface), and receive handled data ASYNC(two
   * interfaces).
   */
  MPP_INPUT_SYNC_OUTPUT_ASYNC = 2,

  /***
   * send unhandled data ASYNC(two interfaces), and receive handled data
   * SYNC(one interface).
   */
  MPP_INPUT_ASYNC_OUTPUT_SYNC = 3,

  /***
   * send unhandled data ASYNC(two interfaces), and receive handled data
   * ASYNC(two interfaces).
   */
  MPP_INPUT_ASYNC_OUTPUT_ASYNC = 4,
} MppDataTransmissinMode;

typedef enum _MppFrameEos {
  FRAME_NO_EOS = 0,
  FRAME_EOS_WITH_DATA = 1,
  FRAME_EOS_WITHOUT_DATA = 2,
} MppFrameEos;

/***
 * (nXmin,nYmin)
 *     +----------+
 *     |          |
 *     |          |
 *     |          |
 *     +----------+
 *            (nXmax,nYmax)
 */
typedef struct _MppRect {
  U32 nXmin;
  U32 nXmax;
  U32 nYmin;
  U32 nYmax;
} MppRect;

/***
 * (nX0,nY0)        (nX1,nY1)
 *     +----------------+
 */
typedef struct _MppLine {
  U32 nX0;
  U32 nY0;
  U32 nX1;
  U32 nY1;
} MppLine;

typedef struct _MppPoint {
  U32 nX;
  U32 nY;
} MppPoint;

typedef enum _MppRotate {
  /***
   *  +---+-------+
   *  |   |       |
   *  +---+       |
   *  |           |
   *  +-----------+
   */
  MPP_ROTATE_0,

  /***
   *  +-----+-----+
   *  |     |     |
   *  |     +-----+
   *  |           |
   *  |           |
   *  |           |
   *  +-----------+
   */
  MPP_ROTATE_90,

  /***
   *  +-----------+
   *  |           |
   *  |       +---+
   *  |       |   |
   *  +-------+---+
   */
  MPP_ROTATE_180,

  /***
   *  +-----------+
   *  |           |
   *  |           |
   *  |           |
   *  +-----+     |
   *  |     |     |
   *  +-----+-----+
   */
  MPP_ROTATE_270,

  /***
   *  +-------+---+
   *  |       |   |
   *  |       +---+
   *  |           |
   *  +-----------+
   */
  MPP_MIRROR,

  /***
   *  +-----------+
   *  |           |
   *  +---+       |
   *  |   |       |
   *  +---+-------+
   */
  MPP_VFLIP,
} MppRotate;

typedef enum _MppScale {
  /***
   * 1X1->2X2
   */
  MPP_SCALE_2,

  /***
   * 1X1->4X4
   */
  MPP_SCALE_4,

  /***
   * 2X2->1X1
   */
  MPP_SCALE_1_2,

  /***
   * 4X4->1X1
   */
  MPP_SCALE_1_4,
  MPP_SCALE_CUSTOM,
} MppScale;

typedef enum _MppBlendMode {
  MPP_BLEND_ALPHA,
  MPP_BLEND_ROP,
  MPP_BLEND_BUTT,
} MppBlendMode;

typedef enum _MppBlendAlphaMode {
  MPP_BLEND_ALPHA_ZERO,
  MPP_BLEND_ALPHA_ONE,
  MPP_BLEND_ALPHA_SRC_ALPHA,
  MPP_BLEND_ALPHA_ONE_MINUS_SRC_ALPHA,
  MPP_BLEND_ALPHA_DST_ALPHA,
  MPP_BLEND_ALPHA_ONE_MINUS_DST_ALPHA,
  MPP_BLEND_ALPHA_BUTT,
} MppBlendAlphaMode;

typedef enum _MppBlendRopMode {
  MPP_BLEND_ROP_BLACK,
  MPP_BLEND_ROP_NOTMERGEPEN,  //~(S2+S1)
  MPP_BLEND_ROP_MASKNOTPEN,   //~S2&S1
  MPP_BLEND_ROP_NOTCOPYPEN,   //~S2
  MPP_BLEND_ROP_MASKPENNOT,   // S2&~S1
  MPP_BLEND_ROP_NOT,          //~S1
  MPP_BLEND_ROP_XORPEN,       // S2^S1
  MPP_BLEND_ROP_NOTMASKPEN,   //~(S2&S1)
  MPP_BLEND_ROP_MASKPEN,      // S2&S1
  MPP_BLEND_ROP_NOTXORPEN,    //~(S2^S1)
  MPP_BLEND_ROP_NOP,          // S1
  MPP_BLEND_ROP_MERGENOTPEN,  //~S2+S1
  MPP_BLEND_ROP_COPYPEN,      // S2
  MPP_BLEND_ROP_MERGEPENNOT,  // S2+~S1
  MPP_BLEND_ROP_MERGEPEN,     // S2+S1
  MPP_BLEND_ROP_WHITE,
  MPP_BLEND_ROP_BUTT,
} MppBlendRopMode;

typedef struct _MppRGBColor {
  U32 nR;
  U32 nG;
  U32 nB;
  U32 nA;
} MppRGBColor;

typedef struct _MppYUVColor {
  U32 nY;
  U32 nU;
  U32 nV;
} MppYUVColor;

/***
 * @description: para sent and get between application and decoder.
 */
typedef struct _MppVdecPara {
  /***
   * set to MPP
   */
  MppCodingType eCodingType;
  S32 nProfile;

  /***
   * read from MPP
   */
  MppFrameBufferType eFrameBufferType;
  MppDataTransmissinMode eDataTransmissinMode;

  /***
   * set to MPP
   */
  S32 nWidth;
  S32 nHeight;
  S32 nStride;
  S32 nScale;

  /***
   * Horizontal downscale ratio, [1, 256]
   * set to MPP
   */
  S32 nHorizonScaleDownRatio;

  /***
   * Vertical downscale ratio, [1, 128]
   * set to MPP
   */
  S32 nVerticalScaleDownRatio;

  /***
   * Downscaled frame width in pixels
   * set to MPP
   */
  S32 nHorizonScaleDownFrameWidth;

  /***
   * Downscaled frame height in pixels
   * set to MPP
   */
  S32 nVerticalScaleDownFrameHeight;

  /***
   * 0, 90, 180, 270
   * set to MPP
   */
  S32 nRotateDegree;
  S32 bThumbnailMode;
  BOOL bIsInterlaced;
  BOOL bIsFrameReordering;
  BOOL bIgnoreStreamHeaders;
  MppPixelFormat eOutputPixelFormat;
  BOOL bNoBFrames;
  BOOL bDisable3D;
  BOOL bSupportMaf;
  BOOL bDispErrorFrame;
  BOOL bInputBlockModeEnable;
  BOOL bOutputBlockModeEnable;

  /***
   * read from MPP
   */
  /***
   * input buffer num that APP can use
   */
  S32 nInputQueueLeftNum;
  S32 nOutputQueueLeftNum;
  S32 nInputBufferNum;
  S32 nOutputBufferNum;
  void* pFrame[64];
  S32 nOldWidth;
  S32 nOldHeight;
  BOOL bIsResolutionChanged;

  /***
   * used for chromium
   */
  BOOL bIsBufferInDecoder[64];
  S32 nOutputBufferFd[64];
} MppVdecPara;

/***
 * @description: para sent and get between application and encoder.
 */
typedef struct _MppVencPara {
  /***
   * set to MPP
   */
  MppCodingType eCodingType;
  S32 nProfile;
  MppPixelFormat PixelFormat;

  /***
   * read from MPP
   */
  MppFrameBufferType eFrameBufferType;
  MppDataTransmissinMode eDataTransmissinMode;

  /***
   * set to MPP
   */
  S32 nWidth;
  S32 nHeight;
  S32 nStride;
  S32 nBitrate;
  S32 nFrameRate;
} MppVencPara;

typedef enum _MppG2dCmd {
  /***
   * draw.
   */
  MPP_G2D_CMD_DRAW,

  /***
   * fill color to a rect of a frame.
   */
  MPP_G2D_CMD_FILL_COLOR,

  /***
   * rotate a rect of src frame to a rect of dst frame.
   */
  MPP_G2D_CMD_ROTATE,

  /***
   * scale a rect of src frame to a rect of dst frame.
   */
  MPP_G2D_CMD_SCALE,

  /***
   * copy a rect of src frame to a rect of dst frame, not
   * support scale and csc
   */
  MPP_G2D_CMD_COPY,
  MPP_G2D_CMD_DITHER,
  MPP_G2D_CMD_MASK,
} MppG2dCmd;

typedef struct _MppG2dFillColorPara {
  MppPixelFormat eColorFormat;
  union {
    MppRGBColor sRGBFillColor;
    MppYUVColor sYUVFillColor;
  };
  MppRect sFillPosition;
} MppG2dFillColorPara;

typedef struct _MppG2dCopyPara {
  MppPixelFormat eColorFormat;
  MppRect sSrcPosition;
  MppRect sDstPosition;
} MppG2dCopyPara;

typedef struct _MppG2dScalePara {
  MppPixelFormat eColorFormat;
  MppScale eScale;
  MppRect sSrcPosition;
  MppRect sDstPosition;
} MppG2dScalePara;

typedef struct _MppG2dRotatePara {
  MppPixelFormat eColorFormat;
  MppRotate eRotate;
  MppRect sSrcPosition;
  MppRect sDstPosition;
} MppG2dRotatePara;

typedef struct _MppG2dMaskPara {
  MppPixelFormat eColorFormat;
  U32 nWidth;
  U32 nHeight;
  void* mask;
} MppG2dMaskPara;

typedef struct _MppG2dDrawPara {
  MppPixelFormat eColorFormat;
  U32 nWidth;
  U32 nHeight;
  U32 nNumOfRect;
  U32 nNumOfLine;
  MppRect* pRect;
  MppLine* pLine;
} MppG2dDrawPara;

typedef struct _MppG2dPara {
  /***
   * read from MPP
   */
  MppFrameBufferType eInputFrameBufferType;
  MppFrameBufferType eOutputFrameBufferType;
  MppDataTransmissinMode eDataTransmissinMode;

  /***
   * set to MPP
   */
  MppG2dCmd eG2dCmd;
  MppPixelFormat eInputPixelFormat;
  MppPixelFormat eOutputPixelFormat;
  S32 nInputBufFd;
  S32 nOutputBufFd;
  S32 nInputWidth;
  S32 nInputHeight;
  S32 nOutputWidth;
  S32 nOutputHeight;
  S32 nInputBufSize;
  S32 nOutputBufSize;
  union {
    MppG2dFillColorPara sFillColorPara;
    MppG2dCopyPara sCopyPara;
    MppG2dScalePara sScalePara;
    MppG2dRotatePara sRotatePara;
    MppG2dMaskPara sMaskPara;
    MppG2dDrawPara sDrawPara;
  };
} MppG2dPara;

/***
 * @description: para sent and get between application and decoder.
 */
typedef struct _MppVoPara {
  MppFrameBufferType eFrameBufferType;
  MppDataTransmissinMode eDataTransmissinMode;
  BOOL bIsFrame;

  /***
   * for frame
   */
  MppPixelFormat ePixelFormat;
  S32 nWidth;
  S32 nHeight;
  S32 nStride;

  /***
   * for vo file
   */
  U8* pOutputFileName;
} MppVoPara;

/***
 * @description: para sent and get between application and decoder.
 */
typedef struct _MppViPara {
  MppFrameBufferType eFrameBufferType;
  MppDataTransmissinMode eDataTransmissinMode;

  /***
   * for frame
   */
  MppPixelFormat ePixelFormat;
  S32 nWidth;
  S32 nHeight;
  S32 nStride;
  S32 nBufferNum;

  /***
   * for vi file
   */
  U8 pVideoDeviceName[128];
} MppViPara;

#endif /*_MPP_PARA_H_*/
