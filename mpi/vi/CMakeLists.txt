# ============================================================
# mpi/vi  →  libmpp_vi (platform-neutral VI entry)
# ============================================================

set(VI_SRCS
    vi.c
    vi_buf_mgr.c
)

add_library(mpp_vi STATIC ${VI_SRCS})

target_include_directories(mpp_vi
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/al/include"
        "${PROJECT_SOURCE_DIR}/al/vi/include"
)

target_link_libraries(mpp_vi PUBLIC dl)
target_link_libraries(mpp_vi PUBLIC mpp_al_module)
target_link_libraries(mpp_vi PUBLIC mpp_sys)

set_target_properties(mpp_vi PROPERTIES
    OUTPUT_NAME "mpp_vi"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_vi ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/vi/vi_api.h"
    "${CMAKE_SOURCE_DIR}/include/vi/vi_type.h"
    DESTINATION include/vi
)
