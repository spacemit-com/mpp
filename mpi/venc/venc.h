/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: Legacy VENC context API (venc_ctx_*). Internal header used by
 *               venc.c and venc_api.c. External users should use venc_api.h.
 */

#ifndef _MPP_VENC_H_
#define _MPP_VENC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "data.h"
#include "frame.h"
#include "module.h"
#include "packet.h"
#include "processflow.h"
#include "type.h"
#include "venc_type.h"

typedef struct _MppVencCtx {
  MppProcessNode pNode;
  MppModuleType eCodecType;
  MppVencPara stVencPara;
  MppModule *pModule;
} MppVencCtx;

MppVencCtx *venc_ctx_create(void);

S32 venc_ctx_init(MppVencCtx *ctx);

S32 venc_ctx_set_param(MppVencCtx *ctx, MppVencCmd cmd, void *para);

S32 venc_ctx_get_param(MppVencCtx *ctx, MppVencPara *para);

S32 venc_ctx_send_input_frame(MppVencCtx *ctx, MppData *sink_data);

S32 venc_ctx_return_input_frame(MppVencCtx *ctx, MppData *sink_data);

S32 venc_ctx_encode(MppVencCtx *ctx, MppData *sink_data);

S32 venc_ctx_process(MppVencCtx *ctx, MppData *sink_data, MppData *src_data);

S32 venc_ctx_get_output_stream(MppVencCtx *ctx, MppData *src_data);

S32 venc_ctx_request_output_stream(MppVencCtx *ctx, MppData *src_data);

S32 venc_ctx_return_output_stream(MppVencCtx *ctx, MppData *src_data);

S32 venc_ctx_flush(MppVencCtx *ctx);

S32 venc_ctx_destroy(MppVencCtx *ctx);

S32 venc_ctx_reset(MppVencCtx *ctx);

/* For bind system */
S32 handle_venc_data(ALBaseContext *base_context, MppData *sink_data);

S32 process_venc_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data);

S32 get_venc_result_sync(ALBaseContext *base_context, MppData *src_data);

S32 get_venc_result(ALBaseContext *base_context, MppData *src_data);

S32 return_venc_result(ALBaseContext *base_context, MppData *src_data);

#ifdef __cplusplus
}
#endif

#endif /* _MPP_VENC_H_ */
