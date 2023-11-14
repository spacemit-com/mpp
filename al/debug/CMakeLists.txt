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

message(STATUS "compile fake dec")
set(SRC_LIST ./fake_dec_plugin.c)
add_library(fake_dec_plugin SHARED ${SRC_LIST})
target_link_libraries(fake_dec_plugin utils)

install(TARGETS fake_dec_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
