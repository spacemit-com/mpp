#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-11-02 18:57:24
# @Description: the cmake script of al layer.
#------------------------------------------------------------

#if(COMPILE_SFOMX)
message(STATUS "compile sfomx codec")
include_directories(include)
include_directories(include/khronos)
set(SRC_LIST ./starfive/sfomxil_dec_plugin.c
             ./starfive/sfomxil_enc_plugin.c)
add_library(sfomxil_plugin SHARED ${SRC_LIST})
target_link_libraries(sfomxil_plugin utils)
#target_link_libraries(sfomxil_plugin sf-omx-il)
install(TARGETS sfomxil_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
#endif()

