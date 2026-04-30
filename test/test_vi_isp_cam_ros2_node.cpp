/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    test_vi_isp_cam_ros2_node.cpp
 * @Date      :    2026-04-28
 * @Brief     :    ROS2 node example for ISP camera capture.
 *
 * Build note:
 *   This example is not added to the default CMake target because ROS2 may not
 *   be installed in the BSP build environment. Copy this file into a ROS2
 *   package, build test/ros2_mpp_msgs as the mpp_msgs interface package, and
 *   link this node with mpp_vi and mpp_sys.
 *
 * Data path:
 *   The default mode publishes an MPP VB descriptor only. The image payload stays
 *   in the MPP SYS/VB dma-buf buffer and can be imported by another process for
 *   zero-copy processing. Use publish_mode:=copy only when a standard ROS2
 *   sensor_msgs/Image stream is required for compatibility.
 *------------------------------------------------------------------------------
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>

#include "mpp_msgs/msg/mpp_video_frame_desc.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

extern "C" {
#include "sys_api.h"
#include "vb_api.h"
#include "vi_api.h"
}

class IspCameraNode final : public rclcpp::Node {
public:
    IspCameraNode()
        : Node("mpp_isp_camera_node")
    {
        viDev_ = static_cast<VI_DEV>(declare_parameter<int>("vi_dev", 0));
        viChn_ = static_cast<VI_CHN>(declare_parameter<int>("vi_chn", 0));
        sensorWidth_ = declare_parameter<int>("sensor_width", 3864);
        sensorHeight_ = declare_parameter<int>("sensor_height", 2192);
        width_ = declare_parameter<int>("width", 1920);
        height_ = declare_parameter<int>("height", 1080);
        mipiLanes_ = declare_parameter<int>("mipi_lanes", 4);
        mbps_ = declare_parameter<int>("mbps", 800);
        fps_ = declare_parameter<int>("fps", 30);
        frameId_ = declare_parameter<std::string>("frame_id", "camera");
        publishMode_ = declare_parameter<std::string>("publish_mode", "mpp_zero_copy");
        topicName_ = declare_parameter<std::string>("topic", "mpp_frame_desc");
        copyTopicName_ = declare_parameter<std::string>("copy_topic", "image_raw");
        maxExportedFrames_ = declare_parameter<int>("max_exported_frames", 8);

        if (publishMode_ == "copy") {
            imagePublisher_ = create_publisher<sensor_msgs::msg::Image>(copyTopicName_, rclcpp::SensorDataQoS());
            RCLCPP_WARN(get_logger(), "publish_mode=copy uses memcpy into sensor_msgs/Image; this is not MPP zero-copy");
        } else if (publishMode_ == "mpp_zero_copy") {
            descPublisher_ = create_publisher<mpp_msgs::msg::MppVideoFrameDesc>(topicName_, rclcpp::SensorDataQoS());
        } else {
            throw std::runtime_error("publish_mode must be mpp_zero_copy or copy");
        }

        if (!open_camera()) {
            throw std::runtime_error("failed to open MPP ISP camera");
        }

        auto period = std::chrono::milliseconds(1000 / (fps_ > 0 ? fps_ : 30));
        timer_ = create_wall_timer(period, std::bind(&IspCameraNode::capture_once, this));
        RCLCPP_INFO(get_logger(), "MPP ISP camera node started: %dx%d mode=%s topic=%s",
                    width_, height_, publishMode_.c_str(), topicName_.c_str());
    }

    ~IspCameraNode() override
    {
        close_camera();
    }

private:
    bool open_camera()
    {
        S32 ret;
        ViDevAttrS devAttr{};
        ViChnAttrS chnAttr{};

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

        ret = VI_Init();
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_Init failed: %d", ret);
            close_camera();
            return false;
        }
        viInited_ = true;

