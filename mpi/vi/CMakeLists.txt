# ============================================================
# mpi/vi  →  mpp_vi (OBJECT, aggregated into libmpp.so)
# ============================================================

set(VI_SRCS
    vi.c
    vi_buf_mgr.c
)

add_library(mpp_vi OBJECT ${VI_SRCS})

target_include_directories(mpp_vi
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/al/include"
        "${PROJECT_SOURCE_DIR}/al/vi/include"
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/vi/vi_api.h"
    "${CMAKE_SOURCE_DIR}/include/vi/vi_type.h"
    DESTINATION include/vi
)
