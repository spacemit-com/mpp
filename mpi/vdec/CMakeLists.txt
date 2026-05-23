# ============================================================
# mpi/vdec  →  libmpp_vdec (static)
# ============================================================

set(VDEC_SRCS
    vdec.c
    vdec_api.c
)

add_library(mpp_vdec STATIC ${VDEC_SRCS})

target_include_directories(mpp_vdec BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPP_INCLUDE_DIRS}
)

target_link_libraries(mpp_vdec PUBLIC mpp_sys mpp_al_module)
if(TARGET utils)
    target_link_libraries(mpp_vdec PRIVATE utils)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mpp_vdec PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(mpp_vdec PROPERTIES
    OUTPUT_NAME "mpp_vdec"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_vdec ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/vdec/vdec_api.h"
    "${CMAKE_SOURCE_DIR}/include/vdec/vdec_type.h"
    DESTINATION include/vdec
    OPTIONAL
)
