#------------------------------------------------------------
# @Copyright 2022-2023 SPACEMIT. All rights reserved.
# @Use of this source code is governed by a BSD-style license
# @that can be found in the LICENSE file.
# @
# @Author: David(qiang.fu@spacemit.com)
# @Date: 2022-12-22 14:56:49
# @LastEditTime: 2023-11-02 18:01:15
# @Description: the cmake script of test.
#------------------------------------------------------------

include_directories(include)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/mpi/include)
include_directories(${PROJECT_SOURCE_DIR}/al/include)
include_directories(${PROJECT_SOURCE_DIR}/utils/include)

set(SRC_LIST ./test_sys_vdec_venc_one_frame.c)
add_executable(test_sys_vdec_venc_one_frame ${SRC_LIST})
target_link_libraries(test_sys_vdec_venc_one_frame spacemit_mpp)

set(SRC_LIST ./test_sys_venc_vdec_one_frame.c)
add_executable(test_sys_venc_vdec_one_frame ${SRC_LIST})
target_link_libraries(test_sys_venc_vdec_one_frame spacemit_mpp)

set(SRC_LIST ./test_sys_vdec_venc_vdec_one_frame.c)
add_executable(test_sys_vdec_venc_vdec_one_frame ${SRC_LIST})
target_link_libraries(test_sys_vdec_venc_vdec_one_frame spacemit_mpp)

set(SRC_LIST ./vdec_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(vdec_test ${SRC_LIST})
target_link_libraries(vdec_test spacemit_mpp)

set(SRC_LIST ./vdec_vo_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(vdec_vo_test ${SRC_LIST})
target_link_libraries(vdec_vo_test spacemit_mpp)

set(SRC_LIST ./vdec_venc_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(vdec_venc_test ${SRC_LIST})
target_link_libraries(vdec_venc_test spacemit_mpp)

set(SRC_LIST ./vdec_venc_sync_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(vdec_venc_sync_test ${SRC_LIST})
target_link_libraries(vdec_venc_sync_test spacemit_mpp)

set(SRC_LIST ./vdec_venc_sync_userptr_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(vdec_venc_sync_userptr_test ${SRC_LIST})
target_link_libraries(vdec_venc_sync_userptr_test spacemit_mpp)

set(SRC_LIST ./venc_sync_userptr_test.c ./parse/h264parse.c ./parse/h265parse.c ./parse/mjpegparse.c ./parse/defaultparse.c ./parse/parse.c)
add_executable(venc_sync_userptr_test ${SRC_LIST})
target_link_libraries(venc_sync_userptr_test spacemit_mpp)

set(SRC_LIST ./venc_test.c)
add_executable(venc_test ${SRC_LIST})
target_link_libraries(venc_test spacemit_mpp)

set(SRC_LIST ./g2d_test.c)
add_executable(g2d_test ${SRC_LIST})
target_link_libraries(g2d_test spacemit_mpp)

install(TARGETS vdec_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vdec_vo_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS venc_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vdec_venc_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vdec_venc_sync_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vdec_venc_sync_userptr_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS venc_sync_userptr_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS g2d_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
