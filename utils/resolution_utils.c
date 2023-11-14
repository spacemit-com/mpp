/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-10 15:52:19
 * @LastEditTime: 2023-08-29 16:26:17
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "resolution_utils.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "type.h"

#define MODULE_TAG "mpp_resolution_utils"

BOOL is_h264_sps(U8 *buf, S32 buf_length, S32 *startcode_length) {
  S32 startcode_len = 0;
  U8 tmp;
  if ((0x00 == *(buf)) && (0x00 == *(buf + 1)) && (0x00 == *(buf + 2)) &&
      (0x01 == *(buf + 3))) {
    startcode_len = 4;
  }

  if ((0x00 == *(buf)) && (0x00 == *(buf + 1)) && (0x01 == *(buf + 2))) {
    startcode_len = 3;
  }

  tmp = *(buf + startcode_len) & 0x1f;

  // SEI&SPS&PPS
  if (0x07 == tmp) {
    debug("get a sps!");
    *startcode_length = startcode_len;
    return MPP_TRUE;
  }
  return MPP_FALSE;
}

#ifdef NOCACHE

typedef struct {
  const uint8_t *data;  //存放除了sps数据
  int count;            /* in bits */
  int index;            /* in bits */
} br_state;

#define BR_INIT(data, bytes) \
  { (data), 8 * (bytes), 0 }

#define BR_EOF(br) ((br)->index >= (br)->count)

static inline void br_init(br_state *br, const uint8_t *data, int bytes) {
  br->data = data;
  br->count = 8 * bytes;
  br->index = 0;
}

static inline int br_get_bit(br_state *br) {
  if (br->index >= br->count) return 1; /* -> no infinite colomb's ... */

  int r = (br->data[br->index >> 3] >> (7 - (br->index & 7))) & 1;
  br->index++;
  return r;
}

static inline uint32_t br_get_bits(br_state *br, uint32_t n) {
  uint32_t r = 0;
  while (n--) r = r | (br_get_bit(br) << n);
  return r;
}

#define br_skip_bit(br) br_skip_bits(br, 1)

static inline void br_skip_bits(br_state *br, int n) { br->index += n; }

#else /* NOCACHE */

typedef struct {
  uint8_t *data;
  uint8_t *data_end;
  uint32_t cache;
  uint32_t cache_bits;

} br_state;

#define BR_INIT(data, bytes) \
  { (data), (data) + (bytes), 0, 0 }

static inline void br_init(br_state *br, uint8_t *data, int bytes) {
  br->data = data;
  br->data_end = data + bytes;
  br->cache = 0;
  br->cache_bits = 0;
}

#define BR_GET_BYTE(br) (br->data < br->data_end ? *br->data++ : 0xff)

#define BR_EOF(br) ((br)->data >= (br)->data_end)

static inline uint32_t br_get_bits(br_state *br, uint32_t n) {
  if (n > 24) return (br_get_bits(br, 16) << 16) | br_get_bits(br, n - 16);

  while (br->cache_bits < 24) {
    br->cache = (br->cache << 8) | BR_GET_BYTE(br);
    br->cache_bits += 8;
  }

  br->cache_bits -= n;
  return (br->cache >> br->cache_bits) & ((1 << n) - 1);
}

static inline int br_get_bit(br_state *br) {
  if (!br->cache_bits) {
    br->cache = BR_GET_BYTE(br);
    br->cache_bits = 7;

  } else {
    br->cache_bits--;
  }
  return (br->cache >> br->cache_bits) & 1;
}

static inline void br_skip_bit(br_state *br) {
  if (!br->cache_bits) {
    br->cache = BR_GET_BYTE(br);
    br->cache_bits = 7;

  } else {
    br->cache_bits--;
  }
}

static inline void br_skip_bits(br_state *br, int n) {
  if (br->cache_bits >= n) {
    br->cache_bits -= n;

  } else {
    /* drop cached bits */
    n -= br->cache_bits;

    /* drop full bytes */
    br->data += (n >> 3);
    n &= 7;

    /* update cache */
    if (n) {
      br->cache = BR_GET_BYTE(br);
      br->cache_bits = 8 - n;

    } else {
      br->cache_bits = 0;
    }
  }
}

