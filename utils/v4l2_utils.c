/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 15:35:41
 * @LastEditTime: 2023-11-28 11:26:18
 * @Description: some V4L2 utils
 */

#define MODULE_TAG "mpp_v4l2_utils"
#define ENABLE_DEBUG 0

#include "v4l2_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

#define MAX_VIDEO_NODE_NUM 32

#define V4L2_IS_M2M(_dcaps)                                                    \
    (((_dcaps) & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE)) ||          \
        (((_dcaps) & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) && \
        ((_dcaps) & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE))))

#define V4L2_IS_OPEN(video_fd) ((video_fd) > 0)
#define V4L2_CHECK_OPEN(video_fd)                          \
    if (!V4L2_IS_OPEN(video_fd)) {                         \
        error("video_fd is NOT opened, please check it!"); \
        return MPP_FALSE;                                  \
    }

#define V4L2_CHECK_NOT_OPEN(v4l2object)                \
    if (V4L2_IS_OPEN(v4l2object)) {                    \
        error("video_fd is opened, please check it!"); \
        return MPP_FALSE;                              \
    }

/**
 * @description: check whether the V4L2 output is stream(H264 etc.)
 * @param {S32} video_fd : the opened device fd
 * @return {*}
 */
BOOL check_output_is_stream(S32 video_fd) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break; /* end of enumeration */
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_MJPEG:
            case V4L2_PIX_FMT_JPEG:
            case V4L2_PIX_FMT_MPEG:
            case V4L2_PIX_FMT_H264:
            case V4L2_PIX_FMT_H264_NO_SC:
            case V4L2_PIX_FMT_H264_MVC:
            case V4L2_PIX_FMT_MPEG1:
            case V4L2_PIX_FMT_MPEG2:
            case V4L2_PIX_FMT_MPEG2_SLICE:
            case V4L2_PIX_FMT_MPEG4:
            case V4L2_PIX_FMT_VP8:
            case V4L2_PIX_FMT_VP9:
            case V4L2_PIX_FMT_HEVC:
            case V4L2_PIX_FMT_FWHT:
            case V4L2_PIX_FMT_FWHT_STATELESS:
                check_result = MPP_TRUE;
                break;
            default:
                break;
        }
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break; /* end of enumeration */
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_MJPEG:
            case V4L2_PIX_FMT_JPEG:
            case V4L2_PIX_FMT_MPEG:
            case V4L2_PIX_FMT_H264:
            case V4L2_PIX_FMT_H264_NO_SC:
            case V4L2_PIX_FMT_H264_MVC:
            case V4L2_PIX_FMT_MPEG1:
            case V4L2_PIX_FMT_MPEG2:
            case V4L2_PIX_FMT_MPEG2_SLICE:
            case V4L2_PIX_FMT_MPEG4:
            case V4L2_PIX_FMT_VP8:
            case V4L2_PIX_FMT_VP9:
            case V4L2_PIX_FMT_HEVC:
            case V4L2_PIX_FMT_FWHT:
            case V4L2_PIX_FMT_FWHT_STATELESS:
                check_result = MPP_TRUE;
                break;
            default:
                break;
        }
    }

    debug("================= check output is stream finish! result = %d", check_result);
    return check_result;
}

BOOL check_output_is_frame(S32 video_fd) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    debug("check_output_is_frame");

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=======================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_NV12:
            case V4L2_PIX_FMT_NV21:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_YVU420:
                check_result = MPP_TRUE;
                break;
            default:
                debug("pixelformat(%u) is not supported", format.pixelformat);
                break;
        }
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=======================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_NV12:
            case V4L2_PIX_FMT_NV21:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_YVU420:
                check_result = MPP_TRUE;
                break;
            default:
                debug("pixelformat(%u) is not supported", format.pixelformat);
                break;
        }
    }

    debug("================= check output is frame finish! result = %d", check_result);
    return check_result;
}

BOOL check_output_format(S32 video_fd, U32 fmt) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    debug("================= check output format! fmt = %d", fmt);

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        if (fmt == format.pixelformat)
            check_result = MPP_TRUE;
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        if (fmt == format.pixelformat)
            check_result = MPP_TRUE;
    }

    debug("================= check output formatfinish! result = %d", check_result);
    return check_result;
}

