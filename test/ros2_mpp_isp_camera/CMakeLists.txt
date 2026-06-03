cmake_minimum_required(VERSION 3.8)
project(mpp_isp_camera_ros2)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(mpp_msgs REQUIRED)

# Find MPP root - resolve real path for symlinked colcon workspace
get_filename_component(REAL_PACKAGE_DIR "${CMAKE_CURRENT_LIST_DIR}" REALPATH)
message(STATUS "Package real path: ${REAL_PACKAGE_DIR}")

set(MPP_ROOT "${REAL_PACKAGE_DIR}/../.." CACHE PATH "MPP source root")
set(MPP_BUILD_ROOT "${MPP_ROOT}/build" CACHE PATH "MPP build root")
message(STATUS "MPP_ROOT: ${MPP_ROOT}")
message(STATUS "Test files: ${MPP_ROOT}/test/test_*.cpp")

include_directories(
  ${MPP_ROOT}/include
  ${MPP_ROOT}/include/sys
  ${MPP_ROOT}/include/uvc
  ${MPP_ROOT}/include/vi
)

link_directories(
  ${MPP_BUILD_ROOT}/lib
)

add_executable(mpp_isp_camera_node "${MPP_ROOT}/test/test_vi_isp_cam_ros2_node.cpp")
ament_target_dependencies(mpp_isp_camera_node rclcpp sensor_msgs mpp_msgs)
target_link_libraries(mpp_isp_camera_node mpp)

add_executable(mpp_uvc_camera_node "${MPP_ROOT}/test/test_uvc_ros2_node.cpp")
ament_target_dependencies(mpp_uvc_camera_node rclcpp sensor_msgs mpp_msgs)
target_link_libraries(mpp_uvc_camera_node mpp)

add_executable(mpp_frame_consumer_node "${MPP_ROOT}/test/test_vi_isp_cam_ros2_consumer.cpp")
ament_target_dependencies(mpp_frame_consumer_node rclcpp mpp_msgs)
target_link_libraries(mpp_frame_consumer_node mpp)

install(TARGETS
  mpp_isp_camera_node
  mpp_uvc_camera_node
  mpp_frame_consumer_node
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
