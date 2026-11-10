/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 10:27:53
 * @LastEditTime: 2024-04-28 17:17:12
 * @Description: dlopen the video codec library dynamicly
 */


#include "module.h"

#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "v4l2_utils.h"

struct _MppModule {
    U8 so_path[MAX_PATH_LENGTH];
    void *load_so;
};

/*
 * Locate the plugin library "lib<name>.so" for a module.
 *
 * Search order (first match wins):
 *   1. $MPP_PLUGIN_DIR: colon-separated directories. Lets a build tree or a
 *      test plugin take precedence without installing into system dirs.
 *   2. /usr/lib, /usr/local/lib
 *   3. /system/lib64, /vendor/lib64 as "lib<name>.z.so" (OpenHarmony)
 */
static S32 plugin_exists(const char *candidate, U8 *path) {
    S32 len = strlen(candidate);

    if (0 != access(candidate, F_OK)) {
        return 0;
    }

    memcpy(path, candidate, len);
    path[len] = '\0';
    return 1;
}

static S32 find_plugin_so(const char *name, U8 *path) {
    static const char *const system_dirs[] = {"/usr/lib", "/usr/local/lib"};
    static const char *const ohos_dirs[] = {"/system/lib64", "/vendor/lib64"};
    char candidate[MAX_PATH_LENGTH];
    const char *dir = getenv("MPP_PLUGIN_DIR");
    S32 ret;
    U32 i;

    while (dir && *dir) {
        const char *sep = strchr(dir, ':');
        S32 dir_len = sep ? (S32)(sep - dir) : (S32)strlen(dir);

        if (dir_len > 0) {
            ret = snprintf(candidate, sizeof(candidate), "%.*s/lib%s.so", dir_len, dir, name);
            if (ret > 0 && ret < (S32)sizeof(candidate) && plugin_exists(candidate, path)) {
                debug("found %s plugin via MPP_PLUGIN_DIR: %s", name, candidate);
                return 1;
            }
        }

        if (!sep) {
            break;
        }

        dir = sep + 1;
    }

    for (i = 0; i < sizeof(system_dirs) / sizeof(system_dirs[0]); i++) {
        snprintf(candidate, sizeof(candidate), "%s/lib%s.so", system_dirs[i], name);
        if (plugin_exists(candidate, path)) {
            debug("found %s plugin: %s", name, candidate);
            return 1;
        }
    }

    for (i = 0; i < sizeof(ohos_dirs) / sizeof(ohos_dirs[0]); i++) {
        snprintf(candidate, sizeof(candidate), "%s/lib%s.z.so", ohos_dirs[i], name);
        if (plugin_exists(candidate, path)) {
            debug("found %s plugin: %s", name, candidate);
            return 1;
        }
    }

    return 0;
}

#define FIND_PLUGIN(TYPE, type, name)       \
    S32 find_##type##_plugin(U8 *path) {    \
        return find_plugin_so(#name, path); \
    }

FIND_PLUGIN(OPENH264, openh264, soft_openh264)
FIND_PLUGIN(SFOMX, sfomx, sfomxil_plugin)
FIND_PLUGIN(SFENC, sfenc, sfenc_plugin)
FIND_PLUGIN(SFDEC, sfdec, sfdec_plugin)
FIND_PLUGIN(FFMPEG, ffmpeg, ffmpegcodec)
FIND_PLUGIN(V4L2, v4l2, v4l2_standard_codec)
FIND_PLUGIN(FAKEDEC, fakedec, fake_dec_plugin)
FIND_PLUGIN(V4L2_LINLONV5V7, v4l2_linlonv5v7, v4l2_linlonv5v7_codec)
FIND_PLUGIN(K1_V2D, k1_v2d, v2d_plugin)
FIND_PLUGIN(K1_JPU, k1_jpu, jpu_plugin)
FIND_PLUGIN(VO_SDL2, vo_sdl2, vo_sdl2_plugin)
FIND_PLUGIN(VO_FILE, vo_file, vo_file_plugin)
FIND_PLUGIN(VI_V4L2, vi_v4l2, vi_v4l2_plugin)
FIND_PLUGIN(VI_K1_CAM, vi_k1_cam, vi_k1_cam_plugin)
FIND_PLUGIN(VI_K3_CAM, vi_k3_cam, vi_k3_cam_plugin)
FIND_PLUGIN(VI_FILE, vi_file, vi_file_plugin)