BOOL check_input_is_stream(S32 video_fd) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    debug("check_input_is_stream");

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=======================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_MJPEG:
            case V4L2_PIX_FMT_JPEG:
            case V4L2_PIX_FMT_MPEG:
            case V4L2_PIX_FMT_H264:
            case V4L2_PIX_FMT_H264_NO_SC:
            case V4L2_PIX_FMT_H264_MVC:
            case V4L2_PIX_FMT_MPEG1:
            case V4L2_PIX_FMT_MPEG2:
            case V4L2_PIX_FMT_MPEG2_SLICE:
            case V4L2_PIX_FMT_MPEG4:
            case V4L2_PIX_FMT_VP8:
            case V4L2_PIX_FMT_VP9:
            case V4L2_PIX_FMT_HEVC:
            case V4L2_PIX_FMT_FWHT:
            case V4L2_PIX_FMT_FWHT_STATELESS:
                check_result = MPP_TRUE;
                break;
            default:
                debug("pixelformat(%u) is not supported", format.pixelformat);
                break;
        }
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=======================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_MJPEG:
            case V4L2_PIX_FMT_JPEG:
            case V4L2_PIX_FMT_MPEG:
            case V4L2_PIX_FMT_H264:
            case V4L2_PIX_FMT_H264_NO_SC:
            case V4L2_PIX_FMT_H264_MVC:
            case V4L2_PIX_FMT_MPEG1:
            case V4L2_PIX_FMT_MPEG2:
            case V4L2_PIX_FMT_MPEG2_SLICE:
            case V4L2_PIX_FMT_MPEG4:
            case V4L2_PIX_FMT_VP8:
            case V4L2_PIX_FMT_VP9:
            case V4L2_PIX_FMT_HEVC:
            case V4L2_PIX_FMT_FWHT:
            case V4L2_PIX_FMT_FWHT_STATELESS:
                check_result = MPP_TRUE;
                break;
            default:
                debug("pixelformat(%u) is not supported", format.pixelformat);
                break;
        }
    }

    debug("================= check input is stream finish! result = %d", check_result);
    return check_result;
}

BOOL check_input_is_frame(S32 video_fd) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    debug("check_input_is_frame");

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_NV12:
            case V4L2_PIX_FMT_NV21:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_YVU420:
                check_result = MPP_TRUE;
                break;
            default:
                break;
        }
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        switch (format.pixelformat) {
            case V4L2_PIX_FMT_NV12:
            case V4L2_PIX_FMT_NV21:
            case V4L2_PIX_FMT_YUV420:
            case V4L2_PIX_FMT_YVU420:
                check_result = MPP_TRUE;
                break;
            default:
                break;
        }
    }

    debug("================= check input is frame finish! result = %d", check_result);
    return check_result;
}

BOOL check_input_format(S32 video_fd, U32 fmt) {
    BOOL check_result = MPP_FALSE;
    struct v4l2_fmtdesc format;

    debug("check input format! fmt = %d", fmt);

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        if (fmt == format.pixelformat)
            check_result = MPP_TRUE;
    }

    for (S32 i = 0;; i++) {
        memset(&format, 0, sizeof(format));
        format.index = i;
        format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

        if (ioctl(video_fd, VIDIOC_ENUM_FMT, &format) < 0) {
            break;
        }

        debug("=================================");
        debug("index:       %u", format.index);
        debug("type:        %d", format.type);
        debug("flags:       %08x", format.flags);
        debug("description: %s", format.description);
        debug("pixelformat: %d", format.pixelformat);

        if (fmt == format.pixelformat)
            check_result = MPP_TRUE;
    }

    debug("check input format finish! result = %d", check_result);
    return check_result;
}

/**
 * @description: Probe /dev/video* for an M2M V4L2 codec (decoder or encoder).
 *               Used by module.c before dlopen of the standard V4L2 plugin.
 */
