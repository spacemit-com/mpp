# ============================================================
# mpi/venc  →  libmpp_venc (static)
# ============================================================

set(VENC_SRCS
    venc.c
    venc_api.c
)

add_library(mpp_venc STATIC ${VENC_SRCS})

target_include_directories(mpp_venc BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPP_INCLUDE_DIRS}
)

target_link_libraries(mpp_venc PUBLIC mpp_sys mpp_al_module)
if(TARGET utils)
    target_link_libraries(mpp_venc PRIVATE utils)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mpp_venc PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(mpp_venc PROPERTIES
    OUTPUT_NAME "mpp_venc"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp_venc ARCHIVE DESTINATION lib)
install(FILES
    "${CMAKE_SOURCE_DIR}/include/venc/venc_api.h"
    "${CMAKE_SOURCE_DIR}/include/venc/venc_type.h"
    DESTINATION include/venc
)
