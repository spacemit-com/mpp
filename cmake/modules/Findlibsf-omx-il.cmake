#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-21 14:05:48
# @LastEditTime: 2023-02-03 15:30:46
# @Description: cmake script for finding libsf-omx-il.
#------------------------------------------------------------

message(STATUS "Using findlibsf-omx-il.cmake...")
find_library(LIB_SFOMX_LIBRARIES sf-omx-il ${CMAKE_FIND_ROOT_PATH}/usr/lib/ ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/)
message(STATUS "Find libsf-omx-il.so in ${LIB_SFOMX_LIBRARIES}")

find_path(LIB_SFOMX_INCLUDE_DIR OMX_Video.h ${CMAKE_FIND_ROOT_PATH}/usr/include/ ${CMAKE_FIND_ROOT_PATH}/usr/include/omx-il/ ${CMAKE_FIND_ROOT_PATH}/usr/local/include/)
message(STATUS "Find sf-omx-il include file in ${LIB_SFOMX_INCLUDE_DIR}")

if(LIB_SFOMX_LIBRARIES AND LIB_SFOMX_INCLUDE_DIR)
    message(STATUS "sf-omx-il exist !!!")
    option(COMPILE_SFOMX "compile sf-omx-il " ON)
endif()