BOOL check_v4l2(void) {
    S32 video_fd = -1;
    struct v4l2_capability vcap;
    U32 device_caps;

    debug("Start probing devices");

    for (S32 i = 0; i < MAX_VIDEO_NODE_NUM; i++) {
        if (video_fd >= 0) {
            close(video_fd);
            video_fd = -1;
        }

        U8 path_base[12] = "/dev/video";
        U8 device_path[20];
        snprintf((char *)device_path, sizeof(device_path), "%s%d", (char *)path_base, i);
        debug("Now open '%s' and check it!", device_path);

        video_fd = open((const char *)device_path, O_RDWR | O_CLOEXEC);

        if (video_fd == -1) {
            error("Can not open '%s', please check it! (%s)", device_path, strerror(errno));
            continue;
        }

        memset(&vcap, 0, sizeof(vcap));

        if (ioctl(video_fd, VIDIOC_QUERYCAP, &vcap) < 0) {
            error("Can not get device capabilities, please check it ! (%s)", strerror(errno));
            continue;
        }

        if (vcap.capabilities & V4L2_CAP_DEVICE_CAPS) {
            device_caps = vcap.device_caps;
        } else {
            device_caps = vcap.capabilities;
        }

        if (!V4L2_IS_M2M(device_caps)) {
            continue;
        }

        debug("Probing '%s' located at '%s'", (const char *)vcap.driver, (const char *)device_path);

        if ((check_input_is_stream(video_fd) && check_output_is_frame(video_fd)) ||
            (check_input_is_frame(video_fd) && check_output_is_stream(video_fd))) {
            close(video_fd);
            video_fd = -1;
            return MPP_TRUE;
        }
    }

    if (video_fd >= 0) {
        close(video_fd);
        video_fd = -1;
    }
    return MPP_FALSE;
}

/**
 * @description: find the video device which can do the decode we need
 * @param {U8*} device_path : output, the device path, such as /dev/video0.
 * @param {S32} coding_type : input, the stream format we need to decode
 * @return {S32} : video device fd
 */
#define V4L2_DEVICE_PATH_LEN 20

S32 find_v4l2_decoder(U8 *device_path, S32 coding_type) {
    S32 video_fd = -1;
    struct v4l2_capability vcap;
    U32 device_caps;
    U8 path_base[12] = "/dev/video";

    debug("find V4L2 Decoder");

    for (S32 i = 0; i < MAX_VIDEO_NODE_NUM; i++) {
        if (video_fd >= 0) {
            close(video_fd);
            video_fd = -1;
        }

        snprintf((char *)device_path, V4L2_DEVICE_PATH_LEN, "%s%d", (char *)path_base, i);
        debug("now open '%s' and check it!", device_path);
        video_fd = open((const char *)device_path, O_RDWR | O_CLOEXEC);

        if (video_fd == -1) {
            error("can not open '%s', please check it! (%s)", device_path, strerror(errno));
            continue;
        }

        memset(&vcap, 0, sizeof(vcap));

        if (ioctl(video_fd, VIDIOC_QUERYCAP, &vcap) < 0) {
            error("can not get device capabilities, please check it ! (%s)", strerror(errno));
            continue;
        }

        error("v4l2_capability.capabilities = %u, v4l2_capability.device_caps = %u",
            vcap.capabilities, vcap.device_caps);

        if (vcap.capabilities & V4L2_CAP_DEVICE_CAPS) {
            device_caps = vcap.device_caps;
        } else {
            device_caps = vcap.capabilities;
        }

        if (!V4L2_IS_M2M(device_caps)) {
            error("%s is not a M2M device (caps=0x%08x), skip", device_path, device_caps);
            continue;
        }

        debug("Probing '%s' located at '%s'", (const char *)vcap.driver, (const char *)device_path);

        if (check_input_is_stream(video_fd) && check_output_is_frame(video_fd) &&
            check_input_format(video_fd, coding_type)) {
            return video_fd;
        }
    }

    if (video_fd >= 0) {
        close(video_fd);
        video_fd = -1;
    }
    return -1;
}

/**
 * @description: find the video device which can do the encode we need
 * @param {U8*} device_path : output, the device path, such as /dev/video0.
 * @param {S32} coding_type : the stream format we need to encode
 * @return {S32} : video device fd
 */
