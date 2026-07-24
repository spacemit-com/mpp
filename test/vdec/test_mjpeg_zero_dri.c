/*
 * Copyright 2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>

#include "linlonv5v7_mjpeg.h"

#define TEST_REQUIRE(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition);                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static int check_copy(const uint8_t *input, size_t input_size, const uint8_t *expected,
    size_t expected_size, int in_place) {
    uint8_t output[128];
    size_t output_size = 0;

    TEST_REQUIRE(input_size <= sizeof(output));
    if (in_place) {
        memcpy(output, input, input_size);
        TEST_REQUIRE(linlon_mjpeg_remove_zero_dri(output, input_size, output, sizeof(output), &output_size) == 0);
    } else {
        TEST_REQUIRE(linlon_mjpeg_remove_zero_dri(input, input_size, output, sizeof(output), &output_size) == 0);
    }
    TEST_REQUIRE(output_size == expected_size);
    TEST_REQUIRE(memcmp(output, expected, expected_size) == 0);
    return 0;
}

int main(void) {
    static const uint8_t zero_dri[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x04, 0x11, 0x22,
        0xff, 0xdd, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xda, 0x00, 0x02, 0x12, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x00, 0xff, 0xd9,
    };
    static const uint8_t zero_dri_expected[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x04, 0x11, 0x22,
        0xff, 0xda, 0x00, 0x02, 0x12, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x00, 0xff, 0xd9,
    };
    static const uint8_t nonzero_dri[] = {
        0xff, 0xd8, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x10, 0xff, 0xda, 0x00, 0x02, 0xff, 0xd9,
    };
    static const uint8_t app_contains_pattern[] = {
        0xff, 0xd8, 0xff, 0xe1, 0x00, 0x08, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xda, 0x00, 0x02, 0xff, 0xd9,
    };
    static const uint8_t multiple_zero_dri[] = {
        0xff, 0xd8,
        0xff, 0xff, 0xdd, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xe0, 0x00, 0x02,
        0xff, 0xdd, 0x00, 0x04, 0x00, 0x00,
        0xff, 0xda, 0x00, 0x02, 0xff, 0xd9,
    };
    static const uint8_t multiple_expected[] = {
        0xff, 0xd8, 0xff, 0xe0, 0x00, 0x02, 0xff, 0xda, 0x00, 0x02, 0xff, 0xd9,
    };
    static const uint8_t truncated[] = {0xff, 0xd8, 0xff, 0xdd, 0x00, 0x04, 0x00};
    static const uint8_t nonstandard_dri_length[] = {
        0xff, 0xd8, 0xff, 0xdd, 0x00, 0x05, 0x00, 0x00, 0x7f, 0xff, 0xda, 0x00, 0x02,
    };
    static const uint8_t no_soi[] = {0xff, 0xdd, 0x00, 0x04, 0x00, 0x00};
    uint8_t too_small[sizeof(zero_dri) - 1];
    size_t output_size = 0;

    if (check_copy(zero_dri, sizeof(zero_dri), zero_dri_expected, sizeof(zero_dri_expected), 0) != 0)
        return 1;
    if (check_copy(zero_dri, sizeof(zero_dri), zero_dri_expected, sizeof(zero_dri_expected), 1) != 0)
        return 1;
    if (check_copy(nonzero_dri, sizeof(nonzero_dri), nonzero_dri, sizeof(nonzero_dri), 0) != 0)
        return 1;
    if (check_copy(app_contains_pattern, sizeof(app_contains_pattern), app_contains_pattern,
            sizeof(app_contains_pattern), 0) != 0)
        return 1;
    if (check_copy(multiple_zero_dri, sizeof(multiple_zero_dri), multiple_expected,
            sizeof(multiple_expected), 0) != 0)
        return 1;
    if (check_copy(truncated, sizeof(truncated), truncated, sizeof(truncated), 0) != 0)
        return 1;
    if (check_copy(nonstandard_dri_length, sizeof(nonstandard_dri_length), nonstandard_dri_length,
            sizeof(nonstandard_dri_length), 0) != 0)
        return 1;
    if (check_copy(no_soi, sizeof(no_soi), no_soi, sizeof(no_soi), 0) != 0)
        return 1;
    if (linlon_mjpeg_remove_zero_dri(zero_dri, sizeof(zero_dri), too_small,
            sizeof(too_small), &output_size) == 0)
        return 1;

    printf("[PASS] MJPEG zero DRI normalization\n");
    return 0;
}
