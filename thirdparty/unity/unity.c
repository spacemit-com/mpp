/* =========================================================================
 * Unity - A Test Framework for C
 * ThrowTheSwitch.org
 * Copyright (c) 2007-24 Mike Karlesky, Mark VanderVoord, & Greg Williams
 * SPDX-License-Identifier: MIT
 * ========================================================================= */

#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

/* Global state */
UNITY_STORAGE_T Unity;
jmp_buf         UnityJumpBuffer;

/* -----------------------------------------------------------------------
 * Internal print helpers
 * ----------------------------------------------------------------------- */
static void UnityPrintNewline(void)   { putchar('\n'); }
static void UnityPrintColon(void)     { putchar(':'); }
static void UnityPrintSpace(void)     { putchar(' '); }

void UnityPrintChar(const char *pch)
{
    if (*pch && *pch >= ' ')
        putchar(*pch);
    else
        putchar('?');
}

void UnityPrint(const char *string)
{
    if (string) fputs(string, stdout);
}

void UnityPrintLen(const char *string, UNITY_UINT32 length)
{
    if (string) {
        while (*string && length--) putchar(*string++);
    }
}

void Unity_PrintNumber(UNITY_INT number)
{
    printf("%ld", (long)number);
}

void Unity_PrintNumberUnsigned(UNITY_UINT number)
{
    printf("%lu", (unsigned long)number);
}

void Unity_PrintNumberHex(UNITY_UINT number, char nibbles)
{
    printf("%0*lX", (int)nibbles, (unsigned long)number);
}

/* -----------------------------------------------------------------------
 * Core framework
 * ----------------------------------------------------------------------- */
void UnityBegin(const char *filename)
{
    Unity.TestFile            = filename;
    Unity.CurrentTestName     = NULL;
    Unity.CurrentTestLineNumber = 0;
    Unity.NumberOfTests       = 0;
    Unity.TestFailures        = 0;
    Unity.TestIgnores         = 0;
    Unity.CurrentTestFailed   = 0;
    Unity.CurrentTestIgnored  = 0;
    printf("--- Test file: %s ---\n", filename ? filename : "unknown");
}

int UnityEnd(void)
{
    printf("\n-----------------------\n");
    Unity_PrintNumberUnsigned(Unity.NumberOfTests);
    UnityPrint(" Tests ");
    Unity_PrintNumberUnsigned(Unity.TestFailures);
    UnityPrint(" Failures ");
    Unity_PrintNumberUnsigned(Unity.TestIgnores);
    UnityPrint(" Ignored\n");
    if (Unity.TestFailures == 0U) {
        UnityPrint("OK\n");
        return 0;
    }
    UnityPrint("FAIL\n");
    return (int)(Unity.TestFailures);
}

void UnitySetTestFile(const char *filename)
{
    Unity.TestFile = filename;
}

void UnityConcludeTest(void)
{
    if (Unity.CurrentTestIgnored) {
        Unity.TestIgnores++;
    } else if (Unity.CurrentTestFailed == 0) {
        UnityPrint("PASS\n");
    }
    Unity.CurrentTestFailed  = 0;
    Unity.CurrentTestIgnored = 0;
}

/* -----------------------------------------------------------------------
 * Failure / ignore / message
 * ----------------------------------------------------------------------- */
void UnityFail(const char *message, UNITY_UINT line)
{
    printf("%s:", Unity.CurrentTestName ? Unity.CurrentTestName : "?");
    Unity_PrintNumberUnsigned(line);
    UnityPrint(":FAIL");
    if (message) {
        UnityPrintColon();
        UnityPrintSpace();
        UnityPrint(message);
    }
    UnityPrintNewline();
    Unity.CurrentTestFailed = 1;
    Unity.TestFailures++;
    longjmp(UnityJumpBuffer, 1);
}

void UnityIgnore(const char *message, UNITY_UINT line)
{
    (void)line;
    UnityPrint(":IGNORE");
    if (message) {
        UnityPrintColon();
        UnityPrintSpace();
        UnityPrint(message);
    }
    UnityPrintNewline();
    Unity.CurrentTestIgnored = 1;
    longjmp(UnityJumpBuffer, 1);
}

void UnityMessage(const char *message, UNITY_UINT line)
{
    (void)line;
    if (message) UnityPrint(message);
    UnityPrintNewline();
}

/* -----------------------------------------------------------------------
 * Test runner
 * ----------------------------------------------------------------------- */
void UnityDefaultTestRun(void (*func)(void), const char *funcName, int funcLineNum)
{
    (void)funcLineNum;
    Unity.CurrentTestName = funcName;
    Unity.NumberOfTests++;
    Unity.CurrentTestFailed  = 0;
    Unity.CurrentTestIgnored = 0;

    printf("  %-50s ... ", funcName);
    fflush(stdout);

    if (TEST_PROTECT()) {
        func();
    }
    UnityConcludeTest();
}
