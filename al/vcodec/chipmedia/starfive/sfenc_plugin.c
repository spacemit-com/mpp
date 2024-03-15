/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-02-01 10:51:34
 * @Description:
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_dec.h"
#include "log.h"
#include "wave420l/config.h"
#include "wave420l/vpuapi/vpuapi.h"
#include "wave420l/vpuapi/vpuapifunc.h"
#include "wave420l/vpuapi/vputypes.h"

#define MODULE_TAG "sfenc"

typedef struct _ALSfEncContext ALSfEncContext;

struct _ALSfEncContext {
  ALEncBaseContext stAlEncBaseContext;
};
