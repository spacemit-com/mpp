# ============================================================
# mpi/sys  →  libmpp_sys (static)
# Covers: SYS module + VB (Video Buffer) module
# ============================================================

set(SYS_SRCS
    sys.c
    vb.c
    mpp_shm.c
    dma_alloc.c
)

add_library(mpp_sys STATIC ${SYS_SRCS})

find_package(Threads REQUIRED)

target_include_directories(mpp_sys
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

target_link_libraries(mpp_sys PUBLIC Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mpp_sys PUBLIC rt)
endif()

set_target_properties(mpp_sys PROPERTIES
    OUTPUT_NAME "mpp_sys"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_sys
    ARCHIVE DESTINATION lib
)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/sys/sys_api.h"
    "${CMAKE_SOURCE_DIR}/include/sys/sys_type.h"
    "${CMAKE_SOURCE_DIR}/include/sys/vb_api.h"
    "${CMAKE_SOURCE_DIR}/include/sys/vb_type.h"
    "${CMAKE_SOURCE_DIR}/include/sys/type.h"
    "${CMAKE_SOURCE_DIR}/include/sys/mpp_shm.h"
    "${CMAKE_SOURCE_DIR}/include/sys/dma_alloc.h"
    DESTINATION include/sys
)