S32 find_v4l2_encoder(U8 *device_path, S32 coding_type) {
    S32 video_fd = -1;
    struct v4l2_capability vcap;
    U32 device_caps;
    U8 path_base[12] = "/dev/video";

    debug("find V4L2 Encoder");

    for (S32 i = 0; i < MAX_VIDEO_NODE_NUM; i++) {
        if (video_fd >= 0) {
            close(video_fd);
            video_fd = -1;
        }

        snprintf((char *)device_path, V4L2_DEVICE_PATH_LEN, "%s%d", (char *)path_base, i);
        debug("now open '%s' and check it!", device_path);
        video_fd = open((const char *)device_path, O_RDWR | O_CLOEXEC);

        if (video_fd == -1) {
            error("can not open '%s', please check it! (%s)", device_path, strerror(errno));
            continue;
        }

        memset(&vcap, 0, sizeof(vcap));

        if (ioctl(video_fd, VIDIOC_QUERYCAP, &vcap) < 0) {
            error("can not get device capabilities, please check it ! (%s)", strerror(errno));
            continue;
        }

        if (vcap.capabilities & V4L2_CAP_DEVICE_CAPS) {
            device_caps = vcap.device_caps;
        } else {
            device_caps = vcap.capabilities;
        }

        if (!V4L2_IS_M2M(device_caps)) {
            continue;
        }

        debug("Probing '%s' located at '%s'", (const char *)vcap.driver, (const char *)device_path);

        if (check_input_is_frame(video_fd) && check_output_is_stream(video_fd) &&
            check_output_format(video_fd, coding_type)) {
            return video_fd;
        }
    }

    if (video_fd >= 0) {
        close(video_fd);
        video_fd = -1;
    }
    return -1;
}

static S32 ioctl_handler(S32 fd, uintptr_t req, void *data) {
    S32 ret = ioctl(fd, req, data);
    if (0 != ret) {
        error("=====> IOCTL ERROR, ret = %d, req = %08" PRIxPTR " (%s)", ret, (uintptr_t)req, strerror(errno));
    }
    return ret;
}

S32 mpp_v4l2_set_ctrl(S32 fd, S32 id, S32 val) {
    struct v4l2_control ctrl;

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = val;

    return ioctl_handler(fd, VIDIOC_S_CTRL, &ctrl);
}

S32 mpp_v4l2_get_format(S32 fd, struct v4l2_format *fmt, enum v4l2_buf_type buf_type) {
    fmt->type = buf_type;
    return ioctl_handler(fd, VIDIOC_G_FMT, fmt);
}

S32 mpp_v4l2_set_format(S32 fd, struct v4l2_format *fmt) {
    return ioctl_handler(fd, VIDIOC_S_FMT, fmt);
}

S32 mpp_v4l2_try_format(S32 fd, struct v4l2_format *fmt) {
    return ioctl_handler(fd, VIDIOC_TRY_FMT, fmt);
}

S32 mpp_v4l2_subscribe_event(S32 fd, struct v4l2_event_subscription *sub) {
    return ioctl_handler(fd, VIDIOC_SUBSCRIBE_EVENT, sub);
}

S32 mpp_v4l2_req_buffers(S32 fd, struct v4l2_requestbuffers *reqbuf) {
    return ioctl_handler(fd, VIDIOC_REQBUFS, reqbuf);
}

S32 mpp_v4l2_query_buffer(S32 fd, struct v4l2_buffer *buf) {
    return ioctl_handler(fd, VIDIOC_QUERYBUF, buf);
}

S32 mpp_v4l2_queue_buffer(S32 fd, struct v4l2_buffer *buf) {
    return ioctl_handler(fd, VIDIOC_QBUF, buf);
}

S32 mpp_v4l2_dequeue_buffer(S32 fd, struct v4l2_buffer *buf) {
    return ioctl_handler(fd, VIDIOC_DQBUF, buf);
}

S32 mpp_v4l2_stream_on(S32 fd, enum v4l2_buf_type *type) {
    return ioctl_handler(fd, VIDIOC_STREAMON, type);
}

S32 mpp_v4l2_stream_off(S32 fd, enum v4l2_buf_type *type) {
    return ioctl_handler(fd, VIDIOC_STREAMOFF, type);
}

S32 mpp_v4l2_get_crop(S32 fd, struct v4l2_crop *crop) {
    return ioctl_handler(fd, VIDIOC_G_CROP, crop);
}

S32 mpp_v4l2_map_memory(S32 fd, const struct v4l2_buffer *buf, U8 *user_ptr[8]) {
    if (V4L2_TYPE_IS_MULTIPLANAR(buf->type)) {
        for (U32 i = 0; i < buf->length; ++i) {
            struct v4l2_plane *p = &buf->m.planes[i];
            if (p->length > 0) {
                user_ptr[i] = mmap(NULL, p->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, p->m.mem_offset);
                if (user_ptr[i] == MAP_FAILED) {
                    error("Failed to mmap multi plane memory (%s)", strerror(errno));
                    return MPP_MMAP_FAILED;
                }
            }
        }
    } else {
        if (buf->length > 0) {
            user_ptr[0] = mmap(NULL, buf->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf->m.offset);
            if (user_ptr[0] == MAP_FAILED) {
                error("Failed to mmap single plane memory (%s)", strerror(errno));
                return MPP_MMAP_FAILED;
            }
        }
    }
    return MPP_OK;
}

