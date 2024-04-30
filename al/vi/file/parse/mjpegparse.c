/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-29 19:19:21
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "mjpegparse.h"

#include "log.h"
#include "parse.h"

S32 PARSE_MJPEG_Init(MppParseContext *ctx) {}

S32 PARSE_MJPEG_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                      S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                      S32 is_first_seq_header) {
  S32 i = 0;
  S32 num_start_code = 0;  // num 0f 0x000001 or 0x00000001
  S32 num_finish_code = 0;
  U8 *rbsp_base;

  U8 tmp_0, tmp_1;
  U8 *tmp_mem;
  U8 *src_mem;

  U8 *start_pos;

  src_mem = stream_start_addr;

  while (1) {
    tmp_mem = src_mem + i;

    if ((0xff == *(tmp_mem)) && (0xd8 == *(tmp_mem + 1))) {
      debug("%p %x %x", tmp_mem, *(tmp_mem), *(tmp_mem + 1));
      if (num_start_code == 0) {
        start_pos = stream_start_addr + i;
        num_start_code++;
      }
      /*
      if((*(tmp_mem+2) == 0xff) && ((*(tmp_mem+3)&0xf0) == 0xe0))
      {
              debug("jump to find tail  %d", *(tmp_mem+4) << 8 +
*(tmp_mem+5)); i +=  *(tmp_mem+4) << 8 + *(tmp_mem+5);
              //tmp_mem = src_mem + i;
      }
      else
      {
              i ++;
      }
      */
      i++;

    } else if ((0xff == *(tmp_mem)) && (0xd9 == *(tmp_mem + 1))) {
      debug("%p %x %x", tmp_mem, *(tmp_mem), *(tmp_mem + 1));
      if (1 == num_start_code) {
        *frame_size = tmp_mem - start_pos;
        num_start_code = 0;
        num_finish_code = 1;
        i++;
        break;
      }
      i++;

    } else {
      i++;
    }

    if (i >= stream_size - 1) break;
  }

  if (1 == num_finish_code) {
    memcpy(frame_start_addr, start_pos, *frame_size);
    return 0;
  } else
    return 2;
}
