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

#ifndef __TYPE_H__
#define __TYPE_H__

#include <inttypes.h>
#include <stddef.h>

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long ULONG;
typedef unsigned long long int U64;

typedef signed char S8;
typedef signed short S16;
typedef signed int S32;
typedef signed long LONG;
typedef int64_t S64;

typedef signed int RETURN;

typedef signed int BOOL;
#define MPP_TRUE 1
#define MPP_FALSE 0

// compute the size of two-dimensional array
#define NUM_OF(arr) (sizeof(arr) / sizeof(*arr))
#define PRINT_DIGIT_ARR(arr)                                     \
  do {                                                           \
    printf("%s: ", #arr);                                        \
    for (int i = 0; i < NUM_OF(arr); i++) printf("%d ", arr[i]); \
    printf("\n");                                                \
  } while (0)

// check the system is big-end or little-end
static union {
  char c[4];
  unsigned long l;
} endian_test = {{'l', '?', '?', 'b'}};
#define ENDIANNESS ((char)endian_test.l)

// check whether n is power of 2
#define is_power_of_2(n) ((n) != 0 && ((n) & ((n)-1)) == 0)

#define MIN(A, B)            \
  ({                         \
    __typeof__(A) __a = (A); \
    __typeof__(B) __b = (B); \
    __a < __b ? __a : __b;   \
  })

/*
 *typedef struct
 *{
 *    char a;
 *    int b;
 * }S;
 * offset_of(S, b);
 */
#define offset_of(type, member) ((size_t) & ((type *)0->menber))
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member)                \
  ({                                                   \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

#define ROUNDUP(a, size) (((a) & ((size)-1)) ? (1 + ((a) | ((size)-1))) : (a))

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif /*__TYPE_H__*/
