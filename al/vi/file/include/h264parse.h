/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-01 15:30:16
 * @Description:
 */

#ifndef _MPP_H264PARSE_H_
#define _MPP_H264PARSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "type.h"

/*
 *  NALU
 *  +---------------+
 *	|0|1|2|3|4|5|6|7|
 *	+---------------+
 *	|F|NRI|   Type  |
 *	+---------------+
 *	|0|1|1|0|0|1|1|1|
 *	+---------------+
 *
 *  F: forbidden bit, 0 on normal, 1 on error, always 0.
 *  NRI: important level, 11 on "very important".
 *  Type:
 *	{
 *		0:未使用
 *		1:不区分，非I帧
 *		2:片分区A
 *		3:片分区B
 *		4:片分区C
 *		5:I帧
 *		6:补充增强信息单元（DEI）
 *		7:SPS标识(序列参数集)
 *		8:PPS标识(图像参数集)
 *		9:分节符
 *		10:序列结束
 *		11:码流结束
 *		12:填充
 *		13-23:保留
 *		24-31:未使用
 *  }
 *
 */

typedef struct vui_parameters {
  S32 aspect_ratio_info_present_flag;           // 0 u(1)
  S32 aspect_ratio_idc;                         // 0 u(8)
  S32 sar_width;                                // 0 u(16)
  S32 sar_height;                               // 0 u(16)
  S32 overscan_info_present_flag;               // 0 u(1)
  S32 overscan_appropriate_flag;                // 0 u(1)
  S32 video_signal_type_present_flag;           // 0 u(1)
  S32 video_format;                             // 0 u(3)
  S32 video_full_range_flag;                    // 0 u(1)
  S32 colour_description_present_flag;          // 0 u(1)
  S32 colour_primaries;                         // 0 u(8)
  S32 transfer_characteristics;                 // 0 u(8)
  S32 matrix_coefficients;                      // 0 u(8)
  S32 chroma_loc_info_present_flag;             // 0 u(1)
  S32 chroma_sample_loc_type_top_field;         // 0 ue(v)
  S32 chroma_sample_loc_type_bottom_field;      // 0 ue(v)
  S32 timing_info_present_flag;                 // 0 u(1)
  U32 num_units_in_tick;                        // 0 u(32)
  U32 time_scale;                               // 0 u(32)
  S32 fixed_frame_rate_flag;                    // 0 u(1)
  S32 nal_hrd_parameters_present_flag;          // 0 u(1)
  S32 cpb_cnt_minus1;                           // 0 ue(v)
  S32 bit_rate_scale;                           // 0 u(4)
  S32 cpb_size_scale;                           // 0 u(4)
  S32 bit_rate_value_minus1[16];                // 0 ue(v)
  S32 cpb_size_value_minus1[16];                // 0 ue(v)
  S32 cbr_flag[16];                             // 0 u(1)
  S32 initial_cpb_removal_delay_length_minus1;  // 0 u(5)
  S32 cpb_removal_delay_length_minus1;          // 0 u(5)
  S32 dpb_output_delay_length_minus1;           // 0 u(5)
  S32 time_offset_length;                       // 0 u(5)
  S32 vcl_hrd_parameters_present_flag;          // 0 u(1)
  S32 low_delay_hrd_flag;                       // 0 u(1)
  S32 pic_struct_present_flag;                  // 0 u(1)
  S32 bitstream_restriction_flag;               // 0 u(1)
  S32 motion_vectors_over_pic_boundaries_flag;  // 0 ue(v)
  S32 max_bytes_per_pic_denom;                  // 0 ue(v)
  S32 max_bits_per_mb_denom;                    // 0 ue(v)
  S32 log2_max_mv_length_horizontal;            // 0 ue(v)
  S32 log2_max_mv_length_vertical;              // 0 ue(v)
  S32 num_reorder_frames;                       // 0 ue(v)
  S32 max_dec_frame_buffering;                  // 0 ue(v)
} vui_parameters_t;

enum _FrameType { I_Frame = 1, P_Frame, B_Frame, NONE } FrameType;

#define Extended_SAR 255
typedef struct SPS {
  S32 profile_idc;
  S32 constrait_set0_flag;
  S32 constrait_set1_flag;
  S32 constrait_set2_flag;
  S32 constrait_set3_flag;
  S32 reserved_zero_4bits;
  S32 level_idc;
  S32 seq_parameter_set_id;
  S32 chroma_format_idc;
  S32 seperate_colour_plane_flag;
  S32 bit_depth_luma_minus8;
  S32 bit_depth_chroma_minus8;
  S32 qpprime_y_zero_transform_bypass_flag;
  S32 seq_scaling_matrix_present_flag;
  S32 seq_scaling_list_present_flag[12];
  S32 userDefaultScalingMatrix4x4flag[16];
  S32 userDefaultScalingMatrix8x8flag[16];
  S32 scalinglist4x4[6][16];
  S32 scalinglisd8x8[6][64];
  S32 log2_max_frame_num_minus4;
  S32 pic_order_cnt_type;
  S32 log2_max_pic_order_cnt_lsb_minus4;
  S32 delta_pic_order_always_zero_flag;
  S32 offset_for_non_ref_pic;
  S32 offer_for_top_to_bottom_field;
  S32 num_ref_frame_in_pic_order_cnt_cycle;
  S32 offset_for_ref_frame_array[16];  // se(v)
  S32 num_ref_frames;
  S32 gaps_in_frame_num_value_allowed_flag;
  S32 pic_width_in_mbs_minus1;         // ue(v)
  S32 pic_height_in_map_units_minus1;  // u(1)
  S32 frame_mbs_only_flag;             // 0 u(1)
  S32 mb_adaptive_frame_field_flag;    // 0 u(1)
  S32 direct_8x8_inference_flag;       // 0 u(1)
  S32 frame_cropping_flag;             // u(1)
  S32 frame_crop_left_offset;          // ue(v)
  S32 frame_crop_right_offset;         // ue(v)
  S32 frame_crop_top_offset;           // ue(v)
  S32 frame_crop_bottom_offset;        // ue(v)
  S32 vui_parameters_present_flag;     // u(1)
  vui_parameters_t vui_parameters;

} SPS;

#endif /*_MPP_H264PARSE_H_*/
