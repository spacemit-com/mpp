# ============================================================
# utils  →  shared helpers (frame, packet, log, v4l2, dma-buf, …)
# Built as a static library for AL plugins and future MPI use.
# ============================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "utils: skipped (Linux-only sources)")
    return()
endif()

set(MPP_UTILS_SRCS
    dataqueue.c
    dmabufwrapper.c
    env.c
    frame.c
    log.c
    packet.c
    ringbuffer.c
    v4l2_utils.c
    os/linux/os_env.c
    os/linux/os_log.c
)

add_library(utils STATIC ${MPP_UTILS_SRCS})

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/os/include
        ${PROJECT_SOURCE_DIR}/include/sys
)

find_package(Threads REQUIRED)
target_link_libraries(utils PUBLIC Threads::Threads)
