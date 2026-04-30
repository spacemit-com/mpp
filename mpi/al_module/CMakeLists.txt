# ============================================================
# Shared AL plugin loader (single copy for vdec + venc + others)
# ============================================================

add_library(mpp_al_module STATIC "${CMAKE_SOURCE_DIR}/al/module.c")

target_include_directories(mpp_al_module
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)

if(TARGET utils)
    target_link_libraries(mpp_al_module PRIVATE utils)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mpp_al_module PUBLIC ${CMAKE_DL_LIBS})
endif()

set_target_properties(mpp_al_module PROPERTIES
    OUTPUT_NAME "mpp_al_module"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
