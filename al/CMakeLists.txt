# ============================================================
# al  →  Abstract Layer (codec plugins, loaded via dlopen)
# ============================================================

# Use BEFORE so project headers take precedence over any stale
# MPP headers that may be installed in /usr/include.
include_directories(BEFORE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/sys
    ${PROJECT_SOURCE_DIR}/utils/include
    ${PROJECT_SOURCE_DIR}/utils/os/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vi/include
)

add_subdirectory(vi/k1)
add_subdirectory(vi/k3)
add_subdirectory(vcodec)
