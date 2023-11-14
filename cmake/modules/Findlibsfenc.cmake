#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-21 14:05:48
# @LastEditTime: 2023-02-03 15:31:19
# @Description: cmake script for finding libsfenc.
#------------------------------------------------------------

message(STATUS "Using findlibsfenc.cmake...")
find_library(LIB_SFENC_LIBRARIES sfenc ${CMAKE_FIND_ROOT_PATH}/usr/lib/ ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/)
message(STATUS "Find libsfenc.so in ${LIB_SFENC_LIBRARIES}")

find_path(LIB_SFENC_INCLUDE_DIR vpuapi.h ${CMAKE_FIND_ROOT_PATH}/usr/include/ ${CMAKE_FIND_ROOT_PATH}/usr/include/wave420l/vpuapi/ ${CMAKE_FIND_ROOT_PATH}/usr/local/include/)
message(STATUS "Find sfenc include file in ${LIB_SFENC_INCLUDE_DIR}")

if(LIB_SFENC_LIBRARIES AND LIB_SFENC_INCLUDE_DIR)
    message(STATUS "sfenc exist !!!")
    option(COMPILE_SFENC "compile sfenc " ON)
endif()