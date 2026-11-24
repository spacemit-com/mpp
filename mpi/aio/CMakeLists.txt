# ============================================================
# mpi/aio  →  mpp_aio (OBJECT, aggregated into libspacemit_mpp.so)
# Covers: AI (audio input) + AO (audio output) modules
# The pcm backend is a plugin, see al/aio.
# ============================================================

set(AIO_SRCS
    ai.c
    ai_stub.c
    ao.c
    ao_stub.c
    aio_backend.c
    aio_common.c
    aio_resample.c
)

add_library(mpp_aio OBJECT ${AIO_SRCS})

target_include_directories(mpp_aio
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/al/include"
        "${PROJECT_SOURCE_DIR}/al/aio/include"
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/aio/ai_api.h"
    "${CMAKE_SOURCE_DIR}/include/aio/ai_type.h"
    "${CMAKE_SOURCE_DIR}/include/aio/ao_api.h"
    "${CMAKE_SOURCE_DIR}/include/aio/ao_type.h"
    "${CMAKE_SOURCE_DIR}/include/aio/aio_type.h"
    DESTINATION include/aio
)
