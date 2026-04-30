# ============================================================
# mpi/vo  →  libmpp_vo (static)
# ============================================================

set(VO_SRCS
    vo.c
)

add_library(mpp_vo STATIC ${VO_SRCS})

target_include_directories(mpp_vo
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

target_link_libraries(mpp_vo PUBLIC mpp_sys)

set_target_properties(mpp_vo PROPERTIES
    OUTPUT_NAME "mpp_vo"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_vo ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/vo/vo_api.h"
    "${CMAKE_SOURCE_DIR}/include/vo/vo_type.h"
    DESTINATION include/vo
    OPTIONAL
)
