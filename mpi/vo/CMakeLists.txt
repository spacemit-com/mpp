# ============================================================
# mpi/vo  →  mpp_vo (OBJECT, aggregated into libmpp.so)
# ============================================================

set(VO_SRCS
    vo.c
)

add_library(mpp_vo OBJECT ${VO_SRCS})

target_include_directories(mpp_vo
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/vo/vo_api.h"
    "${CMAKE_SOURCE_DIR}/include/vo/vo_type.h"
    DESTINATION include/vo
    OPTIONAL
)
