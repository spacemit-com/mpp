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

if(COMPILE_SFDEC)
message(STATUS "compile sfdec codec")
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/include/wave511/vpuapi)
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/local/include)
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/include)
set(SRC_LIST ./starfive/sfdec_plugin.c)
add_library(sfdec_plugin SHARED ${SRC_LIST})
target_link_libraries(sfdec_plugin utils)
target_link_libraries(sfdec_plugin sfdec)
install(TARGETS sfdec_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
endif()

if(COMPILE_SFENC)
message(STATUS "compile sfenc codec")
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/include/wave420l/vpuapi)
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/local/include)
include_directories(${CMAKE_FIND_ROOT_PATH}/usr/include)
set(SRC_LIST ./starfive/sfenc_plugin.c)
add_library(sfenc_plugin SHARED ${SRC_LIST})
target_link_libraries(sfenc_plugin utils)
target_link_libraries(sfenc_plugin sfenc)
install(TARGETS sfenc_plugin LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
endif()
