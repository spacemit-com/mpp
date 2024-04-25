#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-08-03 20:10:39
# @Description: the cmake script of al layer.
#------------------------------------------------------------

include_directories(v4l2/include)
set(SRC_LIST ./v4l2/vi_v4l2.c)
add_library(vi_v4l2_plugin SHARED ${SRC_LIST})
target_link_libraries(vi_v4l2_plugin utils)
install(TARGETS vi_v4l2_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")

include_directories(k1/cam/include)
set(SRC_LIST ./k1/cam/vi_k1_cam.c)
add_library(vi_k1_cam_plugin SHARED ${SRC_LIST})
target_link_libraries(vi_k1_cam_plugin utils)
install(TARGETS vi_k1_cam_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
