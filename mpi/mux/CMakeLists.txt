# ============================================================
# mpi/mux  →  mpp_mux (OBJECT, aggregated into libmpp.so)
# ============================================================

set(MUX_SRCS
    mux.c
    mux_rtsp_server.c
    mux_rtp.c
)

add_library(mpp_mux OBJECT ${MUX_SRCS})

target_include_directories(mpp_mux
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/mux/mux_api.h"
    "${CMAKE_SOURCE_DIR}/include/mux/mux_type.h"
    DESTINATION include/mux
    OPTIONAL
)
