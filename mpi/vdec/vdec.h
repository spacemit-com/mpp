/*
* Copyright 2022-2023 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @Description: Legacy VDEC context API (vdec_ctx_*). Use this tree's copy so
*               builds do not pick up a different vdec.h from /usr/include.
*/

#ifndef VDEC_H
#define VDEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "data.h"
#include "frame.h"
#include "module.h"
#include "packet.h"
#include "processflow.h"
#include "type.h"

typedef struct _MppVdecCtx {
    MppProcessNode pNode;
    MppModuleType eCodecType;
    MppModule *pModule;
    MppVdecPara stVdecPara;
} MppVdecCtx;

MppVdecCtx *vdec_ctx_create(void);

S32 vdec_ctx_init(MppVdecCtx *ctx);

S32 vdec_ctx_set_param(MppVdecCtx *ctx);

S32 vdec_ctx_get_param(MppVdecCtx *ctx, MppVdecPara **stVdecPara);

S32 vdec_ctx_get_default_param(MppVdecCtx *ctx);

S32 vdec_ctx_decode(MppVdecCtx *ctx, MppData *sink_data);

S32 vdec_ctx_process(MppVdecCtx *ctx, MppData *sink_data, MppData *src_data);

S32 vdec_ctx_get_output_frame(MppVdecCtx *ctx, MppData *src_data);

S32 vdec_ctx_request_output_frame(MppVdecCtx *ctx, MppData *src_data);

S32 vdec_ctx_request_output_frame_2(MppVdecCtx *ctx, MppData **src_data,
    U32 u32TimeoutMs);

S32 vdec_ctx_return_output_frame(MppVdecCtx *ctx, MppData *src_data);

S32 vdec_ctx_queue_output_buffer(MppVdecCtx *ctx, MppData *src_data);

S32 vdec_ctx_flush(MppVdecCtx *ctx);

S32 vdec_ctx_destroy(MppVdecCtx *ctx);

S32 vdec_ctx_reset(MppVdecCtx *ctx);

S32 handle_vdec_data(ALBaseContext *base_context, MppData *sink_data);

S32 process_vdec_data(ALBaseContext *base_context, MppData *sink_data,
    MppData *src_data);

S32 get_vdec_result(ALBaseContext *base_context, MppData *src_data);

S32 get_vdec_result_2(ALBaseContext *base_context, MppData **src_data,
    U32 u32TimeoutMs);

S32 return_vdec_result(ALBaseContext *base_context, MppData *src_data);

#ifdef __cplusplus
}
#endif

#endif /* VDEC_H */