void mpp_v4l2_unmap_memory(const struct v4l2_buffer *buf, U8 *user_ptr[8]) {
    if (V4L2_TYPE_IS_MULTIPLANAR(buf->type)) {
        for (U32 i = 0; i < buf->length; ++i) {
            if (user_ptr[i] != 0) {
                munmap(user_ptr[i], buf->m.planes[i].length);
            }
        }
    } else {
        if (user_ptr[0]) {
            munmap(user_ptr[0], buf->length);
        }
    }
}

void show_buffer_info(const struct v4l2_buffer *p) {
    debug("======= v4l2_buffer =====");
    debug("index:             %d", p->index);
    debug("type:              %d", p->type);
    debug("bytesused:         %d", p->bytesused);
    debug("flags:             0x%08x", p->flags);
    debug("timeval sec:       %" PRId64, (int64_t)p->timestamp.tv_sec);
    debug("timeval usec:      %" PRId64, (int64_t)p->timestamp.tv_usec);
    debug("timecode type:     %d", p->timecode.type);
    debug("timecode flags:    0x%08x", p->timecode.flags);
    debug("timecode frames:   %d", p->timecode.frames);
    debug("timecode seconds:  %d", p->timecode.seconds);
    debug("timecode minutes:  %d", p->timecode.minutes);
    debug("timecode hours:    %d", p->timecode.hours);
    debug("sequence:          %d", p->sequence);
    debug("length:            %d", p->length);
    debug("reserved2:         %08x", p->reserved2);
}

/**
 * @description: Same role as check_v4l2 for Linlon V5V7–style M2M devices.
 */
BOOL check_v4l2_linlonv5v7(void) {
    S32 video_fd = -1;
    struct v4l2_capability vcap;
    U32 device_caps;

    debug("check_v4l2_linlonv5v7");

    for (S32 i = 0; i < MAX_VIDEO_NODE_NUM; i++) {
        if (video_fd >= 0) {
            close(video_fd);
            video_fd = -1;
        }

        U8 path_base[12] = "/dev/video";
        U8 device_path[20];
        snprintf((char *)device_path, sizeof(device_path), "%s%d", (char *)path_base, i);
        debug("Now open '%s' and check it!", device_path);

        video_fd = open((const char *)device_path, O_RDWR | O_CLOEXEC);

        if (video_fd == -1) {
            error("Can not open '%s', please check it! (%s)", device_path, strerror(errno));
            continue;
        }

        memset(&vcap, 0, sizeof(vcap));

        if (ioctl(video_fd, VIDIOC_QUERYCAP, &vcap) < 0) {
            error("Can not get device capabilities, please check it ! (%s)", strerror(errno));
            continue;
        }

        debug("driver = (%s)", vcap.driver);

        if (vcap.capabilities & V4L2_CAP_DEVICE_CAPS) {
            device_caps = vcap.device_caps;
        } else {
            device_caps = vcap.capabilities;
        }

        /* Skip obvious non-M2M devices (CAPTURE-only, e.g. VI nodes) to
         * avoid wasting time probing them.  Pure M2M devices may or may not
         * have V4L2_CAP_VIDEO_M2M set depending on kernel version, so fall
         * through to the format probe when caps are ambiguous. */
        if ((device_caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) &&
            !(device_caps & (V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_VIDEO_OUTPUT_MPLANE))) {
            debug("%s is capture-only (caps=0x%08x), skip", device_path, device_caps);
            continue;
        }

        debug("Probing '%s' located at '%s'", (const char *)vcap.driver, (const char *)device_path);

        if ((check_input_is_stream(video_fd) && check_output_is_frame(video_fd)) ||
            (check_input_is_frame(video_fd) && check_output_is_stream(video_fd))) {
            close(video_fd);
            video_fd = -1;
            return MPP_TRUE;
        }
    }

    if (video_fd >= 0) {
        close(video_fd);
        video_fd = -1;
    }
    return MPP_FALSE;
}
