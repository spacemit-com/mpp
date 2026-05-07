# ============================================================
# al/vi/k1  →  K1 VI plugin (shared library, dlopen'd at runtime)
# ============================================================

# Platform detection: check if K1 camera libraries are available
message(STATUS "CMAKE_SYSTEM_LIBRARY_PATH: ${CMAKE_SYSTEM_LIBRARY_PATH}")
message(STATUS "CMAKE_LIBRARY_PATH: ${CMAKE_LIBRARY_PATH}")
message(STATUS "CMAKE_FIND_ROOT_PATH: ${CMAKE_FIND_ROOT_PATH}")
message(STATUS "CMAKE_SYSROOT: ${CMAKE_SYSROOT}")

find_library(CAM_SENSORS_LIB cam_sensors)
find_library(VI_LIB vi)
find_library(ISP_LIB isp)
find_library(CCIC_LIB ccic)


if(CAM_SENSORS_LIB AND VI_LIB AND ISP_LIB AND CCIC_LIB)
    message(STATUS "K1 camera libraries found, building vi_k1_cam_plugin")
    message(STATUS "  libcam_sensors: ${CAM_SENSORS_LIB}")
    message(STATUS "  libvi: ${VI_LIB}")
    message(STATUS "  libisp: ${ISP_LIB}")
    message(STATUS "  libccic: ${CCIC_LIB}")

    include_directories(include)

    set(VI_SRCS
        vi_k1.c
        vi_k1_buffer.c
        vi_k1_common.c
        vi_k1_isp.c
        vi_k1_raw.c
        vi_k1_sensor.c
        vi_k1_virtual.c
        vi_k1_ccic.c
        vi_k1_offline.c
    )

    set(VI_PRIVATE_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )

    add_library(vi_k1_cam_plugin SHARED ${VI_SRCS})

    target_include_directories(vi_k1_cam_plugin
        PUBLIC
            ${MPP_INCLUDE_DIRS}
        PRIVATE
            ${VI_PRIVATE_INCLUDE_DIRS}
    )

    target_link_libraries(vi_k1_cam_plugin PUBLIC mpp_v2d vi isp cam_sensors ccic)

    set_target_properties(vi_k1_cam_plugin PROPERTIES
        OUTPUT_NAME "vi_k1_cam_plugin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    add_custom_command(TARGET vi_k1_cam_plugin POST_BUILD
        COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/install_plugin.sh $<TARGET_FILE:vi_k1_cam_plugin>
        COMMENT "Installing libvi_k1_cam_plugin.so to system or user plugin directory"
    )

    install(TARGETS vi_k1_cam_plugin
        LIBRARY DESTINATION lib
    )
else()
    message(STATUS "K1 camera libraries NOT found, skipping vi_k1_cam_plugin")
    if(NOT CAM_SENSORS_LIB)
        message(STATUS "  Missing: libcam_sensors.so")
    endif()
    if(NOT VI_LIB)
        message(STATUS "  Missing: libvi.so")
    endif()
    if(NOT ISP_LIB)
        message(STATUS "  Missing: libisp.so")
    endif()
    if(NOT CCIC_LIB)
        message(STATUS "  Missing: libccic.so")
    endif()
endif()