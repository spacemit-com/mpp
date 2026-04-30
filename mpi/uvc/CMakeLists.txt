# ============================================================
# mpi/uvc  →  libmpp_uvc (static)
# ============================================================

set(UVC_SRCS
    uvc.c
)

add_library(mpp_uvc STATIC ${UVC_SRCS})

target_include_directories(mpp_uvc
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

target_link_libraries(mpp_uvc PUBLIC mpp_sys)

set_target_properties(mpp_uvc PROPERTIES
    OUTPUT_NAME "mpp_uvc"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_uvc ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/uvc/uvc_api.h"
    "${CMAKE_SOURCE_DIR}/include/uvc/uvc_type.h"
    DESTINATION include/uvc
    OPTIONAL
)
