# ============================================================
# mpi/mux  →  mpp_mux (OBJECT, aggregated into libmpp.so)
# ============================================================

set(MUX_SRCS
    mux.c
    mux_rtsp_server.c
    mux_rtp.c
    mux_common.c
    mux_writer.c
    mux_mp4.c
    mux_ts.c
    mux_file.c
    mux_rtmp.c
)

add_library(mpp_mux OBJECT ${MUX_SRCS})

target_include_directories(mpp_mux
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        # Reuse demux codec/common/protocol/container helpers (compiled into the
        # same libmpp.so): codec/h26x_utils.h, common/socket_utils.h,
        # protocol/rtmp/*.h, container/ts/pat_pmt.h, common/url_parser.h.
        ${CMAKE_SOURCE_DIR}/mpi/demux
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/mux/mux_api.h"
    "${CMAKE_SOURCE_DIR}/include/mux/mux_type.h"
    DESTINATION include/mux
    OPTIONAL
)
