# ============================================================
# mpi/sys  →  mpp_sys (OBJECT, aggregated into libmpp.so)
# Covers: SYS module + VB (Video Buffer) module
# ============================================================

set(SYS_SRCS
    sys.c
    vb.c
    mpp_shm.c
    dma_alloc.c
)

add_library(mpp_sys OBJECT ${SYS_SRCS})

target_include_directories(mpp_sys
    PUBLIC
        ${MPP_INCLUDE_DIRS}
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
