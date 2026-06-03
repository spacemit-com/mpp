# Test executables (SYS + VB + integration + multi-process)
# Link mpp_sys (contains sys.c, vb.c, mpp_shm.c, dma_alloc.c).

set(MPP_TEST_COMMON_DEFS _GNU_SOURCE)

function(mpp_add_test_executable name)
    add_executable(${name} ${name}.c)
    target_compile_definitions(${name} PRIVATE ${MPP_TEST_COMMON_DEFS})
    target_link_libraries(${name} PRIVATE mpp_sys)
    target_include_directories(${name} PRIVATE ${MPP_INCLUDE_DIRS})
    add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
endfunction()

mpp_add_test_executable(test_sys)
mpp_add_test_executable(test_vb)
mpp_add_test_executable(test_integration)
mpp_add_test_executable(test_multiproc)

# RTSP pipeline test (demux + mux, needs ffmpeg)
add_executable(test_rtsp_pipeline test_rtsp_pipeline.c)
target_compile_definitions(test_rtsp_pipeline PRIVATE ${MPP_TEST_COMMON_DEFS})
target_include_directories(test_rtsp_pipeline PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(test_rtsp_pipeline PRIVATE mpp)

# RTSP pull test (demux only, needs ffmpeg/network source)
add_executable(test_rtsp_pull test_rtsp_pull.c)
target_compile_definitions(test_rtsp_pull PRIVATE ${MPP_TEST_COMMON_DEFS})
target_include_directories(test_rtsp_pull PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(test_rtsp_pull PRIVATE mpp)

# DEMUX file test (MP4/TS demuxing + bind to VDEC, save NV12 frames)
add_executable(test_demux_file test_demux_file.c)
target_compile_definitions(test_demux_file PRIVATE ${MPP_TEST_COMMON_DEFS})
target_include_directories(test_demux_file PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(test_demux_file PRIVATE mpp)

# TS demuxer raw dump test - dumps H.264 ES output for offline verification
add_executable(test_ts_dump test_ts_dump.c)
target_compile_definitions(test_ts_dump PRIVATE ${MPP_TEST_COMMON_DEFS})
target_include_directories(test_ts_dump PRIVATE ${MPP_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/mpi/demux/container/ts)
target_link_libraries(test_ts_dump PRIVATE mpp)

# DEMUX seek regression test (low-level demux API)
add_executable(test_demux_seek test_demux_seek.c)
target_compile_definitions(test_demux_seek PRIVATE ${MPP_TEST_COMMON_DEFS})
target_include_directories(test_demux_seek PRIVATE ${MPP_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/mpi/demux)
target_link_libraries(test_demux_seek PRIVATE mpp)

add_executable(test_uvc test_uvc.c)
target_compile_definitions(test_uvc PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_uvc PRIVATE mpp)
target_include_directories(test_uvc PRIVATE ${MPP_INCLUDE_DIRS})
add_test(NAME test_uvc COMMAND $<TARGET_FILE:test_uvc>)

# UVC → VDEC → VENC integration test (MJPEG in, H.264 ES to file)
add_executable(test_uvc_vdec test_uvc_vdec.c)
target_compile_definitions(test_uvc_vdec PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_uvc_vdec PRIVATE mpp)
target_include_directories(test_uvc_vdec PRIVATE ${MPP_INCLUDE_DIRS})
add_test(NAME test_uvc_vdec COMMAND $<TARGET_FILE:test_uvc_vdec>)

# File-based single-module codec tests
add_executable(test_vi_venc_file test_vi_venc_file.c)
target_compile_definitions(test_vi_venc_file PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_venc_file PRIVATE mpp)
target_include_directories(test_vi_venc_file PRIVATE ${MPP_INCLUDE_DIRS})

# VENC: API / format / param / multi-channel regression (select via --test).
# Uses synthetic NV12 frames, no external input required.
add_executable(test_venc venc/test_venc.c)
target_compile_definitions(test_venc PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_venc PRIVATE mpp)
target_include_directories(test_venc PRIVATE ${MPP_INCLUDE_DIRS})
add_test(NAME test_venc COMMAND $<TARGET_FILE:test_venc> --test api)

# VDEC: API / multi-codec / scale regression (select suites via --test)
# Paths are relative to this directory (test/), not project root.
set(TEST_VDEC_PARSE_SRCS
    vdec/parse/parse.c
    vdec/parse/h264parse.c
    vdec/parse/h265parse.c
    vdec/parse/mjpegparse.c
    vdec/parse/defaultparse.c)
add_executable(test_vdec vdec/test_vdec.c ${TEST_VDEC_PARSE_SRCS})
target_compile_definitions(test_vdec PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vdec PRIVATE mpp)
target_include_directories(test_vdec PRIVATE ${MPP_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/vdec/include")
add_test(NAME test_vdec COMMAND $<TARGET_FILE:test_vdec> --test api)

# Demo executables
add_executable(test_v2d test_v2d.c)
target_compile_definitions(test_v2d PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_v2d PRIVATE mpp)
target_include_directories(test_v2d PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_v2d_draw test_v2d_draw.c)
target_compile_definitions(test_v2d_draw PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_v2d_draw PRIVATE mpp)
target_include_directories(test_v2d_draw PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_v2d_blend test_v2d_blend.c)
target_compile_definitions(test_v2d_blend PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_v2d_blend PRIVATE mpp)
target_include_directories(test_v2d_blend PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_v2d_mask test_v2d_mask.c)
target_compile_definitions(test_v2d_mask PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_v2d_mask PRIVATE mpp)
target_include_directories(test_v2d_mask PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_v2d_boarder_fill test_v2d_boarder_fill.c)
target_compile_definitions(test_v2d_boarder_fill PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_v2d_boarder_fill PRIVATE mpp)
target_include_directories(test_v2d_boarder_fill PRIVATE ${MPP_INCLUDE_DIRS})

if(TARGET mpp_cpp)
    add_executable(test_vi_cpp test_vi_cpp.c)
    target_compile_definitions(test_vi_cpp PRIVATE ${MPP_TEST_COMMON_DEFS})
    target_link_libraries(test_vi_cpp PRIVATE mpp)
    target_include_directories(test_vi_cpp PRIVATE ${MPP_INCLUDE_DIRS})

    add_executable(test_vi_cpp_multi_output test_vi_cpp_multi_output.c)
    target_compile_definitions(test_vi_cpp_multi_output PRIVATE ${MPP_TEST_COMMON_DEFS})
    target_link_libraries(test_vi_cpp_multi_output PRIVATE mpp)
    target_include_directories(test_vi_cpp_multi_output PRIVATE ${MPP_INCLUDE_DIRS})
else()
    message(STATUS "mpp_cpp not available, skipping vi_cpp_demo and vi_cpp_multi_output_demo")
endif()

add_executable(test_vi_k1_cam test_vi_k1_cam.c)
target_compile_definitions(test_vi_k1_cam PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_k1_cam PRIVATE mpp)
target_include_directories(test_vi_k1_cam PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_vi_k3_cam test_vi_k3_cam.c)
target_compile_definitions(test_vi_k3_cam PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_k3_cam PRIVATE mpp)
target_include_directories(test_vi_k3_cam PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_vi_k1_ispbypass test_vi_k1_ispbypass.c)
target_compile_definitions(test_vi_k1_ispbypass PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_k1_ispbypass PRIVATE mpp)
target_include_directories(test_vi_k1_ispbypass PRIVATE ${MPP_INCLUDE_DIRS})

add_executable(test_vi_isp_cam_api test_vi_isp_cam_api.c)
target_compile_definitions(test_vi_isp_cam_api PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_isp_cam_api PRIVATE mpp)
target_include_directories(test_vi_isp_cam_api PRIVATE ${MPP_INCLUDE_DIRS})

# ROS2 ISP camera examples are built by the top-level build_ros2_examples
# target when BUILD_ROS2_EXAMPLES is AUTO/ON and ROS2 + colcon are available.

add_executable(test_vi_k1_offline_addr test_vi_k1_offline_addr.c)
target_compile_definitions(test_vi_k1_offline_addr PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(test_vi_k1_offline_addr PRIVATE mpp)
target_include_directories(test_vi_k1_offline_addr PRIVATE ${MPP_INCLUDE_DIRS})

set_tests_properties(test_multiproc PROPERTIES
    LABELS "slow;multiproc"
)
