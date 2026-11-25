# ============================================================
# al/aio/alsa  →  ALSA pcm backend of AI/AO (shared library, dlopen'd at runtime)
#
# Built only where the ALSA development files are present, so that
# libspacemit_mpp.so itself never depends on libasound.
# ============================================================

find_package(ALSA QUIET)

if(ALSA_FOUND)
    message(STATUS "ALSA ${ALSA_VERSION_STRING} found, building aio_alsa_plugin")

    add_library(aio_alsa_plugin SHARED aio_alsa.c)

    target_include_directories(aio_alsa_plugin BEFORE
        PUBLIC
            ${MPP_INCLUDE_DIRS}
        PRIVATE
            "${PROJECT_SOURCE_DIR}/al/aio/include"
            ${ALSA_INCLUDE_DIRS}
    )

    target_link_libraries(aio_alsa_plugin PRIVATE mpp ${ALSA_LIBRARIES})

    set_target_properties(aio_alsa_plugin PROPERTIES
        OUTPUT_NAME "aio_alsa_plugin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    install(TARGETS aio_alsa_plugin
        LIBRARY DESTINATION lib
    )
else()
    message(STATUS "ALSA development files NOT found, skipping aio_alsa_plugin")
endif()
