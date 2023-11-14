#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-11-13 10:10:00
# @Description: the cmake script of al layer.
#------------------------------------------------------------

include_directories(v2d/include)
set(SRC_LIST ./v2d/v2d.c)
add_library(v2d_plugin SHARED ${SRC_LIST})
target_link_libraries(v2d_plugin utils)
install(TARGETS v2d_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
