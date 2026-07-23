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
    linlonv5v7/linlonv5v7_enc.c
    linlonv5v7/linlonv5v7_codec.c
    linlonv5v7/linlonv5v7_buffer.c
    linlonv5v7/linlonv5v7_port.c
)

add_library(v4l2_linlonv5v7_codec2 SHARED ${LINLONV5V7_SRCS})

# Link mpp shared library if available, otherwise just rely on include paths
# Keep libmpp in DT_NEEDED when codec2 is loaded with RTLD_LOCAL.
target_link_libraries(v4l2_linlonv5v7_codec2 PRIVATE
    -Wl,--no-as-needed mpp -Wl,--as-needed)

# Post-build: try copying to /usr/lib first; if no permission, fall back to $HOME/.mpp/plugins
add_custom_command(TARGET v4l2_linlonv5v7_codec2 POST_BUILD
    COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/install_plugin.sh $<TARGET_FILE:v4l2_linlonv5v7_codec2>
    COMMENT "Installing libv4l2_linlonv5v7_codec2.so to system or user plugin directory"
)

install(TARGETS v4l2_linlonv5v7_codec2
    LIBRARY DESTINATION lib
)
