# ============================================================
# mpi/vdec  →  mpp_vdec (OBJECT, aggregated into libspacemit_mpp.so)
# ============================================================

set(VDEC_SRCS
    vdec.c
    vdec_bufpool.c
)

add_library(mpp_vdec OBJECT ${VDEC_SRCS})

target_include_directories(mpp_vdec BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPP_INCLUDE_DIRS}
)

install(FILES
    "${CMAKE_SOURCE_DIR}/include/vdec/vdec_api.h"
    "${CMAKE_SOURCE_DIR}/include/vdec/vdec_type.h"
    DESTINATION "${MPP_INSTALL_INCLUDEDIR}/vdec"
    OPTIONAL
)
