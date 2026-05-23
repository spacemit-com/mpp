/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-18 11:46:03
 * @LastEditTime: 2026-04-20 11:10:23
 * @Description: MPP VDEC API, use these API to do video decode
 *               from stream(H.264 etc.) to frame(YUV420)
 */

#define ENABLE_DEBUG 0

#include "vdec.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "log.h"

#define MODULE_TAG "mpp_vdec"

static ALBaseContext *(*dec_create)();
static S32 (*dec_init)(ALBaseContext *ctx, MppVdecPara *para);
static S32 (*dec_getparam)(ALBaseContext *ctx, MppVdecPara **para);
static S32 (*dec_request_input_stream)(ALBaseContext *ctx, MppData *sink_data);
static S32 (*dec_return_input_stream)(ALBaseContext *ctx, MppData *sink_data);
static S32 (*dec_decode)(ALBaseContext *ctx, MppData *sink_data);
static S32 (*dec_process)(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);
static S32 (*dec_get_output_frame)(ALBaseContext *ctx, MppData *src_data);
static S32 (*dec_request_output_frame)(ALBaseContext *ctx, MppData *src_data);
static S32 (*dec_request_output_frame_2)(ALBaseContext *ctx, MppData **src_data, U32 u32TimeoutMs);
static S32 (*dec_return_output_frame)(ALBaseContext *ctx, MppData *src_data);
static S32 (*dec_queue_output_buffer)(ALBaseContext *ctx, MppData *src_data);
static S32 (*dec_flush)(ALBaseContext *ctx);
static S32 (*dec_reset)(ALBaseContext *ctx);
static void (*dec_destory)(ALBaseContext *ctx);

MppVdecCtx *vdec_ctx_create() {
    MppVdecCtx *ctx = (MppVdecCtx *)malloc(sizeof(MppVdecCtx));
    if (!ctx) {
        error("can not create MppVdecCtx, please check! (%s)", strerror(errno));
        return NULL;
    }

    memset(ctx, 0, sizeof(MppVdecCtx));
    vdec_ctx_get_default_param(ctx);

    debug("create VDEC Channel success!");
    return ctx;
}

