#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-21 14:05:48
# @LastEditTime: 2023-10-23 15:28:03
# @Description: cmake script for finding libopenh264.
#------------------------------------------------------------

message(STATUS "Using findlibopenh264.cmake...")
find_library(LIB_OPENH264_LIBRARIES openh264 ${CMAKE_FIND_ROOT_PATH}/usr/lib/
	                                     ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/
					     ${CMAKE_FIND_ROOT_PATH}/usr/lib/riscv64-linux-gnu/
					     ${CMAKE_FIND_ROOT_PATH}/usr/local/lib/riscv64-linux-gnu/)
message(STATUS "Find libopenh264.so in ${LIB_OPENH264_LIBRARIES}")

if(LIB_OPENH264_LIBRARIES)
    message(STATUS "openh264 exist !!!")
    option(COMPILE_OPENH264 "compile openh264 " ON)
endif()
