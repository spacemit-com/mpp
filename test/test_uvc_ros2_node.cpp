/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be found in
 * the LICENSE file.
 *
 * @File      :    test_uvc_ros2_node.cpp
 * @Date      :    2026-05-12
 * @Brief     :    ROS2 node example for UVC camera capture and MPP zero-copy publish.
 *
 * Data path:
 *   - Captures frames from a UVC device using the MPP UVC module.
 *   - Exports the underlying VB buffer via VB_Export.
 *   - Publishes an mpp_msgs/msg/MppVideoFrameDesc message for zero-copy consumers.
 *------------------------------------------------------------------------------
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "mpp_msgs/msg/mpp_video_frame_desc.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

extern "C" {
#include "sys_api.h"
#include "vb_api.h"
#include "uvc_api.h"
}

class UvcCameraNode final : public rclcpp::Node {
public:
    UvcCameraNode() : Node("mpp_uvc_camera_node") {
        devNode_ = declare_parameter<std::string>("uvc_device", "/dev/video0");
        width_ = declare_parameter<int>("width", 1280);
        height_ = declare_parameter<int>("height", 720);
        fps_ = declare_parameter<int>("fps", 30);
        frameId_ = declare_parameter<std::string>("frame_id", "uvc_camera");
        publishMode_ = declare_parameter<std::string>("publish_mode", "mpp_zero_copy");
        topicName_ = declare_parameter<std::string>("topic", "mpp_frame_desc");
        copyTopicName_ = declare_parameter<std::string>("copy_topic", "image_raw");
        maxExportedFrames_ = declare_parameter<int>("max_exported_frames", 8);

        if (publishMode_ == "copy") {
            imagePublisher_ = create_publisher<sensor_msgs::msg::Image>(copyTopicName_, rclcpp::SensorDataQoS());
            RCLCPP_WARN(
                get_logger(), "publish_mode=copy uses memcpy into sensor_msgs/Image; this is not MPP zero-copy");
        } else if (publishMode_ == "mpp_zero_copy") {
            descPublisher_ = create_publisher<mpp_msgs::msg::MppVideoFrameDesc>(topicName_, rclcpp::SensorDataQoS());
        } else {
            throw std::runtime_error("publish_mode must be mpp_zero_copy or copy");
        }

        if (!open_camera()) {
            throw std::runtime_error("failed to open UVC camera");
        }

        auto period = std::chrono::milliseconds(1000 / (fps_ > 0 ? fps_ : 30));
        timer_ = create_wall_timer(period, std::bind(&UvcCameraNode::capture_once, this));
        RCLCPP_INFO(
            get_logger(),
            "MPP UVC camera node started: device=%s %dx%d@%d mode=%s topic=%s",
            devNode_.c_str(),
            width_,
            height_,
            fps_,
            publishMode_.c_str(),
            topicName_.c_str());
    }

