/*** 
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-11-02 18:22:19
 * @LastEditTime: 2023-11-02 18:24:20
 * @Description: 
 */
/*** 
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-11-02 18:14:04
 * @LastEditTime: 2023-11-02 18:14:06
 * @Description: 
 */

#define FIND_DEC_LIBRARY(TYPE, type, name)                                       \
  void find_dec_##type(U8 *path) {                                             \
    if (0 == access("/usr/lib/lib"#name".so", F_OK)) {                     \
      U8 *tmp = "/usr/lib/lib"#name".so";                                \
      S32 len = strlen(tmp);                                                 \
      memcpy(path, tmp, len);                                                \
      path[len] = '\0';                                                      \
    } else if (0 == access("/usr/local/lib/lib"#name".so", F_OK)) {        \
      U8 *tmp = "/usr/local/lib/lib"#name".so";                          \
      S32 len = strlen(tmp);                                                 \
      memcpy(path, tmp, len);                                                \
      path[len] = '\0';                                                      \
    } else if (0 ==                                                          \
               access("/usr/lib/riscv64-linux-gnu/lib"#name".so", F_OK)) { \
      U8 *tmp = "/usr/lib/riscv64-linux-gnu/lib"#name".so";              \
      S32 len = strlen(tmp);                                                 \
      memcpy(path, tmp, len);                                                \
      path[len] = '\0';                                                      \
    } else {                                                                 \
      path[0] = '\0';                                                        \
      error("can not find omx il so");                                       \
    }                                                                        \
  }

FIND_DEC_LIBRARY(SFOMX, sfomx, sf-omx-il)
