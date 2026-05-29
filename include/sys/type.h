/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-01 15:02:50
 * @Description:
 */

#ifndef TYPE_H
#define TYPE_H

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

typedef unsigned char U8;
typedef uint16_t U16;
typedef unsigned int U32;
typedef uint64_t UL;
typedef uint64_t ULONG;
typedef uint64_t U64;

typedef signed char S8;
typedef int16_t S16;
typedef signed int S32;
typedef int64_t SL;
typedef int64_t LONG;
typedef int64_t S64;
typedef char CHAR;

typedef signed int RETURN;

typedef signed int BOOL;
#define MPP_TRUE 1
#define MPP_FALSE 0

#define VOID void

// compute the size of two-dimensional array
#define NUM_OF(arr) (sizeof(arr) / sizeof(*arr))
#define PRINT_DIGIT_ARR(arr)                                     \
do {                                                           \
    printf("%s: ", #arr);                                        \
    for (int i = 0; i < NUM_OF(arr); i++) printf("%d ", arr[i]); \
    printf("\n");                                                \
} while (0)

// check whether n is power of 2
#define is_power_of_2(n) ((n) != 0 && ((n) & ((n)-1)) == 0)

#define ALIGNUP(x, a) ((((x) + ((a) - 1)) / a) * a)

#define MIN(A, B)            \
({                         \
    __typeof__(A) __a = (A); \
    __typeof__(B) __b = (B); \
    __a < __b ? __a : __b;   \
})

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif  // TYPE_H
