# ============================================================
# al/vi/k3  →  K3 VI plugin (standard V4L2 capture path)
#
# K3 is built when K1-specific camera libraries are absent.
# On K1 platforms, libccic.so / libvi.so / libisp.so are
# present and vi_k1_cam_plugin is built instead.
# ============================================================

find_library(K3_SKIP_CCIC_LIB ccic)
find_library(K3_SKIP_VI_LIB   vi)
find_library(K3_SKIP_ISP_LIB  isp)

if(K3_SKIP_CCIC_LIB AND K3_SKIP_VI_LIB AND K3_SKIP_ISP_LIB)
    message(STATUS "K1 camera libraries detected — skipping vi_k3_cam_plugin (K1 platform)")
else()
    message(STATUS "K1 camera libraries not found — building vi_k3_cam_plugin (K3/V4L2 platform)")

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
endif()