S32 vdec_ctx_init(MppVdecCtx *ctx) {
    S32 ret = 0;
    void *handle;

    ctx->pModule = module_init(ctx->eCodecType);
    if (!ctx->pModule) {
        error("module_init failed for codec type %d", (int)ctx->eCodecType);
        return MPP_INIT_FAILED;
    }

    handle = module_get_so_path(ctx->pModule);
    if (!handle) {
        error("module handle is NULL after module_init");
        module_destory(ctx->pModule);
        ctx->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    dlerror();
    dec_create = (ALBaseContext * (*)()) dlsym(handle, "al_dec_create");
    dec_init = (S32 (*)(ALBaseContext *ctx, MppVdecPara *para))dlsym(handle, "al_dec_init");
    dec_getparam = (S32 (*)(ALBaseContext *ctx, MppVdecPara **para))dlsym(handle, "al_dec_getparam");
    dec_request_input_stream =
        (S32 (*)(ALBaseContext *ctx, MppData *sink_data))dlsym(handle, "al_dec_request_input_stream");
    dec_return_input_stream =
        (S32 (*)(ALBaseContext *ctx, MppData *sink_data))dlsym(handle, "al_dec_return_input_stream");
    dec_decode = (S32 (*)(ALBaseContext *ctx, MppData *sink_data))dlsym(handle, "al_dec_decode");
    dec_process = (S32 (*)(ALBaseContext *ctx, MppData *sink_data, MppData *src_data))dlsym(handle, "al_dec_process");
    dec_get_output_frame = (S32 (*)(ALBaseContext *ctx, MppData *src_data))dlsym(handle, "al_dec_get_output_frame");
    dec_request_output_frame =
        (S32 (*)(ALBaseContext *ctx, MppData *src_data))dlsym(handle, "al_dec_request_output_frame");
    dec_request_output_frame_2 = (S32 (*)(ALBaseContext *ctx, MppData **src_data, U32 u32TimeoutMs))dlsym(
        handle, "al_dec_request_output_frame_2");
    if (!dec_request_output_frame_2) {
        const char *derr = dlerror();
        error(
            "plugin must export al_dec_request_output_frame_2 (timeout-based "
            "output); dlsym: %s",
            derr ? derr : "(no dlerror message)");
        module_destory(ctx->pModule);
        ctx->pModule = NULL;
        return MPP_INIT_FAILED;
    }
    dec_return_output_frame =
        (S32 (*)(ALBaseContext *ctx, MppData *src_data))dlsym(handle, "al_dec_return_output_frame");
    dec_queue_output_buffer =
        (S32 (*)(ALBaseContext *ctx, MppData *src_data))dlsym(handle, "al_dec_queue_output_buffer");
    dec_destory = (void (*)(ALBaseContext *ctx))dlsym(handle, "al_dec_destory");
    dec_flush = (S32 (*)(ALBaseContext *ctx))dlsym(handle, "al_dec_flush");
    dec_reset = (S32 (*)(ALBaseContext *ctx))dlsym(handle, "al_dec_reset");

    if (!dec_create || !dec_init || !dec_decode || !dec_return_output_frame || !dec_destory) {
        error(
            "required decoder symbols missing from plugin (create/init/decode/"
            "return/destroy)");
        module_destory(ctx->pModule);
        ctx->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ctx->pAlBaseContext = dec_create();
    if (!ctx->pAlBaseContext) {
        error("al_dec_create returned NULL");
        module_destory(ctx->pModule);
        ctx->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ret = dec_init(ctx->pAlBaseContext, &(ctx->stVdecPara));
    debug("init VDEC Channel, ret = %d", ret);

    return ret;
}

S32 vdec_ctx_set_param(MppVdecCtx *ctx) {
    error("VDEC_SetParam is not supported yet, return MPP_OK directly!");
    return MPP_OK;
}

S32 vdec_ctx_get_param(MppVdecCtx *ctx, MppVdecPara **stVdecPara) {
    S32 ret = 0;
    ret = dec_getparam(ctx->pAlBaseContext, stVdecPara);
    debug("get VDEC parameters, ret = %d", ret);

    return ret;
}

S32 vdec_ctx_get_default_param(MppVdecCtx *ctx) {
    ctx->stVdecPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
    // ctx->stVdecPara.bScaleDownEn = MPP_FALSE;
    ctx->stVdecPara.nScale = 1;
    ctx->stVdecPara.nHorizonScaleDownRatio = 1;
    ctx->stVdecPara.nVerticalScaleDownRatio = 1;
    // ctx->stVdecPara.bRotationEn = MPP_FALSE;
    ctx->stVdecPara.nRotateDegree = 0;
    ctx->stVdecPara.bThumbnailMode = 0;
    ctx->stVdecPara.bIsInterlaced = MPP_FALSE;
    ctx->stVdecPara.bIsFrameReordering = MPP_TRUE;
    ctx->stVdecPara.bIgnoreStreamHeaders = MPP_FALSE;
    ctx->stVdecPara.eOutputPixelFormat = MPP_PIXEL_FORMAT_I420;
    ctx->stVdecPara.bNoBFrames = MPP_FALSE;
    ctx->stVdecPara.bDisable3D = MPP_FALSE;
    ctx->stVdecPara.bSupportMaf = MPP_FALSE;
    ctx->stVdecPara.bDispErrorFrame = MPP_TRUE;
    ctx->stVdecPara.bInputBlockModeEnable = MPP_FALSE;
    ctx->stVdecPara.bOutputBlockModeEnable = MPP_TRUE;

    return MPP_OK;
}

S32 handle_vdec_data(ALBaseContext *base_context, MppData *sink_data) {
    S32 ret = 0;
    ret = dec_decode(base_context, sink_data);

    return ret;
}

S32 vdec_ctx_decode(MppVdecCtx *ctx, MppData *sink_data) {
    S32 ret = 0;
    ret = handle_vdec_data(ctx->pAlBaseContext, sink_data);
    debug("decode one packet, ret = %d", ret);

    return ret;
}

S32 process_vdec_data(ALBaseContext *base_context, MppData *sink_data, MppData *src_data) {
    S32 ret = 0;
    ret = dec_process(base_context, sink_data, src_data);

    return ret;
}

S32 vdec_ctx_process(MppVdecCtx *ctx, MppData *sink_data, MppData *src_data) {
    S32 ret = 0;
    ret = process_vdec_data(ctx->pAlBaseContext, sink_data, src_data);

    return ret;
}

S32 get_vdec_result_sync(ALBaseContext *base_context, MppData *src_data) {
    S32 ret = 0;
    ret = dec_get_output_frame(base_context, src_data);

    return ret;
}

S32 vdec_ctx_get_output_frame(MppVdecCtx *ctx, MppData *src_data) {
    S32 ret = 0;
    ret = get_vdec_result_sync(ctx->pAlBaseContext, src_data);

    return ret;
}

S32 get_vdec_result(ALBaseContext *base_context, MppData *src_data) {
    S32 ret = 0;
    ret = dec_request_output_frame(base_context, src_data);

    return ret;
}

S32 get_vdec_result_2(ALBaseContext *base_context, MppData **src_data, U32 u32TimeoutMs) {
    if (!src_data) {
        return MPP_NULL_POINTER;
    }
    if (!dec_request_output_frame_2) {
        error("dec_request_output_frame_2 is NULL (init should have failed)");
        return MPP_INIT_FAILED;
    }
    return dec_request_output_frame_2(base_context, src_data, u32TimeoutMs);
}

S32 vdec_ctx_request_output_frame(MppVdecCtx *ctx, MppData *src_data) {
    S32 ret = 0;
    ret = get_vdec_result(ctx->pAlBaseContext, src_data);

    return ret;
}

S32 vdec_ctx_request_output_frame_2(MppVdecCtx *ctx, MppData **src_data, U32 u32TimeoutMs) {
    S32 ret = 0;
    ret = get_vdec_result_2(ctx->pAlBaseContext, src_data, u32TimeoutMs);

    return ret;
}

S32 return_vdec_result(ALBaseContext *base_context, MppData *src_data) {
    S32 ret = 0;
    ret = dec_return_output_frame(base_context, src_data);

    return ret;
}

S32 vdec_ctx_return_output_frame(MppVdecCtx *ctx, MppData *src_data) {
    S32 ret = 0;
    ret = return_vdec_result(ctx->pAlBaseContext, src_data);

    return ret;
}

S32 vdec_ctx_queue_output_buffer(MppVdecCtx *ctx, MppData *src_data) {
    S32 ret = 0;
    ret = dec_queue_output_buffer(ctx->pAlBaseContext, src_data);

    return ret;
}

S32 vdec_ctx_flush(MppVdecCtx *ctx) {
    S32 ret = 0;

    debug("begin flush!");
    ret = dec_flush(ctx->pAlBaseContext);
    debug("finish flush ret = %d", ret);

    return ret;
}

S32 vdec_ctx_destroy(MppVdecCtx *ctx) {
    if (!ctx) {
        error("input para ctx is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (ctx->pModule == NULL) {
        info("module not init!");
        free(ctx);
        return 0;
    }

    if (dec_destory)
        dec_destory(ctx->pAlBaseContext);
    debug("finish destory decoder");

    module_destory(ctx->pModule);
    debug("finish destory module");

    free(ctx);
    // ctx = NULL;

    return MPP_OK;
}

S32 vdec_ctx_reset(MppVdecCtx *ctx) {
    S32 ret = 0;
    ret = dec_reset(ctx->pAlBaseContext);

    return ret;
}
