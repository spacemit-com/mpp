# ============================================================
# utils  →  shared helpers (log, v4l2, dma-buf, …)
# Built as an OBJECT library, aggregated into libmpp.so.
# ============================================================

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "utils: skipped (Linux-only sources)")
    return()
endif()

set(MPP_UTILS_SRCS
    dmabufwrapper.c
    env.c
    log.c
    log_service.c
    v4l2_utils.c
    os/linux/os_env.c
    os/linux/os_log.c
)

add_library(utils OBJECT ${MPP_UTILS_SRCS})

install(FILES include/log.h DESTINATION include/sys)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/os/include
        ${PROJECT_SOURCE_DIR}/include/sys
)

# Standalone log viewer; its main() is not part of the utils object library.
add_executable(mpp-log mpp_log.c)
target_include_directories(mpp-log PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/sys
)
install(TARGETS mpp-log RUNTIME DESTINATION bin)
