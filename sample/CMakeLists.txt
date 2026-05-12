# Sample executables

set(MPP_SAMPLE_COMMON_DEFS _GNU_SOURCE)

# UVC → VDEC decode and save NV12 frames
add_executable(sample_uvc_vdec sample_uvc_vdec.c)
target_compile_definitions(sample_uvc_vdec PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec PRIVATE mpp_uvc mpp_vdec mpp_sys)

# RTSP pull via DEMUX
add_executable(sample_rtsp_pull sample_rtsp_pull.c)
target_compile_definitions(sample_rtsp_pull PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_rtsp_pull PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_rtsp_pull PRIVATE mpp_demux mpp_sys)

# UVC → VDEC decode → VENC encode and save H.264
add_executable(sample_uvc_vdec_venc sample_uvc_vdec_venc.c)
target_compile_definitions(sample_uvc_vdec_venc PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec_venc PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec_venc PRIVATE mpp_uvc mpp_vdec mpp_venc mpp_sys)

# UVC → VDEC → VENC → RTSP (MUX)
add_executable(sample_uvc_vdec_venc_rtsp sample_uvc_vdec_venc_rtsp.c)
target_compile_definitions(sample_uvc_vdec_venc_rtsp PRIVATE ${MPP_SAMPLE_COMMON_DEFS})
target_include_directories(sample_uvc_vdec_venc_rtsp PRIVATE ${MPP_INCLUDE_DIRS})
target_link_libraries(sample_uvc_vdec_venc_rtsp PRIVATE mpp_uvc mpp_vdec mpp_venc mpp_mux mpp_sys)
