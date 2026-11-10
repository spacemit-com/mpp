# ============================================================
# al/vcodec  →  V4L2 codec plugins (shared libraries, dlopen'd at runtime)
# ============================================================

# ---- LinlonV5V7 plugin ----
# BEFORE ensures project headers win over stale /usr/include copies.
include_directories(BEFORE
    linlonv5v7/include
    ${PROJECT_SOURCE_DIR}/include/venc
    ${PROJECT_SOURCE_DIR}/include/vdec
)

set(LINLONV5V7_SRCS
    linlonv5v7/linlonv5v7_dec.c
    linlonv5v7/linlonv5v7_mjpeg.c
    linlonv5v7/linlonv5v7_enc.c
    linlonv5v7/linlonv5v7_codec.c
    linlonv5v7/linlonv5v7_buffer.c
    linlonv5v7/linlonv5v7_port.c
)

add_library(v4l2_linlonv5v7_codec SHARED ${LINLONV5V7_SRCS})

# Link mpp shared library if available, otherwise just rely on include paths
# Keep libspacemit_mpp in DT_NEEDED when the codec plugin is loaded with RTLD_LOCAL.
target_link_libraries(v4l2_linlonv5v7_codec PRIVATE
    -Wl,--no-as-needed mpp -Wl,--as-needed)

# Build-tree plugins sit next to libspacemit_mpp.so so that MPP_PLUGIN_DIR
# can point at a single directory (see al/module.c).
set_target_properties(v4l2_linlonv5v7_codec PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS v4l2_linlonv5v7_codec
    LIBRARY DESTINATION lib
)
