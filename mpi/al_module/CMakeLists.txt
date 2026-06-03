# ============================================================
# Shared AL plugin loader (single copy for vdec + venc + others)
# ============================================================

add_library(mpp_al_module OBJECT "${CMAKE_SOURCE_DIR}/al/module.c")

target_include_directories(mpp_al_module
    PUBLIC
        ${MPP_INCLUDE_DIRS}
)
