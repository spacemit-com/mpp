# ============================================================
# mpi/mux  →  libmpp_mux (static)
# ============================================================

set(MUX_SRCS
    mux.c
    mux_rtsp_server.c
    mux_rtp.c
)

add_library(mpp_mux STATIC ${MUX_SRCS})

target_include_directories(mpp_mux
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mpp_mux
    PUBLIC  mpp_sys
)

set_target_properties(mpp_mux PROPERTIES
    OUTPUT_NAME "mpp_mux"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_mux ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/mux/mux_api.h"
    "${CMAKE_SOURCE_DIR}/include/mux/mux_type.h"
    DESTINATION include/mux
    OPTIONAL
)
