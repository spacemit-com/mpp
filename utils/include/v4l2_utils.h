/*
* Copyright 2022-2023 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*/

#ifndef V4L2_UTILS_H
#define V4L2_UTILS_H

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

BOOL check_v4l2();
BOOL check_v4l2_linlonv5v7();
S32 find_v4l2_decoder(U8 *device_path, S32 coding_type);
S32 find_v4l2_encoder(U8 *device_path, S32 coding_type);
S32 mpp_v4l2_set_ctrl(S32 fd, S32 id, S32 val);
S32 mpp_v4l2_get_format(S32 fd, struct v4l2_format *fmt,
    enum v4l2_buf_type buf_type);
S32 mpp_v4l2_set_format(S32 fd, struct v4l2_format *fmt);
S32 mpp_v4l2_try_format(S32 fd, struct v4l2_format *fmt);
S32 mpp_v4l2_subscribe_event(S32 fd, struct v4l2_event_subscription *sub);
S32 mpp_v4l2_req_buffers(S32 fd, struct v4l2_requestbuffers *reqbuf);
S32 mpp_v4l2_query_buffer(S32 fd, struct v4l2_buffer *buf);
S32 mpp_v4l2_queue_buffer(S32 fd, struct v4l2_buffer *buf);
S32 mpp_v4l2_dequeue_buffer(S32 fd, struct v4l2_buffer *buf);
S32 mpp_v4l2_stream_on(S32 fd, enum v4l2_buf_type *type);
S32 mpp_v4l2_stream_off(S32 fd, enum v4l2_buf_type *type);
S32 mpp_v4l2_get_crop(S32 fd, struct v4l2_crop *crop);
S32 mpp_v4l2_map_memory(S32 fd, const struct v4l2_buffer *buf, U8 *user_ptr[8]);
void mpp_v4l2_unmap_memory(const struct v4l2_buffer *buf, U8 *user_ptr[8]);
void show_buffer_info(const struct v4l2_buffer *p);

#endif /* V4L2_UTILS_H */
