#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:03:57
# @LastEditTime: 2023-02-03 15:27:06
# @Description: the cmake script of mpi.
#------------------------------------------------------------

include_directories(include)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/al/include)
include_directories(${PROJECT_SOURCE_DIR}/utils/include)
include_directories(../../../linux/include/uapi)

set(SRC_LIST ./sys.c
	     ./vdec.c
	     ./venc.c
	     ./g2d.c
	     ./module.c)
add_library(spacemit_mpp SHARED ${SRC_LIST})
target_link_libraries(spacemit_mpp  utils)
target_link_libraries(spacemit_mpp  dl)
target_link_libraries(spacemit_mpp  pthread)
SET_TARGET_PROPERTIES(spacemit_mpp  PROPERTIES VERSION 0.0.15 SOVERSION 0)

option(VC8000 "vc8000" OFF)
option(SOFT_OPENH264 "openh264" ON)
option(LINLONV5V7 "linlonv5v7" OFF)

if(VC8000)
target_link_libraries(spacemit_mpp vc8000)
endif()

if(LINLONV5V7)
target_link_libraries(spacemit_mpp linlonv5v7)
endif()

if(SOFT_OPENH264 AND COMPILE_OPENH264)
message(STATUS "core link soft_openh264 so")
#target_link_libraries(spacemit_mpp soft_openh264)
endif()

install(TARGETS spacemit_mpp LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
