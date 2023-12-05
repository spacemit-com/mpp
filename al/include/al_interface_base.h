/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 09:34:01
 * @LastEditTime: 2023-02-03 08:58:54
 * @Description: base class of the abstract layer interface
 */

#ifndef _AL_INTERFACE_BASE_H_
#define _AL_INTERFACE_BASE_H_

#include "data.h"
#include "dataqueue.h"
#include "frame.h"
#include "packet.h"
#include "para.h"
#include "ringbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIXEL_FORMAT_MAPPING_DEFINE(Type, format) \
  typedef struct _AL##Type##PixelFormatMapping {  \
    MppPixelFormat eMppPixelFormat;               \
    format e##Type##PixelFormat;                  \
  } AL##Type##PixelFormatMapping;

#define PIXEL_FORMAT_MAPPING_CONVERT(Type, type, format)                     \
  static MppPixelFormat get_##type##_mpp_pixel_format(format src_format) {   \
    S32 i = 0;                                                               \
    S32 mapping_length = NUM_OF(stAL##Type##PixelFormatMapping);             \
    for (i = 0; i < mapping_length; i++) {                                   \
      if (src_format ==                                                      \
          stAL##Type##PixelFormatMapping[i].e##Type##PixelFormat)            \
        return stAL##Type##PixelFormatMapping[i].eMppPixelFormat;            \
    }                                                                        \
                                                                             \
    error("Can not find the mapping format, please check it(%d) !",          \
          mapping_length);                                                   \
    return PIXEL_FORMAT_UNKNOWN;                                             \
  }                                                                          \
                                                                             \
  static format get_##type##_codec_pixel_format(MppPixelFormat src_format) { \
    S32 i = 0;                                                               \
    S32 mapping_length = NUM_OF(stAL##Type##PixelFormatMapping);             \
    for (i = 0; i < mapping_length; i++) {                                   \
      if (src_format == stAL##Type##PixelFormatMapping[i].eMppPixelFormat)   \
        return stAL##Type##PixelFormatMapping[i].e##Type##PixelFormat;       \
    }                                                                        \
                                                                             \
    error("Can not find the mapping format, please check it !");             \
    return (format)0;                                                        \
  }

#define CODING_TYPE_MAPPING_DEFINE(Type, format) \
  typedef struct _AL##Type##CodingTypeMapping {  \
    MppCodingType eMppCodingType;                \
    format e##Type##CodingType;                  \
  } AL##Type##CodingTypeMapping;

#define CODING_TYPE_MAPPING_CONVERT(Type, type, format)                     \
  static MppCodingType get_##type##_mpp_coding_type(format src_type) {      \
    S32 i = 0;                                                              \
    S32 mapping_length = NUM_OF(stAL##Type##CodingTypeMapping);             \
    for (i = 0; i < mapping_length; i++) {                                  \
      if (src_type == stAL##Type##CodingTypeMapping[i].e##Type##CodingType) \
        return stAL##Type##CodingTypeMapping[i].eMppCodingType;             \
    }                                                                       \
                                                                            \
    error("Can not find the mapping format, please check it !");            \
    return CODING_UNKNOWN;                                                  \
  }                                                                         \
                                                                            \
  static format get_##type##_codec_coding_type(MppCodingType src_type) {    \
    S32 i = 0;                                                              \
    S32 mapping_length = NUM_OF(stAL##Type##CodingTypeMapping);             \
    for (i = 0; i < mapping_length; i++) {                                  \
      if (src_type == stAL##Type##CodingTypeMapping[i].eMppCodingType)      \
        return stAL##Type##CodingTypeMapping[i].e##Type##CodingType;        \
    }                                                                       \
                                                                            \
    error("Can not find the mapping coding type, please check it !");       \
    return CODING_UNKNOWN;                                                  \
  }

/*
 *                          +----------------+
 *                          |                |
 *                          |  ALBaseContext |
 *                          |                |
 *                          +--------^-------+
 *                                   |
 *                                   |
 *         +-------------------------+-------------------------+
 *         |                         |                         |
 * +-------+-----------     +--------+---------+     +---------+--------+
 * |                  |     |                  |     |                  |
 * | ALDecBaseContext |     | ALEncBaseContext |     | ALG2dBaseContext |
 * |                  |     |                  |     |                  |
 * +------------------+     +------------------+     +------------------+
 *
 */

typedef struct _ALBaseContext ALBaseContext;
typedef struct _ALDecBaseContext ALDecBaseContext;
typedef struct _ALEncBaseContext ALEncBaseContext;
typedef struct _ALG2dBaseContext ALG2dBaseContext;

struct _ALBaseContext {};

struct _ALDecBaseContext {
  ALBaseContext stAlBaseContext;
};

struct _ALEncBaseContext {
  ALBaseContext stAlBaseContext;
};

struct _ALG2dBaseContext {
  ALBaseContext stAlBaseContext;
};

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_BASE_H_*/
