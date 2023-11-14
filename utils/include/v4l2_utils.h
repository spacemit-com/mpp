/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-10 15:52:19
 * @LastEditTime: 2023-02-02 14:53:49
 * @Description:
 */

#ifndef _MPP_V4L2_UTILS_H_
#define _MPP_V4L2_UTILS_H_

#include <linux/videodev2.h>

#include "para.h"
#include "type.h"

typedef enum _DeviceType {
  DECODER = 0,
  ENCODER = 1,
} DeviceType;

typedef struct _DeviceInfo {
  S32 device_num;
  DeviceType type;
} DeviceInfo;

/**
 * @description:
 * @return {*}
 */
BOOL check_v4l2();

/**
 * @description:
 * @return {*}
 */
BOOL check_v4l2_linlonv5v7();

/**
 * @description:
 * @param {U8} *device_path
 * @param {S32} coding_type
 * @return {*}
 */
S32 find_v4l2_decoder(U8 *device_path, S32 coding_type);

/**
 * @description:
 * @param {U8} *device_path
 * @param {S32} coding_type
 * @return {*}
 */
S32 find_v4l2_encoder(U8 *device_path, S32 coding_type);

/**
 * @description:
 * @param {S32} fd
 * @param {S32} id
 * @param {S32} val
 * @return {*}
 */
S32 mpp_v4l2_set_ctrl(S32 fd, S32 id, S32 val);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_format} *fmt
 * @param {enum v4l2_buf_type} buf_type
 * @return {*}
 */
S32 mpp_v4l2_get_format(S32 fd, struct v4l2_format *fmt,
                        enum v4l2_buf_type buf_type);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_format} *fmt
 * @return {*}
 */
S32 mpp_v4l2_set_format(S32 fd, struct v4l2_format *fmt);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_format} *fmt
 * @return {*}
 */
S32 mpp_v4l2_try_format(S32 fd, struct v4l2_format *fmt);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_event_subscription} *sub
 * @return {*}
 */
S32 mpp_v4l2_subscribe_event(S32 fd, struct v4l2_event_subscription *sub);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_requestbuffers} *reqbuf
 * @return {*}
 */
S32 mpp_v4l2_req_buffers(S32 fd, struct v4l2_requestbuffers *reqbuf);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_buffer} *buf
 * @return {*}
 */
S32 mpp_v4l2_query_buffer(S32 fd, struct v4l2_buffer *buf);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_buffer} *buf
 * @return {*}
 */
S32 mpp_v4l2_queue_buffer(S32 fd, struct v4l2_buffer *buf);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_buffer} *buf
 * @return {*}
 */
S32 mpp_v4l2_dequeue_buffer(S32 fd, struct v4l2_buffer *buf);

/**
 * @description:
 * @param {S32} fd
 * @param {enum v4l2_buf_type} *type
 * @return {*}
 */
S32 mpp_v4l2_stream_on(S32 fd, enum v4l2_buf_type *type);

/**
 * @description:
 * @param {S32} fd
 * @param {enum v4l2_buf_type} *type
 * @return {*}
 */
S32 mpp_v4l2_stream_off(S32 fd, enum v4l2_buf_type *type);

/**
 * @description:
 * @param {S32} fd
 * @param {enum v4l2_crop} *crop
 * @return {*}
 */
S32 mpp_v4l2_get_crop(S32 fd, struct v4l2_crop *crop);

/**
 * @description:
 * @param {S32} fd
 * @param {v4l2_buffer} *buf
 * @param {U8} *user_ptr
 * @return {*}
 */
S32 mpp_v4l2_map_memory(S32 fd, const struct v4l2_buffer *buf, U8 *user_ptr[8]);

/**
 * @description:
 * @param {v4l2_buffer} *buf
 * @param {U8} *user_ptr
 * @return {*}
 */
void mpp_v4l2_unmap_memory(const struct v4l2_buffer *buf, U8 *user_ptr[8]);

/**
 * @description:
 * @param {v4l2_buffer} *p
 * @return {*}
 */
void show_buffer_info(const struct v4l2_buffer *p);

#endif /*_MPP_V4L2_UTILS_H_*/
