/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2024-04-29 19:19:08
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "h265parse.h"

#include "log.h"
#include "parse.h"

S32 PARSE_H265_Init(MppParseContext *ctx) {
  /*
  ctx->fp = fopen(p_file_name, "rb");
  if(!ctx->fp)
      return MPP_NULL_POINTER;

  ctx->pInputBuf = (U8*)malloc(STREAM_BUFFER_SIZE);
  if(!ctx->pInputBuf)
      return MPP_NULL_POINTER;
  */
}

static U32 read_bits(unsigned char bytes[], S32 num_read, S32 *bit_offset) {
  U32 bits;
  U32 utmp;

  S32 i;
  S32 bit_shift;

  S32 byte_offset, bit_offset_in_byte;
  S32 num_bytes_copy;

  if (num_read > 24) return 0xFFFFFFFF;
  if (0 == num_read) return 0;

  byte_offset = (*bit_offset) >> 3;  // byte_offset = (*bit_offset) / 8
  bit_offset_in_byte = (*bit_offset) - (byte_offset << 3);

  num_bytes_copy = ((*bit_offset + num_read) >> 3) - (*bit_offset >> 3) + 1;
  bits = 0;
  for (i = 0; i < num_bytes_copy; i++) {
    utmp = bytes[byte_offset + i];
    bits = (bits << 8) | (utmp);
  }

  bit_shift = (num_bytes_copy << 3) - (bit_offset_in_byte + num_read);
  bits >>= bit_shift;
  bits &= (0xFFFFFFFF >> (32 - num_read));

  *bit_offset += num_read;

  return bits;
}

static U32 ue_v(unsigned char bytes[], S32 *bit_offset) {
  U32 b;
  S32 leadingZeroBits = -1;
  U32 codeNum;

  for (b = 0; !b; leadingZeroBits++) {
    b = read_bits(bytes, 1, bit_offset);
  }

  // codeNum = 2^(leadingZeroBits) - 1 + read_bits(leadingZeroBits)
  codeNum = (1 << leadingZeroBits) - 1 +
            read_bits(bytes, leadingZeroBits, bit_offset);

  return codeNum;
}

static U32 se_v(unsigned char bytes[], S32 *bit_offset) {
  U32 b;
  S32 leadingZeroBits = -1;
  U32 codeNum;

  for (b = 0; !b; leadingZeroBits++) {
    b = read_bits(bytes, 1, bit_offset);
  }

  // codeNum = 2^(leadingZeroBits) - 1 + read_bits(leadingZeroBits)
  codeNum = (1 << leadingZeroBits) - 1 +
            read_bits(bytes, leadingZeroBits, bit_offset);

  return codeNum;
}