#endif /* NOCACHE */

#define br_get_u8(br) br_get_bits(br, 8)
#define br_get_u16(br) ((br_get_bits(br, 8) << 8) | br_get_bits(br, 8))

static inline uint32_t br_get_ue_golomb(br_state *br) {
  int n = 0;
  while (!br_get_bit(br) && n < 32) n++;
  return n ? ((1 << n) - 1) + br_get_bits(br, n) : 0;
}

#pragma warning(disable : 4146)

static inline int32_t br_get_se_golomb(br_state *br) {
  uint32_t r = br_get_ue_golomb(br) + 1;
  return (r & 1) ? -(r >> 1) : (r >> 1);
}

static inline void br_skip_golomb(br_state *br) {
  int n = 0;
  while (!br_get_bit(br) && n < 32) n++;
  br_skip_bits(br, n);
}

#define br_skip_ue_golomb(br) br_skip_golomb(br)
#define br_skip_se_golomb(br) br_skip_golomb(br)

int h264_parse_sps(uint8_t *buf, int len, h264_sps_data_t *sps) {
  // find sps
  BOOL findSPS = MPP_FALSE;

  if (buf[2] == 0) {
    if ((buf[4] & 0x1f) == 7) {  // start code 0 0 0 1
      len -= 5;
      buf += 5;
      findSPS = MPP_TRUE;
    }
  } else if (buf[2] == 1) {  // start code 0 0 1
    if ((buf[3] & 0x1f) == 7) {
      len -= 4;
      buf += 4;
      findSPS = MPP_TRUE;
    }
  } else {
    if ((buf[0] & 0x1f) == 7) {  // no start code 0x67 开头
      len -= 1;
      buf += 1;
      findSPS = MPP_TRUE;
    }
  }

  br_state br = BR_INIT(buf, len);
  int profile_idc, pic_order_cnt_type;
  int frame_mbs_only;
  int i, j;

  profile_idc = br_get_u8(&br);
  sps->profile = profile_idc;
  debug("H.264 SPS: profile_idc %d", profile_idc);
  /* constraint_set0_flag = br_get_bit(br);    */
  /* constraint_set1_flag = br_get_bit(br);    */
  /* constraint_set2_flag = br_get_bit(br);    */
  /* constraint_set3_flag = br_get_bit(br);    */
  /* reserved             = br_get_bits(br,4); */
  sps->level = br_get_u8(&br);
  br_skip_bits(&br, 8);
  br_skip_ue_golomb(&br); /* seq_parameter_set_id */
  if (profile_idc >= 100) {
    if (br_get_ue_golomb(&br) == 3) /* chroma_format_idc      */
      br_skip_bit(&br);             /* residual_colour_transform_flag */
    br_skip_ue_golomb(&br);         /* bit_depth_luma - 8             */
    br_skip_ue_golomb(&br);         /* bit_depth_chroma - 8           */
    br_skip_bit(&br);               /* transform_bypass               */
    if (br_get_bit(&br))            /* seq_scaling_matrix_present     */
      for (i = 0; i < 8; i++)
        if (br_get_bit(&br)) {
          /* seq_scaling_list_present    */
          int last = 8, next = 8, size = (i < 6) ? 16 : 64;
          for (j = 0; j < size; j++) {
            if (next) next = (last + br_get_se_golomb(&br)) & 0xff;
            last = next ? next : last;
          }
        }
  }

  br_skip_ue_golomb(&br); /* log2_max_frame_num - 4 */
  pic_order_cnt_type = br_get_ue_golomb(&br);
  if (pic_order_cnt_type == 0)
    br_skip_ue_golomb(&br); /* log2_max_poc_lsb - 4 */
  else if (pic_order_cnt_type == 1) {
    br_skip_bit(&br);          /* delta_pic_order_always_zero     */
    br_skip_se_golomb(&br);    /* offset_for_non_ref_pic          */
    br_skip_se_golomb(&br);    /* offset_for_top_to_bottom_field  */
    j = br_get_ue_golomb(&br); /* num_ref_frames_in_pic_order_cnt_cycle */
    for (i = 0; i < j; i++)
      br_skip_se_golomb(&br); /* offset_for_ref_frame[i]         */
  }
  br_skip_ue_golomb(&br); /* ref_frames                      */
  br_skip_bit(&br);       /* gaps_in_frame_num_allowed       */
  sps->width /* mbs */ = br_get_ue_golomb(&br) + 1;
  sps->height /* mbs */ = br_get_ue_golomb(&br) + 1;
  frame_mbs_only = br_get_bit(&br);
  debug("H.264 SPS: pic_width:  %u mbs", (unsigned)sps->width);
  debug("H.264 SPS: pic_height: %u mbs", (unsigned)sps->height);
  debug("H.264 SPS: frame only flag: %d", frame_mbs_only);

  sps->width *= 16;
  sps->height *= 16 * (2 - frame_mbs_only);

  if (!frame_mbs_only)
    if (br_get_bit(&br)) /* mb_adaptive_frame_field_flag */
      debug("H.264 SPS: MBAFF");
  br_skip_bit(&br); /* direct_8x8_inference_flag    */
  if (br_get_bit(&br)) {
    /* frame_cropping_flag */
    uint32_t crop_left = br_get_ue_golomb(&br);
    uint32_t crop_right = br_get_ue_golomb(&br);
    uint32_t crop_top = br_get_ue_golomb(&br);
    uint32_t crop_bottom = br_get_ue_golomb(&br);
    debug("H.264 SPS: cropping %d %d %d %d", crop_left, crop_top, crop_right,
          crop_bottom);

    sps->width -= 2 * (crop_left + crop_right);
    if (frame_mbs_only)
      sps->height -= 2 * (crop_top + crop_bottom);
    else
      sps->height -= 4 * (crop_top + crop_bottom);
  }

  /* VUI parameters */
  sps->pixel_aspect.num = 0;
  if (br_get_bit(&br)) {
    /* vui_parameters_present flag */
    if (br_get_bit(&br)) {
      /* aspect_ratio_info_present */
      uint32_t aspect_ratio_idc = br_get_u8(&br);
      debug("H.264 SPS: aspect_ratio_idc %d", aspect_ratio_idc);

      if (aspect_ratio_idc == 255 /* Extended_SAR */) {
        sps->pixel_aspect.num = br_get_u16(&br); /* sar_width */
        sps->pixel_aspect.den = br_get_u16(&br); /* sar_height */
        debug("H.264 SPS: -> sar %dx%d", sps->pixel_aspect.num,
              sps->pixel_aspect.den);

      } else {
        static const mpeg_rational_t aspect_ratios[] = {/* page 213: */
                                                        /* 0: unknown */
                                                        {0, 1},
                                                        /* 1...16: */
                                                        {1, 1},
                                                        {12, 11},
                                                        {10, 11},
                                                        {16, 11},
                                                        {40, 33},
                                                        {24, 11},
                                                        {20, 11},
                                                        {32, 11},
                                                        {80, 33},
                                                        {18, 11},
                                                        {15, 11},
                                                        {64, 33},
                                                        {160, 99},
                                                        {4, 3},
                                                        {3, 2},
                                                        {2, 1}

        };

        if (aspect_ratio_idc <
            sizeof(aspect_ratios) / sizeof(aspect_ratios[0])) {
          memcpy(&sps->pixel_aspect, &aspect_ratios[aspect_ratio_idc],
                 sizeof(mpeg_rational_t));
          debug("H.264 SPS: -> aspect ratio %d / %d", sps->pixel_aspect.num,
                sps->pixel_aspect.den);

        } else {
          error("H.264 SPS: aspect_ratio_idc out of range !");
        }
      }
    }
  }

  debug("H.264 SPS: -> video size %dx%d, aspect %d:%d", sps->width, sps->height,
        sps->pixel_aspect.num, sps->pixel_aspect.den);

  return 1;
}

S32 get_resolution_from_stream(U8 *buf, S32 buf_length, S32 *width,
                               S32 *height) {
  int startcode_length = 0;
  if (is_h264_sps(buf, buf_length, &startcode_length)) {
    debug("parse the h264 sps!");
    h264_sps_data_t sps;
    h264_parse_sps(buf, buf_length, &sps);
    *width = sps.width;
    *height = sps.height;
    return MPP_TRUE;
  }
  return MPP_FALSE;
}
