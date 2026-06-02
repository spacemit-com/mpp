# ============================================================
# mpi/venc  →  mpp_venc (OBJECT, aggregated into libmpp.so)
# ============================================================

set(VENC_SRCS
    venc.c
    venc_api.c
)

add_library(mpp_venc OBJECT ${VENC_SRCS})

target_include_directories(mpp_venc BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPP_INCLUDE_DIRS}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/venc/venc_api.h"
    "${CMAKE_SOURCE_DIR}/include/venc/venc_type.h"
    DESTINATION include/venc
)
