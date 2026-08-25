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

set(_dzc_smoke_toolchain_args)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _dzc_smoke_toolchain_args
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

set(_dzc_smoke_pcl_args)
if(DEFINED PCL_DIR AND NOT PCL_DIR STREQUAL "")
    list(APPEND _dzc_smoke_pcl_args -DPCL_DIR=${PCL_DIR})
endif()

set(_dzc_smoke_prefix_args)
if(DEFINED CMAKE_PREFIX_PATH AND NOT CMAKE_PREFIX_PATH STREQUAL "")
    list(APPEND _dzc_smoke_prefix_args -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH})
endif()

set(_dzc_gl001_script
    "${CMAKE_SOURCE_DIR}/tests/cmake/GladDependencyFailure.cmake")
set(_dzc_gl001_root
    "${CMAKE_BINARY_DIR}/gl001-configure-tests")
add_test(
    NAME dzc_gl001_configure
    COMMAND ${CMAKE_COMMAND}
        -DDZC_GL001_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DDZC_GL001_BINARY_ROOT=${_dzc_gl001_root}
        -DDZC_GL001_GENERATOR=${CMAKE_GENERATOR}
        -DDZC_GL001_MAKE_PROGRAM=${_dzc_smoke_make_program}
        -DDZC_GL001_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        ${_dzc_smoke_toolchain_args}
        ${_dzc_smoke_pcl_args}
        ${_dzc_smoke_prefix_args}
        -P ${_dzc_gl001_script}
)
set_tests_properties(dzc_gl001_configure PROPERTIES
    LABELS "configure;opengl;gl001")

add_test(
    NAME dzc_configure_smoke_default
    COMMAND ${CMAKE_COMMAND}
        -DDZC_SMOKE_NAME=default
        -DDZC_SMOKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DDZC_SMOKE_BINARY_ROOT=${_dzc_configure_smoke_root}
        -DDZC_SMOKE_GENERATOR=${CMAKE_GENERATOR}
        -DDZC_SMOKE_MAKE_PROGRAM=${_dzc_smoke_make_program}
        -DDZC_SMOKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        ${_dzc_smoke_toolchain_args}
        ${_dzc_smoke_pcl_args}
        ${_dzc_smoke_prefix_args}
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
        ${_dzc_smoke_toolchain_args}
        ${_dzc_smoke_pcl_args}
        ${_dzc_smoke_prefix_args}
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