    ~UvcCameraNode() override { close_camera(); }

private:
    bool open_camera() {
        S32 ret;
        UvcDevAttr devAttr{};
        UvcChnAttr chnAttr{};

        ret = SYS_Init();
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "SYS_Init failed: %d", ret);
            return false;
        }
        sysInited_ = true;

        ret = VB_Init();
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VB_Init failed: %d", ret);
            close_camera();
            return false;
        }
        vbInited_ = true;

        ret = UVC_Init();
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_Init failed: %d", ret);
            close_camera();
            return false;
        }
        uvcInited_ = true;

        strncpy(devAttr.acDevNode, devNode_.c_str(), sizeof(devAttr.acDevNode) - 1);
        devAttr.acDevNode[sizeof(devAttr.acDevNode) - 1] = '\0';

        ret = UVC_CreateDev(0, &devAttr);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_CreateDev failed: %d", ret);
            close_camera();
            return false;
        }
        uvcDevCreated_ = true;

        ret = UVC_EnableDev(0);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_EnableDev failed: %d", ret);
            close_camera();
            return false;
        }
        uvcDevEnabled_ = true;

        chnAttr.u32Width = static_cast<U32>(width_);
        chnAttr.u32Height = static_cast<U32>(height_);
        chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
        chnAttr.u32Fps = static_cast<U32>(fps_);
        chnAttr.u32Depth = 1;

        ret = UVC_SetChnAttr(0, 0, &chnAttr);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_SetChnAttr failed: %d", ret);
            close_camera();
            return false;
        }

        ret = UVC_EnableChn(0, 0);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_EnableChn failed: %d", ret);
            close_camera();
            return false;
        }
        uvcChnEnabled_ = true;
        return true;
    }

    void close_camera() {
        release_exported_frames(true);

        if (uvcChnEnabled_) {
            (void)UVC_DisableChn(0, 0);
            uvcChnEnabled_ = false;
        }
        if (uvcDevEnabled_) {
            (void)UVC_DisableDev(0);
            uvcDevEnabled_ = false;
        }
        if (uvcDevCreated_) {
            (void)UVC_DestroyDev(0);
            uvcDevCreated_ = false;
        }
        if (uvcInited_) {
            (void)UVC_Exit();
            uvcInited_ = false;
        }
        if (vbInited_) {
            (void)VB_Exit();
            vbInited_ = false;
        }
        if (sysInited_) {
            (void)SYS_Exit();
            sysInited_ = false;
        }
    }

    void capture_once() {
        VideoFrameInfo frame{};
        S32 ret = UVC_GetFrame(0, 0, &frame, 1000);
        if (ret != 0) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "UVC_GetFrame failed: %d", ret);
            return;
        }

        if (publishMode_ == "copy") {
            publish_nv12_copy(frame);
        } else {
            publish_mpp_zero_copy(frame);
        }

        ret = UVC_ReleaseFrame(0, 0, &frame);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "UVC_ReleaseFrame failed: %d", ret);
        }

        release_exported_frames(false);
    }

    struct ExportedFrame {
        UL bufferId{0};
        U64 token{0};
    };

    void publish_mpp_zero_copy(const VideoFrameInfo &frame) {
        U64 token = 0;
        S32 ret = VB_Export(frame.ulBufferId, &token);
        if (ret != 0) {
            RCLCPP_ERROR(
                get_logger(), "VB_Export buffer=0x%" PRIx64 " failed: %d",
                static_cast<uint64_t>(frame.ulBufferId), ret);
            return;
        }

        exportedFrames_.push_back(ExportedFrame{frame.ulBufferId, token});

        const auto &common = frame.stCommFrameInfo;
        const auto &vf = frame.stVFrame;
        auto msg = mpp_msgs::msg::MppVideoFrameDesc();
        msg.header.stamp = now();
        msg.header.frame_id = frameId_;
        msg.version = 1;
        msg.token = token;
        msg.pool_id = static_cast<uint64_t>(frame.ulPoolId);
        msg.buffer_id = static_cast<uint64_t>(frame.ulBufferId);
        msg.frame_index = frame.u32Idx;
        msg.width = common.u32Width;
        msg.height = common.u32Height;
        msg.align = common.u32Align;
        msg.pixel_format = common.ePixelFormat;
        msg.compress_mode = common.eCompressMode;
        msg.color_space = common.eColorSpace;
        msg.pts = vf.u64PTS;
        msg.total_size = vf.u32TotalSize;
        msg.plane_num = vf.u32PlaneNum;

        for (size_t i = 0; i < vf.u32PlaneNum && i < msg.plane_stride.size(); ++i) {
            msg.plane_stride[i] = vf.u32PlaneStride[i];
            msg.plane_size[i] = vf.u32PlaneSize[i];
            msg.plane_size_valid[i] = vf.u32PlaneSizeValid[i];
            msg.plane_phy_addr[i] = vf.u64PlanePhyAddr[i];
        }

        descPublisher_->publish(std::move(msg));
    }

    void publish_nv12_copy(const VideoFrameInfo &frame) {
        if (frame.stVFrame.u32PlaneNum < 2) {
            RCLCPP_WARN(get_logger(), "UVC frame plane count %u not supported for copy", frame.stVFrame.u32PlaneNum);
            return;
        }

        sensor_msgs::msg::Image msg;
        msg.header.stamp = now();
        msg.header.frame_id = frameId_;
        msg.height = static_cast<uint32_t>(frame.stCommFrameInfo.u32Height);
        msg.width = static_cast<uint32_t>(frame.stCommFrameInfo.u32Width);
        msg.encoding = "nv12";
        msg.is_bigendian = false;
        msg.step = frame.stVFrame.u32PlaneStride[0];
        size_t ySize = frame.stVFrame.u32PlaneSizeValid[0];
        size_t uvSize = frame.stVFrame.u32PlaneSizeValid[1];
        msg.data.resize(ySize + uvSize);
        memcpy(
            msg.data.data(),
            reinterpret_cast<const void *>(static_cast<uintptr_t>(frame.stVFrame.ulPlaneVirAddr[0])),
            ySize);
        memcpy(
            msg.data.data() + ySize,
            reinterpret_cast<const void *>(static_cast<uintptr_t>(frame.stVFrame.ulPlaneVirAddr[1])),
            uvSize);
        imagePublisher_->publish(std::move(msg));
    }

    void release_exported_frames(bool releaseAll) {
        const int keepCount = maxExportedFrames_ > 0 ? maxExportedFrames_ : 1;
        while (!exportedFrames_.empty() && (releaseAll || static_cast<int>(exportedFrames_.size()) > keepCount)) {
            const auto exported = exportedFrames_.front();
            exportedFrames_.pop_front();
            (void)VB_Unexport(exported.bufferId);
        }
    }

private:
    std::string devNode_{"/dev/video0"};
    int width_{1280};
    int height_{720};
    int fps_{30};
    std::string frameId_{"uvc_camera"};
    std::string publishMode_{"mpp_zero_copy"};
    std::string topicName_{"mpp_frame_desc"};
    std::string copyTopicName_{"image_raw"};
    int maxExportedFrames_{8};

    bool sysInited_{false};
    bool vbInited_{false};
    bool uvcInited_{false};
    bool uvcDevCreated_{false};
    bool uvcDevEnabled_{false};
    bool uvcChnEnabled_{false};

    std::deque<ExportedFrame> exportedFrames_;
    rclcpp::Publisher<mpp_msgs::msg::MppVideoFrameDesc>::SharedPtr descPublisher_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr imagePublisher_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<UvcCameraNode>());
    rclcpp::shutdown();
    return 0;
}
