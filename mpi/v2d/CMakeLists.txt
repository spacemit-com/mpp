# ============================================================
# mpi/v2d  →  mpp_v2d (OBJECT, aggregated into libmpp.so)
# ============================================================

set(V2D_SRCS
    v2d.c
)

add_library(mpp_v2d OBJECT ${V2D_SRCS})

target_include_directories(mpp_v2d
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/v2d/v2d_api.h"
    "${CMAKE_SOURCE_DIR}/include/v2d/v2d_type.h"
    DESTINATION include/v2d
    OPTIONAL
)
