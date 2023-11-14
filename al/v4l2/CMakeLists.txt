#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-10-13 17:25:25
# @Description: the cmake script of al layer.
#------------------------------------------------------------

include_directories(linlonv5v7/include)
set(SRC_LIST ./linlonv5v7/linlonv5v7_dec.c
                ./linlonv5v7/linlonv5v7_enc.c
                ./linlonv5v7/linlonv5v7_codec.c
                ./linlonv5v7/linlonv5v7_buffer.c
                ./linlonv5v7/linlonv5v7_port.c)
add_library(v4l2_linlonv5v7_codec SHARED ${SRC_LIST})
target_link_libraries(v4l2_linlonv5v7_codec utils)
install(TARGETS v4l2_linlonv5v7_codec LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")

install(DIRECTORY linlonv5v7/include/
        DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
        FILES_MATCHING PATTERN "*.h"
        )

set(SRC_LIST ./standard/v4l2dec.c
	     ./standard/v4l2enc.c)
add_library(v4l2_standard_codec SHARED ${SRC_LIST})
target_link_libraries(v4l2_standard_codec utils)
install(TARGETS v4l2_standard_codec LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
