# ============================================================
# al/vi/k3  →  K3 VI plugin (standard V4L2 capture path)
# ============================================================

include_directories(include)

set(VI_K3_SRCS
    vi_k3.c
    vi_k3_v4l2.c
)

add_library(vi_k3_cam_plugin SHARED ${VI_K3_SRCS})

target_include_directories(vi_k3_cam_plugin
    PUBLIC
        ${MPP_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vi_k3_cam_plugin PRIVATE pthread)

set_target_properties(vi_k3_cam_plugin PROPERTIES
    OUTPUT_NAME "vi_k3_cam_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

add_custom_command(TARGET vi_k3_cam_plugin POST_BUILD
    COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/install_plugin.sh $<TARGET_FILE:vi_k3_cam_plugin>
    COMMENT "Installing libvi_k3_cam_plugin.so to system or user plugin directory"
)

install(TARGETS vi_k3_cam_plugin
    LIBRARY DESTINATION lib
)
