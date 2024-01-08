#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2023-02-01 15:56:51
# @LastEditTime: 2023-02-03 15:29:48
# @Description: cmake script for finding libavcodec.
#------------------------------------------------------------

message(STATUS "Using findlibavcodec.cmake...")
find_library(LIB_AVCODEC_LIBRARIES avcodec ${CMAKE_FIND_ROOT_PATH}/usr/lib/
					   ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/
					   ${CMAKE_FIND_ROOT_PATH}/usr/local/ffmpeg/lib/
					   ${CMAKE_FIND_ROOT_PATH}/usr/lib/riscv64-linux-gnu/
					   ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/riscv64-linux-gnu/)
message(STATUS "Find libavcodec.so in ${LIB_AVCODEC_LIBRARIES}")

if(LIB_AVCODEC_LIBRARIES)
    message(STATUS "avcodec exist !!!")
#   option(COMPILE_AVCODEC "compile avcodec" ON)
endif()
