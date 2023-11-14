#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-28 11:13:50
# @LastEditTime: 2023-10-24 14:45:28
# @Description: the cmake script of al layer.
#------------------------------------------------------------

if(COMPILE_AVCODEC)
message(STATUS "compile ffmpeg codec")
include_directories(include)
set(SRC_LIST ./ffmpegdec.c
	    	 ./ffmpegenc.c
	    	 ./ffmpegswscale.c)
add_library(ffmpegcodec SHARED ${SRC_LIST})
target_link_libraries(ffmpegcodec utils)
target_link_libraries(ffmpegcodec avcodec)
target_link_libraries(ffmpegcodec swscale)
target_link_libraries(ffmpegcodec avutil)

install(TARGETS ffmpegcodec LIBRARY
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")

endif()
