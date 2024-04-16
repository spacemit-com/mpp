/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2024-04-16 19:53:00
 * @Description:
 */

#ifndef _MPP_ARGUMENT_H_
#define _MPP_ARGUMENT_H_

#include "log.h"
#include "para.h"
#include "type.h"

typedef enum {
  INPUT,
  HELP,
  DECODE_FRAME_NUM,
  SAVE_FRAME_START,
  SAVE_FRAME_NUM,
  SAVE_FRAME_FILE,
  COST_DRAM_THREAD_NUM,
  CODING_TYPE,
  CODEC_TYPE,
  WIDTH,
  HEIGHT,
  FORMAT,
  INVALID
} ARGUMENT;

typedef struct _MppArgument {
  U8 sShortName[8];
  U8 sLongName[128];
  ARGUMENT eArgument;
  U8 sDescription[512];
} MppArgument;

/**
 * @description:
 * @param {MppArgument} *argument
 * @param {int} num
 * @return {*}
 */
static void print_demo_usage(const MppArgument *argument, S32 num) {
  S32 i = 0;
  printf("Usage:\n");
  while (i < num) {
    printf("%-8s %-24s  %s\n", argument[i].sShortName, argument[i].sLongName,
           argument[i].sDescription);
    i++;
  }
}

static void print_para_enum() {
  S32 i = 0;
  printf("--codectype:\n");
  for (i = 0; i < CODEC_MAX; i++) {
    printf("%-8d %-24s\n", i, mpp_codectype2str(i));
  }

  printf("--codingtype:\n");
  for (i = 0; i < CODING_MAX; i++) {
    printf("%-8d %-24s\n", i, mpp_codingtype2str(i));
  }

  printf("--format:\n");
  for (i = 0; i < PIXEL_FORMAT_YUV_MAX; i++) {
    printf("%-8d %-24s\n", i, mpp_pixelformat2str(i));
  }
  for (i = PIXEL_FORMAT_RGB_MIN; i < PIXEL_FORMAT_RGB_MAX; i++) {
    printf("%-8d %-24s\n", i, mpp_pixelformat2str(i));
  }
}

/**
 * @description:
 * @param {MppArgument} *argument
 * @param {char} *name
 * @param {int} num
 * @return {*}
 */
ARGUMENT get_argument(const MppArgument *argument, char *name, S32 num) {
  S32 i = 0;
  while (i < num) {
    if ((0 == strcmp(argument[i].sLongName, name)) ||
        ((0 == strcmp(argument[i].sShortName, name)) &&
         (0 != strcmp(argument[i].sShortName, "--")))) {
      return argument[i].eArgument;
    }
    i++;
  }
  return INVALID;
}

#endif /*_MPP_ARGUMENT_H_*/
