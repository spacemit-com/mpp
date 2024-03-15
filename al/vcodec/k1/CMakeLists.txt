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

include_directories(jpu/include)
set(SRC_LIST ./jpu/jpudec.c ./jpu/jpuenc.c)
add_library(jpu_plugin SHARED ${SRC_LIST})
target_link_libraries(jpu_plugin utils)
install(TARGETS jpu_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
