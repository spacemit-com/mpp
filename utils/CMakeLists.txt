#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2023-02-02 14:49:35
# @LastEditTime: 2024-01-12 14:42:02
# @Description: the cmake script of utils.
#------------------------------------------------------------

include_directories(include)
include_directories(os/include)
include_directories(${PROJECT_SOURCE_DIR}/include)
#include_directories(../../../linux/include/uapi)

set(SRC_LIST ./log.c
            ./os/linux/os_log.c
            ./frame.c
            ./packet.c
            ./dataqueue.c
            ./v4l2_utils.c
            ./utils.c
            ./resolution_utils.c
            ./dmabufwrapper.c
            ./ringbuffer.c
            ./env.c
            ./os/linux/os_env.c)
add_library(utils STATIC ${SRC_LIST})
