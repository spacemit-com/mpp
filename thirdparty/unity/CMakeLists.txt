# ============================================================
# thirdparty/unity  →  libunity (static)
# Lightweight Unity test framework for embedded C
# ============================================================

add_library(unity STATIC unity.c)

target_include_directories(unity
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(unity PROPERTIES
    OUTPUT_NAME "unity"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
