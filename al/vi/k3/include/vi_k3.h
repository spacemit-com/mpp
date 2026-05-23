/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#ifndef VI_K3_H
#define VI_K3_H

#include "vi_api.h"

S32 K3_VI_Init(VOID);
S32 K3_VI_DeInit(VOID);
S32 K3_VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);
S32 K3_VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr);
S32 K3_VI_EnableDev(VI_DEV ViDev);
S32 K3_VI_DisableDev(VI_DEV ViDev);
S32 K3_VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr);
S32 K3_VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr);
S32 K3_VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn);
S32 K3_VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn);

#endif /* VI_K3_H */
