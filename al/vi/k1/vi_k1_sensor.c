/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*------------------------------------------------------------------------------
*/

#include <stdlib.h>
#include <stdio.h>

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_sensor.h"
#include "include/vi_k1_common.h"

static BOOL K1_VI_IsSensorCfgMatch(const K1_VI_DEV_CTX_S *pstDevCtx, const SENSOR_CONFIG_S *pstCfg)
{
    if (pstDevCtx == NULL || pstCfg == NULL){
        return MPP_FALSE;
    }

    if ((U32)pstCfg->width != pstDevCtx->stAttr.u32Width ||
        (U32)pstCfg->height != pstDevCtx->stAttr.u32Height){
        return MPP_FALSE;
    }

    if (pstDevCtx->stAttr.u32MipiLaneNum != 0 &&
        (U32)pstCfg->lane_num != pstDevCtx->stAttr.u32MipiLaneNum){
        return MPP_FALSE;
    }

    return MPP_TRUE;
}

static S32 K1_VI_SelectSensorCfg(K1_VI_DEV_CTX_S *pstDevCtx)
{
    U32 i = 0;
    SENSOR_CONFIG_S *pstCfg = NULL;

    if (pstDevCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    for (i = 0; i < (U32)pstDevCtx->stSensorCapability.sensor_capability.snr_config_num; i++) {
        pstCfg = &pstDevCtx->stSensorCapability.sensor_capability.snr_config[i];
        if (K1_VI_IsSensorCfgMatch(pstDevCtx, pstCfg) != MPP_TRUE){
            continue;
        }

        pstDevCtx->pstSensorCfg = pstCfg;
        info("Selected sensor config %u: %ux%u\n", i, pstCfg->width, pstCfg->height);
        info("Selected sensor config minFps maxFps: %f %f bitDepth=%d lane_num=%d\n",
            pstCfg->minFps, pstCfg->maxFps, pstCfg->bitDepth, pstCfg->lane_num);

        return K1_VI_SUCCESS;
    }
    return K1_VI_ERR_INVALID_PARAM;
}

static S32 K1_VI_GetSelectedSensorCfgIndex(const K1_VI_DEV_CTX_S *pstDevCtx)
{
    ptrdiff_t iIndex = 0;

    if (pstDevCtx == NULL ||
        pstDevCtx->pstSensorCfg == NULL ||
        pstDevCtx->stSensorCapability.sensor_capability.snr_config == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    iIndex = pstDevCtx->pstSensorCfg - pstDevCtx->stSensorCapability.sensor_capability.snr_config;
    if (iIndex < 0 ||
        (U32)iIndex >= (U32)pstDevCtx->stSensorCapability.sensor_capability.snr_config_num){
        return K1_VI_ERR_INVALID_PARAM;
    }

    return (S32)iIndex;
}

S32 K1_VI_TryAutoInitSensor(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    SENSOR_CUSTOM_S stSnrCustom;
    int s32Ret = 0;
    int s32Width = 0;
    int s32Height = 0;
    S32 s32CfgIndex = 0;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bSensorInfoValid == MPP_TRUE){
        return K1_VI_SUCCESS;
    }
    if (pstDevCtx->pSensorHandle != NULL){
        return K1_VI_SUCCESS;
    }

    memset(&stSnrCustom, 0, sizeof(stSnrCustom));
    stSnrCustom.dev_id = ViDev;
    stSnrCustom.i2c_addr = -1;
    stSnrCustom.board_id = 1;

    memset(pstDevCtx->szSensorName, 0, sizeof(pstDevCtx->szSensorName));
    info("Detecting sensor for VI dev %u...\n", ViDev);
    s32Ret = SPM_SENSORS_MODULE_Detect_Auto(pstDevCtx->szSensorName, &s32Width, &s32Height, ViDev, stSnrCustom.board_id);
    if (s32Ret != 0){
        return s32Ret;
    }

    pstDevCtx->bSensorDetected = MPP_TRUE;

    if (s32Width > 0 && s32Height > 0) {
        pstDevCtx->stAttr.u32Width = (U32)s32Width;
        pstDevCtx->stAttr.u32Height = (U32)s32Height;
    }

    s32Ret = SPM_SENSORS_MODULE_Init(&pstDevCtx->pSensorHandle,
        pstDevCtx->szSensorName,
        ViDev,
        &pstDevCtx->stSensorModuleInfo,
        stSnrCustom.i2c_addr);
    info("%s: SPM_SENSORS_MODULE_Init %s devId %d, i2cAddr %d, ret = %d\n", __func__, pstDevCtx->szSensorName, ViDev, stSnrCustom.i2c_addr, s32Ret);
    if (s32Ret != 0){
        return s32Ret;
    }

    if (pstDevCtx->stSensorModuleInfo.snr_config_num > 0) {
        pstDevCtx->stSensorCapability.sensor_capability.snr_config_num = pstDevCtx->stSensorModuleInfo.snr_config_num;
        pstDevCtx->stSensorCapability.sensor_capability.snr_config =
            (SENSOR_CONFIG_S *)calloc(1,
            pstDevCtx->stSensorModuleInfo.snr_config_num * sizeof(SENSOR_CONFIG_S));
        if (pstDevCtx->stSensorCapability.sensor_capability.snr_config == NULL) {
            K1_VI_DeInitSensor(ViDev);
            return K1_VI_ERR_BUSY;
        }
    }

    s32Ret = SPM_SENSORS_MODULE_EnumCapability(pstDevCtx->pSensorHandle, &pstDevCtx->stSensorCapability);
    info("%s: SPM_SENSORS_MODULE_EnumCapability ret = %d, config_num = %d\n", __func__, s32Ret, pstDevCtx->stSensorCapability.sensor_capability.snr_config_num);
    if (s32Ret != 0) {
        K1_VI_DeInitSensor(ViDev);
        return s32Ret;
    }

    s32Ret = K1_VI_SelectSensorCfg(pstDevCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        K1_VI_DeInitSensor(ViDev);
        return s32Ret;
    }

    s32CfgIndex = K1_VI_GetSelectedSensorCfgIndex(pstDevCtx);
    if (s32CfgIndex < 0) {
        K1_VI_DeInitSensor(ViDev);
        return s32CfgIndex;
    }

    s32Ret = SPM_SENSOR_Open(pstDevCtx->pSensorHandle, stSnrCustom);

    if (s32Ret != 0) {
        K1_VI_DeInitSensor(ViDev);
        return s32Ret;
    }
    pstDevCtx->bSensorOpened = MPP_TRUE;

    s32Ret = SPM_SENSOR_GetOps(pstDevCtx->pSensorHandle, &pstDevCtx->stSensorOps);

    if (s32Ret != 0) {
        K1_VI_DeInitSensor(ViDev);
        return s32Ret;
    }

    s32Ret = SPM_SENSOR_Config(pstDevCtx->pSensorHandle, s32CfgIndex);

    if (s32Ret != 0) {
        K1_VI_DeInitSensor(ViDev);
        return s32Ret;
    }

    pstDevCtx->bSensorInfoValid = MPP_TRUE;
    pstDevCtx->bSensorAutoManaged = MPP_TRUE;

    return K1_VI_SUCCESS;
}

S32 K1_VI_StartSensor(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    int s32Ret = 0;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bSensorAutoManaged != MPP_TRUE || pstDevCtx->pSensorHandle == NULL){
        return K1_VI_SUCCESS;
    }
    if (pstDevCtx->bSensorStreaming == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    s32Ret = SPM_SENSOR_StreamOn(pstDevCtx->pSensorHandle);
    if (s32Ret != 0){
        return s32Ret;
    }

    pstDevCtx->bSensorStreaming = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StopSensor(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    int s32Ret = 0;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bSensorAutoManaged != MPP_TRUE || pstDevCtx->pSensorHandle == NULL){
        return K1_VI_SUCCESS;
    }
    if (pstDevCtx->bSensorStreaming != MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    s32Ret = SPM_SENSOR_StreamOff(pstDevCtx->pSensorHandle);
    if (s32Ret != 0){
        return s32Ret;
    }

    pstDevCtx->bSensorStreaming = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DeInitSensor(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];

    if (pstDevCtx->bVcmOpened == MPP_TRUE && pstDevCtx->pSensorHandle != NULL) {
        (void)SPM_VCM_Close(pstDevCtx->pSensorHandle);
        pstDevCtx->bVcmOpened = MPP_FALSE;
    }

    if (pstDevCtx->bSensorStreaming == MPP_TRUE){
        (void)K1_VI_StopSensor(ViDev);
    }

    if (pstDevCtx->bSensorOpened == MPP_TRUE && pstDevCtx->pSensorHandle != NULL) {
        (void)SPM_SENSOR_Close(pstDevCtx->pSensorHandle);
        pstDevCtx->bSensorOpened = MPP_FALSE;
    }

    if (pstDevCtx->pSensorHandle != NULL) {
        (void)SPM_SENSORS_MODULE_Deinit(pstDevCtx->pSensorHandle);
        pstDevCtx->pSensorHandle = NULL;
    }

    if (pstDevCtx->stSensorCapability.sensor_capability.snr_config != NULL) {
        free(pstDevCtx->stSensorCapability.sensor_capability.snr_config);
        pstDevCtx->stSensorCapability.sensor_capability.snr_config = NULL;
    }

    pstDevCtx->stSensorCapability.sensor_capability.snr_config_num = 0;
    pstDevCtx->pstSensorCfg = NULL;
    pstDevCtx->bSensorStreaming = MPP_FALSE;
    pstDevCtx->bSensorDetected = MPP_FALSE;

    if (pstDevCtx->bSensorAutoManaged == MPP_TRUE) {
        pstDevCtx->bSensorInfoValid = MPP_FALSE;
        memset(&pstDevCtx->stSensorOps, 0, sizeof(pstDevCtx->stSensorOps));
        memset(&pstDevCtx->stAfOps, 0, sizeof(pstDevCtx->stAfOps));
        memset(pstDevCtx->szSensorName, 0, sizeof(pstDevCtx->szSensorName));
        pstDevCtx->bSensorAutoManaged = MPP_FALSE;
    }

    return K1_VI_SUCCESS;
}
