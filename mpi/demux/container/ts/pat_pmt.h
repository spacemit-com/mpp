/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    pat_pmt.h
 * @Brief     :    MPEG-2 TS PAT/PMT table definitions and parsing.
 *------------------------------------------------------------------------------
 */

#ifndef PAT_PMT_H
#define PAT_PMT_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Well-known PIDs */
#define TS_PID_PAT 0x0000
#define TS_PID_CAT 0x0001
#define TS_PID_TSDT 0x0002
#define TS_PID_NULL 0x1FFF

/* Table IDs */
#define TABLE_ID_PAT 0x00
#define TABLE_ID_CAT 0x01
#define TABLE_ID_PMT 0x02

/* Stream types */
#define STREAM_TYPE_MPEG1_VIDEO 0x01
#define STREAM_TYPE_MPEG2_VIDEO 0x02
#define STREAM_TYPE_MPEG1_AUDIO 0x03
#define STREAM_TYPE_MPEG2_AUDIO 0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA 0x06
#define STREAM_TYPE_AAC 0x0F
#define STREAM_TYPE_H264 0x1B
#define STREAM_TYPE_H265 0x24
#define STREAM_TYPE_AC3 0x81

#define TS_MAX_PROGRAMS 16
#define TS_MAX_STREAMS 16

/* PAT entry */
typedef struct _PatEntry {
    U16 u16ProgramNum;
    U16 u16PmtPid;
} PatEntry;

/* PAT table */
typedef struct _PatTable {
    U16 u16TsId;
    U8 u8Version;
    U8 u8CurrentNext;
    U8 u8SectionNum;
    U8 u8LastSectionNum;
    U32 u32ProgramCount;
    PatEntry astPrograms[TS_MAX_PROGRAMS];
} PatTable;

/* PMT stream entry */
typedef struct _PmtStreamEntry {
    U8 u8StreamType;
    U16 u16ElementaryPid;
    U16 u16EsInfoLen;
} PmtStreamEntry;

/* PMT table */
typedef struct _PmtTable {
    U16 u16ProgramNum;
    U8 u8Version;
    U16 u16PcrPid;
    U16 u16ProgramInfoLen;
    U32 u32StreamCount;
    PmtStreamEntry astStreams[TS_MAX_STREAMS];
} PmtTable;

/**
 * @brief  Parse PAT section
 * @return 0 on success
 */
S32 Pat_Parse(const U8 *pu8Data, U32 u32Len, PatTable *pPat);

/**
 * @brief  Parse PMT section
 * @return 0 on success
 */
S32 Pmt_Parse(const U8 *pu8Data, U32 u32Len, PmtTable *pPmt);

/**
 * @brief  Find video stream in PMT
 * @return PID of first video stream, or 0 if not found
 */
U16 Pmt_FindVideoPid(const PmtTable *pPmt);

/**
 * @brief  Find audio stream in PMT
 * @return PID of first audio stream, or 0 if not found
 */
U16 Pmt_FindAudioPid(const PmtTable *pPmt);

/**
 * @brief  Get stream type name
 */
const char *Ts_GetStreamTypeName(U8 u8StreamType);

#ifdef __cplusplus
}
#endif

#endif /* __PAT_PMT_H__ */