#define CHECK_LIBRARY(TYPE, type, name, path1, path2)                                                               \
    S32 check_##type() {                                                                                            \
        if ((0 == access("/usr/lib/lib" #name ".so", F_OK)) || (0 == access("/usr/lib/lib" #name ".so.0", F_OK)) || \
            (0 == access("/usr/local/lib/lib" #name ".so", F_OK)) ||                                                \
            (0 == access("/usr/lib/riscv64-linux-gnu/lib" #name ".so", F_OK)) ||                                    \
            (0 == access("/usr/lib/riscv64-linux-gnu/lib" #name ".so.7", F_OK)) ||                                  \
            (0 == access("/usr/lib/riscv64-linux-gnu/lib" #name ".so.0", F_OK)) ||                                  \
            (0 == access(#path1 "/lib" #name ".so", F_OK)) || (0 == access(#path2 "/lib" #name ".so", F_OK)) ||     \
            (0 == access("/system/lib64/lib" #name ".so", F_OK)) ||                                                 \
            (0 == access("/vendor/lib64/lib" #name ".so", F_OK))) {                                                 \
            debug("yeah! have " #type "---------------");                                                           \
            return 1;                                                                                               \
        }                                                                                                           \
                                                                                                                    \
        return 0;                                                                                                   \
    }

CHECK_LIBRARY(SFOMX, sfomx, sf-omx-il, /, /)
CHECK_LIBRARY(SFENC, sfenc, sfenc, /, /)
CHECK_LIBRARY(SFDEC, sfdec, sfdec, /, /)
CHECK_LIBRARY(FFMPEG, ffmpeg, avcodec, /usr/lib/ffmpeg, /usr/local/lib/ffmpeg)
CHECK_LIBRARY(
    OPENH264, openh264, openh264, /usr/lib/x86_64-linux-gnu, /usr/local/lib/x86_64-linux-gnu)
CHECK_LIBRARY(FAKEDEC, fakedec, c, /, /)
CHECK_LIBRARY(K1_V2D, k1_v2d, v2d, /, /)
CHECK_LIBRARY(K1_JPU, k1_jpu, jpu, /, /)
CHECK_LIBRARY(VO_SDL2, vo_sdl2, SDL2-2.0, /, /)
CHECK_LIBRARY(VO_FILE, vo_file, c, /, /)
CHECK_LIBRARY(VI_V4L2, vi_v4l2, c, /, /)
CHECK_LIBRARY(VI_K1_CAM, vi_k1_cam, c, /, /)
CHECK_LIBRARY(VI_K3_CAM, vi_k3_cam, c, /, /)
CHECK_LIBRARY(VI_FILE, vi_file, c, /, /)

#define CHECKMODULE_BY_TYPE(TYPE, type)                                                          \
    {                                                                                            \
        if (check_##type()) {                                                                    \
            if (find_##type##_plugin(module->so_path)) {                                         \
                debug("++++++++++ " #TYPE " (%s)", module->so_path);                             \
                module->load_so = dlopen((const char *)module->so_path, RTLD_LAZY | RTLD_LOCAL); \
                debug("++++++++++ open (%s) success !", module->so_path);                        \
            } else {                                                                             \
                error("can not find " #type " plugin, please check!");                           \
                free(module);                                                                    \
                return NULL;                                                                     \
            }                                                                                    \
        } else {                                                                                 \
            error("can not find " #type ", please check!");                                      \
            free(module);                                                                        \
            return NULL;                                                                         \
        }                                                                                        \
    }

/**
 * @description: dlopen the video codec library by module_type
 * @param {MppModuleType} module_type : input, the codec need to be opened
 * @return {MppModule*} : the module context
 */
MppModule *module_init(MppModuleType module_type) {
    debug("+++++++++++++++ module init, module type = %d", module_type);
    MppModule *module = (MppModule *)malloc(sizeof(MppModule));

#if 0
    // for test
    U8 buffer[2000];
    getcwd(buffer, 2000);
    debug("the buffer is (%s)", buffer);
#endif

    if (CODEC_OPENH264 == module_type) {
        CHECKMODULE_BY_TYPE(OPENH264, openh264);
    } else if (CODEC_FFMPEG == module_type) {
        CHECKMODULE_BY_TYPE(FFMPEG, ffmpeg);
    } else if (CODEC_SFDEC == module_type) {
        CHECKMODULE_BY_TYPE(SFDEC, sfdec);
    } else if (CODEC_SFENC == module_type) {
        CHECKMODULE_BY_TYPE(SFENC, sfenc);
    } else if (CODEC_SFOMX == module_type) {
        CHECKMODULE_BY_TYPE(SFOMX, sfomx);
    } else if (CODEC_V4L2 == module_type) {
        CHECKMODULE_BY_TYPE(V4L2, v4l2);
    } else if (CODEC_FAKEDEC == module_type) {
        CHECKMODULE_BY_TYPE(FAKEDEC, fakedec);
    } else if (CODEC_V4L2_LINLONV5V7 == module_type) {
        CHECKMODULE_BY_TYPE(V4L2_LINLONV5V7, v4l2_linlonv5v7);
    } else if (CODEC_K1_JPU == module_type) {
        CHECKMODULE_BY_TYPE(K1_JPU, k1_v2d);
    } else if (VO_SDL2 == module_type) {
        CHECKMODULE_BY_TYPE(VO_SDL2, vo_sdl2);
    } else if (VO_FILE == module_type) {
        CHECKMODULE_BY_TYPE(VO_FILE, vo_file);
    } else if (VI_V4L2 == module_type) {
        CHECKMODULE_BY_TYPE(VI_V4L2, vi_v4l2);
    } else if (VI_K1_CAM == module_type) {
        CHECKMODULE_BY_TYPE(VI_K1_CAM, vi_k1_cam);
    } else if (VI_K3_CAM == module_type) {
        CHECKMODULE_BY_TYPE(VI_K3_CAM, vi_k3_cam);
    } else if (VI_FILE == module_type) {
        CHECKMODULE_BY_TYPE(VI_FILE, vi_file);
    } else if (VPS_K1_V2D == module_type) {
        CHECKMODULE_BY_TYPE(K1_V2D, k1_v2d);
    } else {
        error("need auto detect load_so");
    }

    return module;
}

MppModule *module_auto_init() {
    MppModule *module_tmp = NULL;
    module_tmp = module_init(CODEC_V4L2);
    if (module_tmp) {
        debug("auto select V4L2 codec");
        return module_tmp;
    }

    module_tmp = module_init(CODEC_SFOMX);
    if (module_tmp) {
        debug("auto select starfice openmax codec");
        return module_tmp;
    }

    error("can not find suitable codec, please check!");
    return NULL;
}

/**
 * @description: close the module
 * @param {MppModule*} module : input, the module need to be closed
 * @return {*}
 */
void module_destory(MppModule *module) {
    debug("+++++++++++++++ module destory");
    if (module->load_so) {
        dlclose(module->load_so);
        module->load_so = NULL;
    }

    free(module);
}

/**
 * @description: get the so path from the module
 * @param {MppModule*} module : the module opened
 * @return {void*} : the path string pointer
 */
void *module_get_so_path(MppModule *module) {
    if (!module) {
        error("module is NULL, please check!");
        return NULL;
    }

    return module->load_so;
}
