# ============================================================
# mpi/demux  →  libmpp_demux (static)
# ============================================================

set(DEMUX_SRCS
    demux.c
)

add_library(mpp_demux STATIC ${DEMUX_SRCS})

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
)

target_include_directories(mpp_demux
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(mpp_demux
    PUBLIC  mpp_sys
    PRIVATE ${FFMPEG_LIBRARIES}
)

target_link_directories(mpp_demux PRIVATE ${FFMPEG_LIBRARY_DIRS})

set_target_properties(mpp_demux PROPERTIES
    OUTPUT_NAME "mpp_demux"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_demux ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/demux/demux_api.h"
    "${CMAKE_SOURCE_DIR}/include/demux/demux_type.h"
    DESTINATION include/demux
    OPTIONAL
)
