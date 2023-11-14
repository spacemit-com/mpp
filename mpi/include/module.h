/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-01 15:24:32
 * @Description:
 */

#ifndef _MPP_MODULE_H_
#define _MPP_MODULE_H_

#include <dlfcn.h>

#include "frame.h"
#include "packet.h"

#define MAX_PATH_LENGTH 2048

typedef struct _MppModule MppModule;

/**
 * @description:
 * @param {MppCodecType} codec_type
 * @return {*}
 */
MppModule* module_init(MppCodecType codec_type);

/***
 * @description:
 * @return {*}
 */
MppModule* module_auto_init();

/**
 * @description:
 * @param {MppModule} *module
 * @return {*}
 */
void module_destory(MppModule* module);

/**
 * @description:
 * @param {MppModule} *module
 * @return {*}
 */
void* module_get_so_path(MppModule* module);

#endif /*_MPP_MODULE_H_*/