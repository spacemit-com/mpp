/* =========================================================================
 * Unity - A Test Framework for C
 * ThrowTheSwitch.org
 * Copyright (c) 2007-24 Mike Karlesky, Mark VanderVoord, & Greg Williams
 * SPDX-License-Identifier: MIT
 * ========================================================================= */

#ifndef UNITY_FRAMEWORK_H
#define UNITY_FRAMEWORK_H

#define UNITY_VERSION_MAJOR    2
#define UNITY_VERSION_MINOR    6
#define UNITY_VERSION_BUILD    0
#define UNITY_VERSION          ((UNITY_VERSION_MAJOR << 16) | (UNITY_VERSION_MINOR << 8) | UNITY_VERSION_BUILD)

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* -----------------------------------------------------------------------
 * Configuration (can be overridden by cmake defines)
 * ----------------------------------------------------------------------- */
#ifndef UNITY_INT_WIDTH
#define UNITY_INT_WIDTH 32
#endif

#ifndef UNITY_LONG_WIDTH
#define UNITY_LONG_WIDTH 32
#endif

/* -----------------------------------------------------------------------
 * Basic types
 * ----------------------------------------------------------------------- */
typedef unsigned char  UNITY_UINT8;
typedef unsigned short UNITY_UINT16;
typedef unsigned int   UNITY_UINT32;
typedef signed char    UNITY_INT8;
typedef signed short   UNITY_INT16;
typedef signed int     UNITY_INT32;

#if defined(__LP64__) || defined(_LP64) || (defined(__WORDSIZE) && __WORDSIZE == 64)
typedef unsigned long long UNITY_UINT;
typedef signed long long   UNITY_INT;
#else
typedef unsigned long UNITY_UINT;
typedef signed long   UNITY_INT;
#endif

typedef UNITY_INT    UNITY_INT_WITHIN_UINT;

/* -----------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------------- */
typedef struct UNITY_STORAGE_T {
    const char *TestFile;
    const char *CurrentTestName;
    UNITY_UINT  CurrentTestLineNumber;
    UNITY_UINT  NumberOfTests;
    UNITY_UINT  TestFailures;
    UNITY_UINT  TestIgnores;
    UNITY_UINT  CurrentTestFailed;
    UNITY_UINT  CurrentTestIgnored;
} UNITY_STORAGE_T;

extern UNITY_STORAGE_T Unity;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void UnityBegin(const char *filename);
int  UnityEnd(void);
void UnitySetTestFile(const char *filename);
void UnityConcludeTest(void);

void Unity_PrintNumber(UNITY_INT number);
void Unity_PrintNumberUnsigned(UNITY_UINT number);
void Unity_PrintNumberHex(UNITY_UINT number, char nibbles);

void UnityPrintChar(const char *pch);
void UnityPrint(const char *string);
void UnityPrintLen(const char *string, UNITY_UINT32 length);

/* Failure functions */
void UnityFail(const char *message, UNITY_UINT line);
void UnityIgnore(const char *message, UNITY_UINT line);
void UnityMessage(const char *message, UNITY_UINT line);
void UnityDefaultTestRun(void (*func)(void), const char *funcName, int funcLineNum);

/* -----------------------------------------------------------------------
 * Assertion macros
 * ----------------------------------------------------------------------- */

/* --- Test running --- */
#define RUN_TEST(func)           UnityDefaultTestRun(func, #func, __LINE__)
#define UNITY_BEGIN()            UnityBegin(__FILE__)
#define UNITY_END()              UnityEnd()
#define TEST_PROTECT()           (setjmp(UnityJumpBuffer) == 0)
#define TEST_ABORT()             longjmp(UnityJumpBuffer, 1)

#include <setjmp.h>
extern jmp_buf UnityJumpBuffer;

/* --- Pass / Fail / Ignore --- */
#define TEST_PASS()              do {} while(0)
#define TEST_FAIL()              UnityFail(NULL, (UNITY_UINT)__LINE__)
#define TEST_IGNORE()            UnityIgnore(NULL, (UNITY_UINT)__LINE__)
#define TEST_FAIL_MESSAGE(msg)   UnityFail((msg), (UNITY_UINT)__LINE__)
#define TEST_IGNORE_MESSAGE(msg) UnityIgnore((msg), (UNITY_UINT)__LINE__)
#define TEST_MESSAGE(msg)        UnityMessage((msg), (UNITY_UINT)__LINE__)

/* --- Boolean --- */
#define TEST_ASSERT(cond) \
    do { if (!(cond)) UnityFail("Expected TRUE Was FALSE", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_TRUE(cond)   TEST_ASSERT(cond)
#define TEST_ASSERT_FALSE(cond) \
    do { if (cond)  UnityFail("Expected FALSE Was TRUE",  (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_NULL(ptr) \
    do { if ((ptr) != NULL) UnityFail("Expected NULL",    (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_NOT_NULL(ptr) \
    do { if ((ptr) == NULL) UnityFail("Expected non-NULL",(UNITY_UINT)__LINE__); } while(0)

/* --- Integer equal / not-equal --- */
#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { if ((int)(expected) != (int)(actual)) { \
        Unity.CurrentTestFailed = 1; \
        UnityFail("Expected equal integers", (UNITY_UINT)__LINE__); } } while(0)
#define TEST_ASSERT_NOT_EQUAL(expected, actual) \
    do { if ((expected) == (actual)) \
        UnityFail("Expected different values", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_EQUAL(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)
#define TEST_ASSERT_EQUAL_INT32(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)

/* --- Unsigned --- */
#define TEST_ASSERT_EQUAL_UINT(expected, actual) \
    do { if ((unsigned)(expected) != (unsigned)(actual)) \
        UnityFail("Expected equal unsigned integers", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_EQUAL_UINT32(expected, actual) TEST_ASSERT_EQUAL_UINT(expected, actual)

/* --- Pointer equal --- */
#define TEST_ASSERT_EQUAL_PTR(expected, actual) \
    do { if ((expected) != (actual)) \
        UnityFail("Expected equal pointers", (UNITY_UINT)__LINE__); } while(0)

/* --- Greater / Less than --- */
#define TEST_ASSERT_GREATER_THAN(threshold, actual) \
    do { if (!((actual) > (threshold))) \
        UnityFail("Expected greater than threshold", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_LESS_THAN(threshold, actual) \
    do { if (!((actual) < (threshold))) \
        UnityFail("Expected less than threshold", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual) \
    do { if (!((actual) >= (threshold))) \
        UnityFail("Expected greater or equal to threshold", (UNITY_UINT)__LINE__); } while(0)
#define TEST_ASSERT_LESS_OR_EQUAL(threshold, actual) \
    do { if (!((actual) <= (threshold))) \
        UnityFail("Expected less or equal to threshold", (UNITY_UINT)__LINE__); } while(0)

/* --- String --- */
#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    do { if (__extension__ __builtin_expect( \
        (expected) == NULL || (actual) == NULL || \
        __builtin_strcmp((expected),(actual)) != 0, 0)) \
        UnityFail("Expected equal strings", (UNITY_UINT)__LINE__); } while(0)

#ifdef __cplusplus
}
#endif

#endif /* UNITY_FRAMEWORK_H */
