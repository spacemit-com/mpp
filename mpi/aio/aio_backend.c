/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_backend.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    Loads the pcm backend plugin (libaio_alsa_plugin.so).
 *                 Kept apart from the rest of mpi/aio so that tests can link
 *                 their own aio_backend_load() instead.
 *------------------------------------------------------------------------------
 */

#include <dlfcn.h>

#include "aio_internal.h"
#include "module.h"

#define AIO_LOG_ERR(fmt, ...) MPP_LOG(MPP_LOG_ERROR, "aio", fmt, ##__VA_ARGS__)

static MppModule *g_pstAioModule;

S32 aio_backend_load(const AioAlOps **ppstOps) {
    PFN_al_aio_get_ops pfnGetOps;
    const AioAlOps *pstOps;
    void *pHandle;

    g_pstAioModule = module_init(AIO_ALSA);
    if (g_pstAioModule == NULL) {
        AIO_LOG_ERR("pcm backend plugin libaio_alsa_plugin.so not found");
        return SYS_ERR_NOT_FOUND;
    }

    pHandle = module_get_so_path(g_pstAioModule);
    if (pHandle == NULL) {
        const char *pszErr = dlerror();

        AIO_LOG_ERR("pcm backend plugin failed to load: %s", pszErr ? pszErr : "unknown");
        aio_backend_unload();
        return SYS_ERR_NOT_FOUND;
    }

    dlerror();
    pfnGetOps = (PFN_al_aio_get_ops)dlsym(pHandle, "al_aio_get_ops");
    pstOps = pfnGetOps ? pfnGetOps() : NULL;
    if (pstOps == NULL) {
        AIO_LOG_ERR("al_aio_get_ops missing from pcm backend plugin");
        aio_backend_unload();
        return SYS_ERR_NOT_FOUND;
    }

    if (pstOps->init == NULL || pstOps->deinit == NULL || pstOps->pcm_open == NULL || pstOps->pcm_close == NULL ||
        pstOps->pcm_start == NULL || pstOps->pcm_stop == NULL || pstOps->pcm_wait == NULL ||
        pstOps->pcm_read == NULL || pstOps->pcm_write == NULL || pstOps->pcm_recover == NULL ||
        pstOps->pcm_get_status == NULL) {
        AIO_LOG_ERR("required ops missing from pcm backend plugin");
        aio_backend_unload();
        return SYS_ERR_NOT_FOUND;
    }
    *ppstOps = pstOps;

    return SYS_ERR_OK;
}

VOID aio_backend_unload(VOID) {
    if (g_pstAioModule != NULL) {
        module_destory(g_pstAioModule);
        g_pstAioModule = NULL;
    }
}
