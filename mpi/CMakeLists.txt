# ============================================================
# mpi - All MPI sub-module libraries
# ============================================================

add_subdirectory(sys)
add_subdirectory(al_module)
add_subdirectory(vi)
add_subdirectory(vo)
add_subdirectory(venc)
add_subdirectory(vdec)
add_subdirectory(demux)
add_subdirectory(mux)
add_subdirectory(v2d)
add_subdirectory(cpp)
add_subdirectory(uvc)

# ============================================================
# libmpp.so  –  unified shared library aggregating all OBJECT modules
# ============================================================

add_library(mpp SHARED)

# Aggregate all OBJECT libraries into a single libmpp.so
target_sources(mpp PRIVATE
    $<TARGET_OBJECTS:mpp_sys>
    $<TARGET_OBJECTS:mpp_al_module>
    $<TARGET_OBJECTS:mpp_vi>
    $<TARGET_OBJECTS:mpp_vo>
    $<TARGET_OBJECTS:mpp_venc>
    $<TARGET_OBJECTS:mpp_vdec>
    $<TARGET_OBJECTS:mpp_demux>
    $<TARGET_OBJECTS:mpp_mux>
    $<TARGET_OBJECTS:mpp_v2d>
    $<TARGET_OBJECTS:mpp_uvc>
)

# Conditionally add utils (Linux-only) and mpp_cpp (platform-dependent)
if(TARGET utils)
    target_sources(mpp PRIVATE $<TARGET_OBJECTS:utils>)
endif()

if(MPP_CPP_AVAILABLE AND TARGET mpp_cpp)
    target_sources(mpp PRIVATE $<TARGET_OBJECTS:mpp_cpp>)
    # Also expose for test/CMakeLists.txt
    set(MPP_HAS_CPP ON PARENT_SCOPE)
endif()

# ---- Public include directories (propagated to consumers) ----
target_include_directories(mpp PUBLIC
    ${MPP_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/utils/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/utils/os/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/al/include>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/al/vi/include>
)

# ---- External dependencies (unified on the final mpp target) ----
find_package(Threads REQUIRED)
target_link_libraries(mpp PUBLIC Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mpp PUBLIC ${CMAKE_DL_LIBS} rt)
endif()

# K1 cpp library (from mpi/cpp)
if(MPP_CPP_AVAILABLE AND MPP_CPP_LINK_LIBS)
    target_link_directories(mpp PRIVATE ${MPP_CPP_LIB_DIR})
    target_link_libraries(mpp PRIVATE ${MPP_CPP_LINK_LIBS})
endif()

# ---- Output & install ----
set_target_properties(mpp PROPERTIES
    OUTPUT_NAME "mpp"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

install(TARGETS mpp LIBRARY DESTINATION lib)

