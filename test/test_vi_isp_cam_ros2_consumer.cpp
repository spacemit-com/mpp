/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    test_vi_isp_cam_ros2_consumer.cpp
 * @Date      :    2026-04-28
 * @Brief     :    ROS2 zero-copy consumer example for MPP VB frame descriptors.
 *
 * Build note:
 *   Copy this file into a ROS2 package that depends on mpp_msgs and links with
 *   mpp_sys. The producer publishes mpp_msgs/msg/MppVideoFrameDesc messages.
 *------------------------------------------------------------------------------
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include "mpp_msgs/msg/mpp_video_frame_desc.hpp"
#include "rclcpp/rclcpp.hpp"

extern "C" {
#include "sys_api.h"
#include "vb_api.h"
}

class MppFrameConsumerNode final : public rclcpp::Node {
public:
    MppFrameConsumerNode() : Node("mpp_frame_consumer_node") {
        topicName_ = declare_parameter<std::string>("topic", "mpp_frame_desc");

        if (SYS_Init() != 0) {
            throw std::runtime_error("SYS_Init failed");
        }
        sysInited_ = true;

        if (VB_Init() != 0) {
            close_mpp();
            throw std::runtime_error("VB_Init failed");
        }
        vbInited_ = true;

        sub_ = create_subscription<mpp_msgs::msg::MppVideoFrameDesc>(
            topicName_, rclcpp::SensorDataQoS(),
            std::bind(&MppFrameConsumerNode::on_frame, this, std::placeholders::_1));

        RCLCPP_INFO(get_logger(), "MPP zero-copy consumer started: topic=%s", topicName_.c_str());
    }

    ~MppFrameConsumerNode() override { close_mpp(); }

private:
    void close_mpp() {
        if (vbInited_) {
            (void)VB_Exit();
            vbInited_ = false;
        }
        if (sysInited_) {
            (void)SYS_Exit();
            sysInited_ = false;
        }
    }

    void on_frame(const mpp_msgs::msg::MppVideoFrameDesc::SharedPtr msg) {
        UL buffer = 0;
        void *virAddr = nullptr;
        S32 dmaBufFd = -1;
        S32 ret;

        ret = VB_Import(msg->token, &buffer);
        if (ret != 0 || buffer == 0) {
            RCLCPP_ERROR(
                get_logger(), "VB_Import token=0x%" PRIx64 " failed: %d", static_cast<uint64_t>(msg->token), ret);
            return;
        }

        ret = VB_GetVirAddr(buffer, &virAddr);
        if (ret != 0 || virAddr == nullptr) {
            RCLCPP_ERROR(
                get_logger(), "VB_GetVirAddr buffer=0x%" PRIx64 " failed: %d", static_cast<uint64_t>(buffer), ret);
            (void)VB_ReleaseBuffer(buffer);
            return;
        }

        ret = VB_GetDmaBufFd(buffer, &dmaBufFd);
        if (ret != 0) {
            RCLCPP_WARN(
                get_logger(), "VB_GetDmaBufFd buffer=0x%" PRIx64 " failed: %d", static_cast<uint64_t>(buffer), ret);
        }

        /*
         * Zero-copy processing point:
         *   - virAddr points to this process' mapping of the same MPP VB buffer.
         *   - dmaBufFd can be passed to DMA-capable APIs such as V4L2 DMABUF.
         *   - Use msg->plane_stride / msg->plane_size_valid for plane layout.
         *   - Do not free virAddr directly; release the imported VB reference.
         */
        RCLCPP_INFO_THROTTLE(
            get_logger(),
            *get_clock(),
            2000,
            "frame idx=%u %ux%u fmt=%u planes=%u buffer=0x%" PRIx64 " fd=%d vir=%p",
            msg->frame_index,
            msg->width,
            msg->height,
            msg->pixel_format,
            msg->plane_num,
            static_cast<uint64_t>(buffer),
            dmaBufFd,
            virAddr);

        (void)VB_ReleaseBuffer(buffer);
    }

private:
    std::string topicName_{"mpp_frame_desc"};
    bool sysInited_{false};
    bool vbInited_{false};
    rclcpp::Subscription<mpp_msgs::msg::MppVideoFrameDesc>::SharedPtr sub_;
};

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<MppFrameConsumerNode>());
    rclcpp::shutdown();
    return 0;
}