        devAttr.eWorkMode = VI_WORK_MODE_ONLINE;
        devAttr.u32Width = static_cast<U32>(sensorWidth_);
        devAttr.u32Height = static_cast<U32>(sensorHeight_);
        devAttr.u32MipiLaneNum = static_cast<U32>(mipiLanes_);
        devAttr.u32mbps = static_cast<U32>(mbps_);
        devAttr.bCapture2Preview = MPP_FALSE;
        ret = VI_SetDevAttr(viDev_, &devAttr);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_SetDevAttr failed: %d", ret);
            close_camera();
            return false;
        }

        chnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
        chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
        chnAttr.u32Width = static_cast<U32>(width_);
        chnAttr.u32Height = static_cast<U32>(height_);
        chnAttr.eRotateMode = VI_ROT_0;
        chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
        ret = VI_SetChnAttr(viDev_, viChn_, &chnAttr);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_SetChnAttr failed: %d", ret);
            close_camera();
            return false;
        }

        ret = VI_EnableDev(viDev_);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_EnableDev failed: %d", ret);
            close_camera();
            return false;
        }
        devEnabled_ = true;

        ret = VI_EnableChn(viDev_, viChn_);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_EnableChn failed: %d", ret);
            close_camera();
            return false;
        }
        chnEnabled_ = true;
        return true;
    }

    void close_camera()
    {
        release_exported_frames(true);

        if (chnEnabled_) {
            (void)VI_DisableChn(viDev_, viChn_);
            chnEnabled_ = false;
        }
        if (devEnabled_) {
            (void)VI_DisableDev(viDev_);
            devEnabled_ = false;
        }
        if (viInited_) {
            (void)VI_DeInit();
            viInited_ = false;
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

    void capture_once()
    {
        VideoFrameInfo frame{};
        S32 ret = VI_GetChnFrame(viDev_, viChn_, &frame, 1000);
        if (ret != 0) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "VI_GetChnFrame failed: %d", ret);
            return;
        }

        if (publishMode_ == "copy") {
            publish_nv12_copy(frame);
        } else {
            publish_mpp_zero_copy(frame);
        }

        ret = VI_ReleaseChnFrame(viDev_, viChn_, &frame);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VI_ReleaseChnFrame failed: %d", ret);
        }

        release_exported_frames(false);
    }

    struct ExportedFrame {
        UL bufferId{0};
        U64 token{0};
    };

    void publish_mpp_zero_copy(const VideoFrameInfo &frame)
    {
        U64 token = 0;
        S32 ret = VB_Export(frame.ulBufferId, &token);
        if (ret != 0) {
            RCLCPP_ERROR(get_logger(), "VB_Export buffer=0x%lx failed: %d", static_cast<unsigned long>(frame.ulBufferId), ret);
            return;
        }

        exportedFrames_.push_back(ExportedFrame{frame.ulBufferId, token});

        const auto &vf = frame.stVFrame;
        const auto &common = frame.stViFrameInfo.stCommFrameInfo;
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

    void release_exported_frames(bool releaseAll)
    {
        const int keepCount = maxExportedFrames_ > 0 ? maxExportedFrames_ : 1;
        while (!exportedFrames_.empty() && (releaseAll || static_cast<int>(exportedFrames_.size()) > keepCount)) {
            const auto exported = exportedFrames_.front();
            exportedFrames_.pop_front();
            (void)VB_Unexport(exported.bufferId);
        }
    }

    void publish_nv12_copy(const VideoFrameInfo &frame)
    {
        auto msg = sensor_msgs::msg::Image();
        const auto &vf = frame.stVFrame;
        const uint8_t *y = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(vf.ulPlaneVirAddr[0]));
        const uint8_t *uv = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(vf.ulPlaneVirAddr[1]));
        const uint32_t ySize = vf.u32PlaneSizeValid[0];
        const uint32_t uvSize = vf.u32PlaneSizeValid[1];

        if (y == nullptr || uv == nullptr || ySize == 0 || uvSize == 0) {
            RCLCPP_WARN(get_logger(), "invalid NV12 planes");
            return;
        }

        msg.header.stamp = now();
        msg.header.frame_id = frameId_;
        msg.height = static_cast<uint32_t>(height_ * 3 / 2);
        msg.width = static_cast<uint32_t>(width_);
        msg.encoding = "nv12";
        msg.is_bigendian = false;
        msg.step = static_cast<uint32_t>(width_);
        msg.data.resize(static_cast<size_t>(ySize + uvSize));
        std::memcpy(msg.data.data(), y, ySize);
        std::memcpy(msg.data.data() + ySize, uv, uvSize);

        imagePublisher_->publish(std::move(msg));
    }

private:
    VI_DEV viDev_{0};
    VI_CHN viChn_{0};
    int sensorWidth_{3864};
    int sensorHeight_{2192};
    int width_{1920};
    int height_{1080};
    int mipiLanes_{4};
    int mbps_{800};
    int fps_{30};
    std::string frameId_{"camera"};
    std::string topicName_{"mpp_frame_desc"};
    std::string copyTopicName_{"image_raw"};
    std::string publishMode_{"mpp_zero_copy"};
    int maxExportedFrames_{8};

    bool sysInited_{false};
    bool vbInited_{false};
    bool viInited_{false};
    bool devEnabled_{false};
    bool chnEnabled_{false};

    std::deque<ExportedFrame> exportedFrames_;
    rclcpp::Publisher<mpp_msgs::msg::MppVideoFrameDesc>::SharedPtr descPublisher_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr imagePublisher_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<IspCameraNode>());
    rclcpp::shutdown();
    return 0;
}
