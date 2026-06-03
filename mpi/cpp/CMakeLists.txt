# ============================================================
# mpi/cpp  →  mpp_cpp (OBJECT, aggregated into libmpp.so)
# CPP wrapper backed by real k1x-cam cpp implementation
# ============================================================

# Platform detection: check if K1 cpp library is available
find_library(CPP_LIB cpp)

if(CPP_LIB)
    message(STATUS "K1 cpp library found: ${CPP_LIB}, building mpp_cpp")

    set(K1X_CAM_ROOT "${CMAKE_SOURCE_DIR}/../k1x-cam")
    set(K1X_CAM_INCLUDE_DIR "${K1X_CAM_ROOT}/libs/include")
    set(K1X_CAM_LIB_DIR "${K1X_CAM_ROOT}/libs/lib64")

    set(CPP_SRCS
        cpp.c
    )

    add_library(mpp_cpp OBJECT ${CPP_SRCS})

    target_include_directories(mpp_cpp
        PUBLIC
            ${MPP_INCLUDE_DIRS}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${K1X_CAM_INCLUDE_DIR}"
    )

    # Export cpp link info to parent scope for libmpp.so aggregation
    set(MPP_CPP_AVAILABLE ON PARENT_SCOPE)
    set(MPP_CPP_LIB_DIR "${K1X_CAM_LIB_DIR}" PARENT_SCOPE)
    set(MPP_CPP_LINK_LIBS cpp PARENT_SCOPE)

    install(FILES
        "${CMAKE_SOURCE_DIR}/include/cpp/cpp_api.h"
        "${CMAKE_SOURCE_DIR}/include/cpp/cpp_type.h"
        DESTINATION include/cpp
    )
else()
    message(STATUS "K1 cpp library NOT found, skipping mpp_cpp")
    set(MPP_CPP_AVAILABLE OFF PARENT_SCOPE)
endif()
