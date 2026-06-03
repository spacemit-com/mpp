# =============================================================================
# demux - Demux Implementation
# =============================================================================
# Copyright 2025-2026 SPACEMIT. All rights reserved.
# BSD-style license.
# =============================================================================

cmake_minimum_required(VERSION 3.10)

option(DEMUX_BUILD_RTSP   "Build RTSP client (Phase 1)"    ON)
option(DEMUX_BUILD_MP4    "Build MP4 demuxer (Phase 2)"    ON)
option(DEMUX_BUILD_FLV    "Build FLV demuxer (Phase 2)"    ON)
option(DEMUX_BUILD_RTMP   "Build RTMP client (Phase 3)"    ON)
option(DEMUX_BUILD_TS     "Build TS demuxer (Phase 4)"     ON)
option(DEMUX_BUILD_HLS    "Build HLS client (Phase 4)"     ON)

# Common sources (always built)
set(COMMON_SOURCES
    common/bit_reader.c
    common/byte_buffer.c
    common/socket_utils.c
    common/url_parser.c
    codec/h264_utils.c
    codec/h265_utils.c
    codec/aac_utils.c
)

# RTP sources (needed by RTSP)
set(RTP_SOURCES
    rtp/rtp_packet.c
    rtp/h264_depacketizer.c
    rtp/h265_depacketizer.c
    rtp/aac_depacketizer.c
)

# RTSP sources (Phase 1)
set(RTSP_SOURCES
    protocol/rtsp/rtsp_parser.c
    protocol/rtsp/sdp_parser.c
    protocol/rtsp/rtp_session.c
    protocol/rtsp/rtsp_client.c
)

# Container sources (Phase 2)
set(MP4_SOURCES
    container/mp4/mp4_box.c
    container/mp4/mp4_demuxer.c
)

set(FLV_SOURCES
    container/flv/flv_demuxer.c
)

# RTMP sources (Phase 3)
set(RTMP_SOURCES
    protocol/rtmp/amf0.c
    protocol/rtmp/rtmp_handshake.c
    protocol/rtmp/rtmp_client.c
)

# TS/HLS sources (Phase 4)
set(TS_SOURCES
    container/ts/ts_demuxer.c
    container/ts/pes_parser.c
)

set(HLS_SOURCES
    protocol/http/http_client.c
    protocol/http/m3u8_parser.c
    protocol/http/hls_client.c
)

# Main demux (context + channel API unified)
set(DEMUX_SOURCES
    demux.c
    ${COMMON_SOURCES}
)

# Conditionally add sources
if(DEMUX_BUILD_RTSP)
    list(APPEND DEMUX_SOURCES ${RTP_SOURCES} ${RTSP_SOURCES})
    add_definitions(-DDEMUX_RTSP=1)
endif()

if(DEMUX_BUILD_MP4)
    list(APPEND DEMUX_SOURCES ${MP4_SOURCES})
    add_definitions(-DDEMUX_MP4=1)
endif()

if(DEMUX_BUILD_FLV)
    list(APPEND DEMUX_SOURCES ${FLV_SOURCES})
    add_definitions(-DDEMUX_FLV=1)
endif()

if(DEMUX_BUILD_RTMP)
    list(APPEND DEMUX_SOURCES ${RTMP_SOURCES})
    add_definitions(-DDEMUX_RTMP=1)
endif()

if(DEMUX_BUILD_TS)
    list(APPEND DEMUX_SOURCES ${TS_SOURCES})
    add_definitions(-DDEMUX_TS=1)
endif()

if(DEMUX_BUILD_HLS)
    list(APPEND DEMUX_SOURCES ${HLS_SOURCES})
    add_definitions(-DDEMUX_HLS=1)
endif()

add_library(mpp_demux OBJECT ${DEMUX_SOURCES})

target_include_directories(mpp_demux 
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install
install(FILES
    "${CMAKE_SOURCE_DIR}/include/demux/demux_api.h"
    "${CMAKE_SOURCE_DIR}/include/demux/demux_type.h"
    DESTINATION include/demux
    OPTIONAL
)
