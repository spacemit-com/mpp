#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2023-02-02 09:39:40
# @LastEditTime: 2023-11-09 15:58:46
# @Description: the main cmake script of mpp.
#------------------------------------------------------------

message("CMAKE_VERSION:       ${CMAKE_VERSION}")
message("CMAKE_MAJOR_VERSION: ${CMAKE_MAJOR_VERSION}")
message("CMAKE_MINOR_VERSION: ${CMAKE_MINOR_VERSION}")
message("CMAKE_PATCH_VERSION: ${CMAKE_PATCH_VERSION}")

if(${CMAKE_VERSION} VERSION_LESS "2.8.8")
    message("Please consider to switch to CMake 2.8.8 or Later")
endif()

cmake_minimum_required(VERSION 2.8.8)

project(spacemit_mpp)

add_definitions(-fPIC -Werror)
#add_definitions(-fPIC -Werror -fsanitize=address -fno-omit-frame-pointer -g)

#set(CMAKE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
#set(CMAKE_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer")

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/modules)
find_package(libopenh264    REQUIRED)
find_package(libavcodec     REQUIRED)
find_package(libsfdec       REQUIRED)
find_package(libsfenc       REQUIRED)
find_package(libsf-omx-il   REQUIRED)

add_subdirectory(mpi)
add_subdirectory(al)
add_subdirectory(utils)
add_subdirectory(test)

# set(CMAKE_INSTALL_PREFIX /usr)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
        FILES_MATCHING PATTERN "*.h"
        )

install(DIRECTORY ${PROJECT_SOURCE_DIR}/mpi/include/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
        FILES_MATCHING PATTERN "*.h"
        )
install(DIRECTORY ${PROJECT_SOURCE_DIR}/utils/include/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
        FILES_MATCHING PATTERN "*.h"
        )
install(DIRECTORY ${PROJECT_SOURCE_DIR}/al/include/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
        FILES_MATCHING PATTERN "*.h"
        )



message(STATUS "CMAKE_INSTALL_INCLUDEDIR:  ${CMAKE_INSTALL_INCLUDEDIR}")
message(STATUS "CMAKE_INSTALL_LIBDIR:  ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "CMAKE_INSTALL_PREFIX:  ${CMAKE_INSTALL_PREFIX}")
message(STATUS "CMAKE_BINARY_DIR:  ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_FIND_ROOT_PATH:  ${CMAKE_FIND_ROOT_PATH}")
message(STATUS "CMAKE_SOURCE_DIR:  ${CMAKE_SOURCE_DIR}")

CONFIGURE_FILE("${PROJECT_SOURCE_DIR}/pkgconfig/spacemit_mpp.pc.cmake"
        "${CMAKE_BINARY_DIR}/spacemit_mpp.pc" @ONLY)

install(FILES "${CMAKE_BINARY_DIR}/spacemit_mpp.pc"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig/")
