# ============================================================
# mpi/v2d  →  libmpp_v2d (static)
# ============================================================

set(V2D_SRCS
    v2d.c
)

add_library(mpp_v2d STATIC ${V2D_SRCS})

target_include_directories(mpp_v2d
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(mpp_v2d PUBLIC mpp_sys)

set_target_properties(mpp_v2d PROPERTIES
    OUTPUT_NAME "mpp_v2d"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_v2d ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/v2d/v2d_api.h"
    "${CMAKE_SOURCE_DIR}/include/v2d/v2d_type.h"
    DESTINATION include/v2d
    OPTIONAL
)
