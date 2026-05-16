/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vi_k1_sensor.h
* @Date      :    2026-3-30
* @Author    :    SPACEMIT
* @Brief     :    K1 VI sensor helpers.
*------------------------------------------------------------------------------
*/

#ifndef VI_K1_SENSOR_H
#define VI_K1_SENSOR_H

#include "vi_k1_ctx.h"

S32 K1_VI_TryAutoInitSensor(VI_DEV ViDev);
S32 K1_VI_DeInitSensor(VI_DEV ViDev);
S32 K1_VI_StartSensor(VI_DEV ViDev);
S32 K1_VI_StopSensor(VI_DEV ViDev);

#endif /* VI_K1_SENSOR_H */
