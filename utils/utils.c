/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-12 10:08:48
 * @LastEditTime: 2023-02-01 19:56:33
 * @Description:
 */

#include "type.h"

void hex2str(const U8 *hex, S32 size, U8 *str) {
  U8 char_arr[17] = "0123456789ABCDEF";
  for (S32 i = 0; i < size; i++) {
    str[3 * i] = char_arr[hex[i] >> 4];
    str[3 * i + 1] = char_arr[hex[i] & 0x0F];
    str[3 * i + 2] = ' ';
  }
}