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

include_directories(sdl2/include)
set(SRC_LIST ./sdl2/vo_sdl2.c)
add_library(vo_sdl2_plugin SHARED ${SRC_LIST})
target_link_libraries(vo_sdl2_plugin utils)
target_link_libraries(vo_sdl2_plugin SDL2)
install(TARGETS vo_sdl2_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")