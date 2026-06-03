# ============================================================
# MPP (Media Processing Platform) - Top-level CMakeLists
# Copyright 2025-2026 SPACEMIT. All rights reserved.
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(mpp
    VERSION 1.0.0
    DESCRIPTION "SPACEMIT Media Processing Platform"
    LANGUAGES C CXX
)

# ────────────────────────────────────────────────────────────
# Global build options
# ────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static libraries are linked into PIE executables / DSOs; PIC avoids RISC-V
# linker errors (e.g. R_RISCV_CALL_PLT) when pulling .a into final links.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "Build type: Debug | Release | RelWithDebInfo | MinSizeRel" FORCE)
endif()

# Install prefix default
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

# ────────────────────────────────────────────────────────────
# Compiler flags
# ────────────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -Wno-unused-function
    -Wno-unused-variable
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -DDEBUG)
else()
    add_compile_options(-O2 -DNDEBUG)
endif()

# ────────────────────────────────────────────────────────────
# CMake modules path
# ────────────────────────────────────────────────────────────
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ────────────────────────────────────────────────────────────
# Global include directories (available to all sub-targets)
# ────────────────────────────────────────────────────────────
set(MPP_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/stream"
    "${CMAKE_CURRENT_SOURCE_DIR}/utils/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/al/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vi"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vo"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/venc"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vdec"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/demux"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/mux"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/v2d"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/uvc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mpi/sys"
)

# ────────────────────────────────────────────────────────────
# Testing support
# ────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build API unit tests and test executables" ON)
set(BUILD_ROS2_EXAMPLES "OFF" CACHE STRING "Build ROS2 examples: AUTO | ON | OFF")
set_property(CACHE BUILD_ROS2_EXAMPLES PROPERTY STRINGS AUTO ON OFF)

if(BUILD_TESTS)
    enable_testing()
endif()

# ────────────────────────────────────────────────────────────
# Optional ROS2 examples
# ────────────────────────────────────────────────────────────
set(MPP_ROS2_WS_DIR "${CMAKE_BINARY_DIR}/ros2_ws" CACHE PATH "ROS2 colcon workspace for MPP examples")

set(MPP_ROS2_AVAILABLE OFF)
find_program(COLCON_EXECUTABLE colcon)
find_file(ROS2_SETUP_BASH setup.bash
    PATHS
        ENV AMENT_PREFIX_PATH
        /opt/ros/humble_prebuilt
        /opt/ros/humble
        /opt/ros/iron
        /opt/ros/jazzy
        /opt/ros/rolling
    PATH_SUFFIXES ""
    NO_DEFAULT_PATH
)

if(COLCON_EXECUTABLE AND ROS2_SETUP_BASH)
    get_filename_component(ROS2_SETUP_DIR "${ROS2_SETUP_BASH}" DIRECTORY)
    set(MPP_ROS2_AVAILABLE ON)
endif()

string(TOUPPER "${BUILD_ROS2_EXAMPLES}" BUILD_ROS2_EXAMPLES_UPPER)
if(NOT BUILD_ROS2_EXAMPLES_UPPER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "BUILD_ROS2_EXAMPLES must be AUTO, ON, or OFF")
endif()

set(MPP_BUILD_ROS2_EXAMPLES OFF)
if(BUILD_ROS2_EXAMPLES_UPPER STREQUAL "ON")
    if(NOT MPP_ROS2_AVAILABLE)
        message(FATAL_ERROR "BUILD_ROS2_EXAMPLES=ON but ROS2 setup.bash or colcon was not found")
    endif()
    set(MPP_BUILD_ROS2_EXAMPLES ON)
elseif(BUILD_ROS2_EXAMPLES_UPPER STREQUAL "AUTO" AND MPP_ROS2_AVAILABLE)
    set(MPP_BUILD_ROS2_EXAMPLES ON)
endif()

if(MPP_BUILD_ROS2_EXAMPLES)
    add_custom_target(build_ros2_examples ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MPP_ROS2_WS_DIR}/src"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MPP_ROS2_WS_DIR}/src"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_SOURCE_DIR}/test/ros2_mpp_msgs" "${MPP_ROS2_WS_DIR}/src/mpp_msgs"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_CURRENT_SOURCE_DIR}/test/ros2_mpp_isp_camera" "${MPP_ROS2_WS_DIR}/src/mpp_isp_camera_ros2"
        COMMAND /bin/bash -lc "source '${ROS2_SETUP_BASH}' && cd '${MPP_ROS2_WS_DIR}' && '${COLCON_EXECUTABLE}' build --event-handlers console_direct+ --cmake-args -DMPP_ROOT='${CMAKE_CURRENT_SOURCE_DIR}' -DMPP_BUILD_ROOT='${CMAKE_BINARY_DIR}'"
        DEPENDS mpp
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building ROS2 examples in ${MPP_ROS2_WS_DIR}"
        VERBATIM
    )
endif()

# ────────────────────────────────────────────────────────────
# Subdirectories
# ────────────────────────────────────────────────────────────
add_subdirectory(thirdparty)
add_subdirectory(utils)
add_subdirectory(al)
add_subdirectory(mpi)

add_subdirectory(sample)

if(BUILD_TESTS)
	add_subdirectory(test)
endif()

# ────────────────────────────────────────────────────────────
# Summary
# ────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "========== MPP Build Configuration ==========")
message(STATUS "  Version      : ${PROJECT_VERSION}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C standard   : C${CMAKE_C_STANDARD}")
message(STATUS "  Install to   : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build tests  : ${BUILD_TESTS}")
message(STATUS "  ROS2 examples: ${BUILD_ROS2_EXAMPLES_UPPER} (available=${MPP_ROS2_AVAILABLE}, build=${MPP_BUILD_ROS2_EXAMPLES})")
if(MPP_BUILD_ROS2_EXAMPLES)
message(STATUS "  ROS2 setup   : ${ROS2_SETUP_BASH}")
message(STATUS "  ROS2 ws      : ${MPP_ROS2_WS_DIR}")
endif()
message(STATUS "=============================================")
message(STATUS "")
