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

set(SRC_LIST ./vi_file_vdec_vo_test.c)
add_executable(vi_file_vdec_vo_test ${SRC_LIST})
target_link_libraries(vi_file_vdec_vo_test spacemit_mpp)

set(SRC_LIST ./vi_file_vdec_venc_sync_userptr_vo_file_test.c)
add_executable(vi_file_vdec_venc_sync_userptr_vo_file_test ${SRC_LIST})
target_link_libraries(vi_file_vdec_venc_sync_userptr_vo_file_test spacemit_mpp)

set(SRC_LIST ./vi_file_venc_sync_userptr_vo_file_test.c)
add_executable(vi_file_venc_sync_userptr_vo_file_test ${SRC_LIST})
target_link_libraries(vi_file_venc_sync_userptr_vo_file_test spacemit_mpp)

set(SRC_LIST ./g2d_test.c)
add_executable(g2d_test ${SRC_LIST})
target_link_libraries(g2d_test spacemit_mpp)

set(SRC_LIST ./vi_v4l2_vo_test.c)
add_executable(vi_v4l2_vo_test ${SRC_LIST})
target_link_libraries(vi_v4l2_vo_test spacemit_mpp)

install(TARGETS vi_file_vdec_vo_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vi_file_vdec_venc_sync_userptr_vo_file_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vi_file_venc_sync_userptr_vo_file_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS g2d_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
install(TARGETS vi_v4l2_vo_test RUNTIME
        DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
