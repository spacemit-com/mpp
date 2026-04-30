# ============================================================
# al  →  Abstract Layer (codec plugins, loaded via dlopen)
# ============================================================

# AL-layer headers (al_interface_dec.h, al_interface_enc.h, module.h, etc.)
include_directories(include)

# Project public headers (type.h, sys_type.h, vb_type.h, etc.)
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/include/sys)

add_subdirectory(vi/k1)
add_subdirectory(vcodec)
