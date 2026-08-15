# Project-local CTest registration for repeatable configure smoke checks.
# The smoke checks configure isolated sub-builds and do not link unfinished
# implementation targets.

set(_dzc_configure_smoke_script
    "${CMAKE_SOURCE_DIR}/tests/cmake/ConfigureSmoke.cmake")
set(_dzc_configure_smoke_root
    "${CMAKE_BINARY_DIR}/pf008-configure-smoke")

# CTest may run tests without the developer-command-prompt PATH. Pass the
# configure-time tool paths explicitly so nested configure cases are repeatable.
if(IS_ABSOLUTE "${CMAKE_MAKE_PROGRAM}")
    set(_dzc_smoke_make_program "${CMAKE_MAKE_PROGRAM}")
else()
    unset(_dzc_smoke_make_program CACHE)
    find_program(_dzc_smoke_make_program NAMES "${CMAKE_MAKE_PROGRAM}")
endif()
if(NOT _dzc_smoke_make_program)
    message(FATAL_ERROR
        "Cannot locate the outer generator's make program: ${CMAKE_MAKE_PROGRAM}")
endif()

add_test(
    NAME dzc_configure_smoke_default
    COMMAND ${CMAKE_COMMAND}
        -DDZC_SMOKE_NAME=default
        -DDZC_SMOKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DDZC_SMOKE_BINARY_ROOT=${_dzc_configure_smoke_root}
        -DDZC_SMOKE_GENERATOR=${CMAKE_GENERATOR}
        -DDZC_SMOKE_MAKE_PROGRAM=${_dzc_smoke_make_program}
        -DDZC_SMOKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DDZC_EXPECT_OPENGL=ON
        -DDZC_EXPECT_VULKAN=OFF
        -DDZC_EXPECT_CUDA=OFF
        -P ${_dzc_configure_smoke_script}
)
set_tests_properties(dzc_configure_smoke_default PROPERTIES
    LABELS "smoke;configure")

add_test(
    NAME dzc_configure_smoke_opengl_only
    COMMAND ${CMAKE_COMMAND}
        -DDZC_SMOKE_NAME=opengl-only
        -DDZC_SMOKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DDZC_SMOKE_BINARY_ROOT=${_dzc_configure_smoke_root}
        -DDZC_SMOKE_GENERATOR=${CMAKE_GENERATOR}
        -DDZC_SMOKE_MAKE_PROGRAM=${_dzc_smoke_make_program}
        -DDZC_SMOKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DDZC_ENABLE_OPENGL=ON
        -DDZC_ENABLE_VULKAN=OFF
        -DDZC_ENABLE_CUDA=OFF
        -DDZC_EXPECT_OPENGL=ON
        -DDZC_EXPECT_VULKAN=OFF
        -DDZC_EXPECT_CUDA=OFF
        -P ${_dzc_configure_smoke_script}
)
set_tests_properties(dzc_configure_smoke_opengl_only PROPERTIES
    LABELS "smoke;configure")