# Sample executables

set(MPP_SAMPLE_COMMON_DEFS _GNU_SOURCE)

# UVC → VDEC decode and save NV12 frames
add_executable(sample_uvc_vdec sample_uvc_vdec.c)
target_compile_definitions(sample_uvc_vdec PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec PRIVATE mpp)

# RTSP pull via DEMUX
add_executable(sample_rtsp_pull sample_rtsp_pull.c)
target_compile_definitions(sample_rtsp_pull PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_rtsp_pull PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_rtsp_pull PRIVATE mpp)

# UVC → VDEC decode → VENC encode and save H.264
add_executable(sample_uvc_vdec_venc sample_uvc_vdec_venc.c)
target_compile_definitions(sample_uvc_vdec_venc PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec_venc PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec_venc PRIVATE mpp)

# UVC → VDEC → VENC → RTSP (MUX)
add_executable(sample_uvc_vdec_venc_rtsp sample_uvc_vdec_venc_rtsp.c)
target_compile_definitions(sample_uvc_vdec_venc_rtsp PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec_venc_rtsp PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec_venc_rtsp PRIVATE mpp)

# VI -> VENC -> file
add_executable(sample_vi_k3_venc_bind sample_vi_k3_venc_bind.c)
target_compile_definitions(sample_vi_k3_venc_bind PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(sample_vi_k3_venc_bind PRIVATE mpp pthread)
target_include_directories(sample_vi_k3_venc_bind PRIVATE ${MPP_INCLUDE_DIRS})

# VI -> VENC -> file (4 streams)
add_executable(sample_vi_k3_4venc_bind sample_vi_k3_4venc_bind.c)
target_compile_definitions(sample_vi_k3_4venc_bind PRIVATE ${MPP_TEST_COMMON_DEFS})
target_link_libraries(sample_vi_k3_4venc_bind PRIVATE mpp pthread)
target_include_directories(sample_vi_k3_4venc_bind PRIVATE ${MPP_INCLUDE_DIRS})

# Multi-Stream RTSP Transcoder (Core Application)
add_executable(sample_multi_rtsp
    sample_multi_rtsp.c
    ${CMAKE_SOURCE_DIR}/mpi/stream/mpp_stream_manager.c
    ${CMAKE_SOURCE_DIR}/mpi/stream/mpp_queue.c
)
target_compile_definitions(sample_multi_rtsp PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_multi_rtsp PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_multi_rtsp PRIVATE mpp)

# File/RTSP transcode: DEMUX -> VDEC -> VENC -> MUX (manual + bind)
add_executable(sample_file_transcode sample_file_transcode.c)
target_compile_definitions(sample_file_transcode PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_file_transcode PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_file_transcode PRIVATE mpp)

# UVC dual-stream: one USB camera -> VDEC -> main(H.264) + sub(H.265) dual encode
add_executable(sample_uvc_dual_stream sample_uvc_dual_stream.c)
target_compile_definitions(sample_uvc_dual_stream PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_dual_stream PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_dual_stream PRIVATE mpp)

# UVC record + snapshot: UVC -> VDEC -> H.264 record + periodic MJPEG snapshot
add_executable(sample_uvc_record_snap sample_uvc_record_snap.c)
target_compile_definitions(sample_uvc_record_snap PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_record_snap PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_record_snap PRIVATE mpp)

# UVC dual camera: TWO USB cameras captured & H.264-encoded in parallel
add_executable(sample_uvc_dual_cam sample_uvc_dual_cam.c)
target_compile_definitions(sample_uvc_dual_cam PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_dual_cam PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_dual_cam PRIVATE mpp)

# RTSP relay + record: DEMUX(RTSP) -> MUX(RTSP relay) + MUX(file record), one-in dual-out
add_executable(sample_rtsp_relay_record sample_rtsp_relay_record.c)
target_compile_definitions(sample_rtsp_relay_record PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_rtsp_relay_record PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_rtsp_relay_record PRIVATE mpp)

# Multi-channel parallel decode: N x (DEMUX -> VDEC), mixed file/RTSP inputs
add_executable(sample_multi_decode sample_multi_decode.c)
target_compile_definitions(sample_multi_decode PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_multi_decode PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_multi_decode PRIVATE mpp pthread)

# MUX record / RTMP push: Annex-B ES file -> fMP4/TS segmented recording or RTMP
add_executable(sample_mux_record sample_mux_record.c)
target_compile_definitions(sample_mux_record PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_mux_record PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_mux_record PRIVATE mpp)

# UVC -> VDEC -> VENC -> file recording + RTMP push (real-world pipeline)
add_executable(sample_uvc_record_rtmp sample_uvc_record_rtmp.c)
target_compile_definitions(sample_uvc_record_rtmp PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_record_rtmp PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_record_rtmp PRIVATE mpp)
