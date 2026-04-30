cmake_minimum_required(VERSION 3.8)
project(mpp_isp_camera_ros2)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(mpp_msgs REQUIRED)

set(MPP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "MPP source root")
set(MPP_BUILD_ROOT "${MPP_ROOT}/build" CACHE PATH "MPP build root")

include_directories(
  ${MPP_ROOT}/include
  ${MPP_ROOT}/include/sys
  ${MPP_ROOT}/include/vi
)

link_directories(
  ${MPP_BUILD_ROOT}/lib
  ${MPP_BUILD_ROOT}/utils
)

add_executable(mpp_isp_camera_node ${MPP_ROOT}/test/test_vi_isp_cam_ros2_node.cpp)
ament_target_dependencies(mpp_isp_camera_node rclcpp sensor_msgs mpp_msgs)
target_link_libraries(mpp_isp_camera_node mpp_vi mpp_al_module utils mpp_sys pthread dl)

add_executable(mpp_frame_consumer_node ${MPP_ROOT}/test/test_vi_isp_cam_ros2_consumer.cpp)
ament_target_dependencies(mpp_frame_consumer_node rclcpp mpp_msgs)
target_link_libraries(mpp_frame_consumer_node mpp_sys pthread)

install(TARGETS
  mpp_isp_camera_node
  mpp_frame_consumer_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