S32 PARSE_H265_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                     S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                     S32 is_first_seq_header) {
  S32 i = 0;
  S32 num_start_code = 0;  // num 0f 0x000001 or 0x00000001
  S32 hd_start_code = 0;   // num of SEI/SPS/PPS
  // when num_start_code = 2, we find a frame
  // when SEI/SPS/PPS after 0x000001or0x00000001
  // num_start_code not add， but hd_start_code add 1

  U8 *rbsp_base;

  U8 tmp_0, tmp_1;
  U8 *tmp_mem;
  U8 *src_mem;

  S32 nal_count = 0;
  S32 b_have_got_width_height = 0;  // for only get width and height once

  U8 *start_pos;

  src_mem = stream_start_addr;

  while (1) {
    tmp_mem = src_mem + i;

    // debug("i = %d %x", i, *tmp_mem);
    // NAL start with 00000001||000001
    if (((0x00 == *(tmp_mem)) && (0x00 == *(tmp_mem + 1)) &&
         (0x00 == *(tmp_mem + 2)) && (0x01 == *(tmp_mem + 3))) ||
        ((0x00 == *(tmp_mem)) && (0x00 == *(tmp_mem + 1)) &&
         (0x01 == *(tmp_mem + 2))))

    {
      rbsp_base = stream_start_addr + i;

      if ((0x00 == *(tmp_mem)) && (0x00 == *(tmp_mem + 1)) &&
          (0x00 == *(tmp_mem + 2)) && (0x01 == *(tmp_mem + 3))) {
        i++;
        tmp_mem = src_mem + i;
      }

      nal_count++;

      // tmp_0 = *(tmp_mem+3) & 0x1f ;
      tmp_0 = (*(tmp_mem + 3) & 0x7e) >> 1;

      if (0x20 == tmp_0 || 0x21 == tmp_0 || 0x022 == tmp_0 ||
          0x27 == tmp_0)  // SEI&SPS&PPS
      {
        hd_start_code++;
        if (num_start_code == 0 && hd_start_code == 1) {
          start_pos = rbsp_base;
        }
        if (num_start_code == 1) num_start_code++;
      }

      // SPS case
      if (0x07 == tmp_0)  // SPS
      {
        /*
        if (num_start_code ==0 && hd_start_code==1)  //find a SPS, but no I/B/P
                        {
            U8 *p_tmp;

            ptmp = tmp_mem + 3;
            SpsDecodeParser spsParser;
            if(spsParser.h264_decode_sps( ptmp,
                                                stream_size - i - 3,
                                                conf_data->width,
                                                conf_data->height)) //get the
        video resolution from SPS
            {
                bHaveGotWidthHeight = true; //get width and height ok
            }
                        }
        */
      }

#ifdef _SLICE_BASED_INTERFACE_
      if (is_first_seq_header && tmp_0 == 0x06)  //=== SEI
      {
        *frame_size = rbsp_base - *frame_start_addr;
        num_start_code = 2;
        break;
      }
#endif

      if (tmp_0 >= 0x0 && tmp_0 <= 0x15)  // non-IDR picture, IDR picture
      {
        U8 umpt;
        U8 *temp;
        // S32 frame_type;
        S32 offset = 0;
        temp = tmp_mem + 4;
        umpt = ue_v(temp, &offset);

        /*when nal_unit_type=1or5 and is_first_seq_header=true,
         * we only get the SEI&SPS&PPS, return
         * is_first_seq_header is used when first calling parser,
         * for get the video information
         * when first frame is not I frame, maybe P frame,
         * there is no SPS/PPS in front of P frame,
         * only return SEI.
         */
        if (is_first_seq_header) {
          *frame_size = rbsp_base - start_pos;
          num_start_code = 2;

          break;
        }

#ifdef _SLICE_BASED_INTERFACE_
        num_start_code++;
        if (num_start_code == 1 && hd_start_code == 0)
          *frame_start_addr = rbsp_base;
#else
        /*first_mb_in_slice is used for find the boundary between frames.
         * if the first bit of the byte is 1, it is the first slice of frame.
         */
        tmp_1 = *(tmp_mem + 5) & 0x80;
        {
          /*Here maybe the first slice, or the first slice of next frame,
           * it is determined by num_start_code and hd_start_code.
           */
          num_start_code++;
          if (num_start_code == 1 && hd_start_code == 0) start_pos = rbsp_base;
        }

        if (1 ==
            num_start_code)  // find the first I/B/P header（should not return）
        {
          umpt = ue_v(temp, &offset);  // slice_type
          // FrameType f_type;
          U8 slice_type = umpt;
          switch (slice_type) {
            case 2:
            case 7:
            case 4:
            case 9:
              // f_type = I_Frame;
              // if(conf_data) conf_data->cType = 'I';
              break;
            case 0:
            case 5:
            case 3:
            case 8:
              // f_type = P_Frame;
              // if(conf_data) conf_data->cType = 'P';
              break;
            case 1:
            case 6:
              // f_type = B_Frame;
              // if(conf_data) conf_data->cType = 'B';
              break;
            default:
              // f_type = NONE;
              // if(conf_data) conf_data->cType = 0;
              break;
          }

          // if(f_type != NONE) // 检查到帧类型
          //{
          //	  if(conf_data && conf_data->bGetWH == 0)
          ////如果不是获取分辨率则跳出 	    break;
          // }
        }

#endif
      }

      /*find two I/B/P header，or the first is I/B/P header，
       * secnod is SEI/SPS/PPS header，here can return
       */
      if (2 == num_start_code) {
        *frame_size = rbsp_base - start_pos;
        break;
      }

      i += 3;
    } else {
      i++;
    }
    if (i >= stream_size - 4) break;
  }

  if (2 == num_start_code) {
    memcpy(frame_start_addr, start_pos, *frame_size);
    return 0;
  }
  if (1 == num_start_code)
    return 1;
  else
    return 2;
}
