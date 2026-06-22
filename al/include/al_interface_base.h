/*
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: base definitions of the abstract layer interface.
 *               AL plugins directly consume MPI public structs
 *               (VdecChnAttr / VencChnAttr / VideoFrameInfo / StreamBufferInfo).
 */

#ifndef AL_INTERFACE_BASE_H
#define AL_INTERFACE_BASE_H

#include "para.h"
#include "sys/sys_type.h"
#include "sys/type.h"
#include "sys/vb_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIXEL_FORMAT_MAPPING_DEFINE(Type, format)  \
    typedef struct _AL##Type##PixelFormatMapping { \
        MppPixelFormat eMppPixelFormat;            \
        format e##Type##PixelFormat;               \
    } AL##Type##PixelFormatMapping;

#define PIXEL_FORMAT_MAPPING_CONVERT(Type, type, format)                                 \
    static MppPixelFormat get_##type##_mpp_pixel_format(format src_format) {             \
        S32 i = 0;                                                                       \
        S32 mapping_length = NUM_OF(stAL##Type##PixelFormatMapping);                     \
        for (i = 0; i < mapping_length; i++) {                                           \
            if (src_format == stAL##Type##PixelFormatMapping[i].e##Type##PixelFormat)    \
                return stAL##Type##PixelFormatMapping[i].eMppPixelFormat;                \
        }                                                                                \
                                                                                            \
        error("Can not find the mapping format, please check it(%d) !", mapping_length); \
        return MPP_PIXEL_FORMAT_UNKNOWN;                                                 \
    }                                                                                    \
                                                                                            \
    static format get_##type##_codec_pixel_format(MppPixelFormat src_format) {           \
        S32 i = 0;                                                                       \
        S32 mapping_length = NUM_OF(stAL##Type##PixelFormatMapping);                     \
        for (i = 0; i < mapping_length; i++) {                                           \
            if (src_format == stAL##Type##PixelFormatMapping[i].eMppPixelFormat)         \
                return stAL##Type##PixelFormatMapping[i].e##Type##PixelFormat;           \
        }                                                                                \
                                                                                            \
        error("Can not find the mapping format, please check it !");                     \
        return (format)0;                                                                \
    }

#define CODING_TYPE_MAPPING_DEFINE(Type, format)  \
    typedef struct _AL##Type##CodingTypeMapping { \
        MppStreamCodecType eMppCodecType;         \
        format e##Type##CodingType;               \
    } AL##Type##CodingTypeMapping;

#define CODING_TYPE_MAPPING_CONVERT(Type, type, format)                           \
    static MppStreamCodecType get_##type##_mpp_coding_type(format src_type) {     \
        S32 i = 0;                                                                \
        S32 mapping_length = NUM_OF(stAL##Type##CodingTypeMapping);               \
        for (i = 0; i < mapping_length; i++) {                                    \
            if (src_type == stAL##Type##CodingTypeMapping[i].e##Type##CodingType) \
                return stAL##Type##CodingTypeMapping[i].eMppCodecType;            \
        }                                                                         \
                                                                                    \
        error("Can not find the mapping format, please check it !");              \
        return MPP_STREAM_CODEC_UNKNOWN;                                          \
    }                                                                             \
                                                                                    \
    static format get_##type##_codec_coding_type(MppStreamCodecType src_type) {   \
        S32 i = 0;                                                                \
        S32 mapping_length = NUM_OF(stAL##Type##CodingTypeMapping);               \
        for (i = 0; i < mapping_length; i++) {                                    \
            if (src_type == stAL##Type##CodingTypeMapping[i].eMppCodecType)       \
                return stAL##Type##CodingTypeMapping[i].e##Type##CodingType;      \
        }                                                                         \
                                                                                    \
        error("Can not find the mapping coding type, please check it !");         \
        return (format)0;                                                         \
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
typedef struct _ALVoBaseContext ALVoBaseContext;
typedef struct _ALViBaseContext ALViBaseContext;

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

struct _ALVoBaseContext {
    ALBaseContext stAlBaseContext;
};

struct _ALViBaseContext {
    ALBaseContext stAlBaseContext;
};

#ifdef __cplusplus
};
#endif

#endif /*AL_INTERFACE_BASE_H*/
