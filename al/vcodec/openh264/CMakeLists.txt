#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-10-24 09:40:45
# @Description: the cmake script of al layer.
#------------------------------------------------------------

if(COMPILE_OPENH264)
message(STATUS "compile soft_openh264")
include_directories(include)
set(SRC_LIST ./openh264dec.cpp
             ./openh264enc.cpp)
add_library(soft_openh264 SHARED ${SRC_LIST})
target_link_libraries(soft_openh264 openh264)
target_link_libraries(soft_openh264 utils)

install(TARGETS soft_openh264 LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
endif()
