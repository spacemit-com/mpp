# ============================================================
# mpi/uvc  →  mpp_uvc (OBJECT, aggregated into libmpp.so)
# ============================================================

set(UVC_SRCS
    uvc.c
)

add_library(mpp_uvc OBJECT ${UVC_SRCS})

target_include_directories(mpp_uvc
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/uvc/uvc_api.h"
    "${CMAKE_SOURCE_DIR}/include/uvc/uvc_type.h"
    DESTINATION include/uvc
    OPTIONAL
)
