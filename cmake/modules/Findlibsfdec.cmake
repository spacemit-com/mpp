#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-21 14:05:48
# @LastEditTime: 2023-02-03 15:31:06
# @Description: cmake script for finding libsfdec.
#------------------------------------------------------------

message(STATUS "Using findlibsfdec.cmake...")
find_library(LIB_SFDEC_LIBRARIES sfdec ${CMAKE_FIND_ROOT_PATH}/usr/lib/ ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/)
message(STATUS "Find libsfdec.so in ${LIB_SFDEC_LIBRARIES}")

find_path(LIB_SFDEC_INCLUDE_DIR vpuapi.h ${CMAKE_FIND_ROOT_PATH}/usr/include/ ${CMAKE_FIND_ROOT_PATH}/usr/include/wave511/vpuapi/ ${CMAKE_FIND_ROOT_PATH}/usr/local/include/)
message(STATUS "Find sfdec include file in ${LIB_SFDEC_INCLUDE_DIR}")

if(LIB_SFDEC_LIBRARIES AND LIB_SFDEC_INCLUDE_DIR)
    message(STATUS "sfdec exist !!!")
    option(COMPILE_SFDEC "compile sfdec " ON)
endif()