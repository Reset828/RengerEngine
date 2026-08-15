if(NOT DEFINED DZC_SMOKE_NAME OR DZC_SMOKE_NAME STREQUAL "")
    message(FATAL_ERROR "PF-008 requires DZC_SMOKE_NAME")
endif()
if(NOT DEFINED DZC_SMOKE_SOURCE_DIR OR DZC_SMOKE_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "PF-008 requires DZC_SMOKE_SOURCE_DIR")
endif()
if(NOT DEFINED DZC_SMOKE_BINARY_ROOT OR DZC_SMOKE_BINARY_ROOT STREQUAL "")
    message(FATAL_ERROR "PF-008 requires DZC_SMOKE_BINARY_ROOT")
endif()
if(NOT DEFINED DZC_EXPECT_OPENGL OR NOT DEFINED DZC_EXPECT_VULKAN OR NOT DEFINED DZC_EXPECT_CUDA)
    message(FATAL_ERROR "PF-008 requires expected backend values")
endif()

set(_smoke_binary_dir "${DZC_SMOKE_BINARY_ROOT}/${DZC_SMOKE_NAME}")
file(REMOVE_RECURSE "${_smoke_binary_dir}")
file(MAKE_DIRECTORY "${DZC_SMOKE_BINARY_ROOT}")

set(_configure_args
    -S "${DZC_SMOKE_SOURCE_DIR}"
    -B "${_smoke_binary_dir}"
    -DDZC_BUILD_TESTS=OFF
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
)
if(DEFINED DZC_SMOKE_GENERATOR AND NOT DZC_SMOKE_GENERATOR STREQUAL "")
    list(APPEND _configure_args -G "${DZC_SMOKE_GENERATOR}")
endif()
if(DEFINED DZC_SMOKE_MAKE_PROGRAM AND NOT DZC_SMOKE_MAKE_PROGRAM STREQUAL "")
    list(APPEND _configure_args -DCMAKE_MAKE_PROGRAM=${DZC_SMOKE_MAKE_PROGRAM})
endif()
if(DEFINED DZC_SMOKE_CXX_COMPILER AND NOT DZC_SMOKE_CXX_COMPILER STREQUAL "")
    list(APPEND _configure_args -DCMAKE_CXX_COMPILER=${DZC_SMOKE_CXX_COMPILER})
endif()

if(DEFINED DZC_ENABLE_OPENGL)
    list(APPEND _configure_args -DDZC_ENABLE_OPENGL=${DZC_ENABLE_OPENGL})
endif()
if(DEFINED DZC_ENABLE_VULKAN)
    list(APPEND _configure_args -DDZC_ENABLE_VULKAN=${DZC_ENABLE_VULKAN})
endif()
if(DEFINED DZC_ENABLE_CUDA)
    list(APPEND _configure_args -DDZC_ENABLE_CUDA=${DZC_ENABLE_CUDA})
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_configure_args}
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_stdout
    ERROR_VARIABLE _configure_stderr
)

if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR
        "PF-008 configure smoke '${DZC_SMOKE_NAME}' failed with exit code "
        "${_configure_result}.\n"
        "--- stdout ---\n${_configure_stdout}\n"
        "--- stderr ---\n${_configure_stderr}")
endif()

set(_cache_file "${_smoke_binary_dir}/CMakeCache.txt")
if(NOT EXISTS "${_cache_file}")
    message(FATAL_ERROR
        "PF-008 configure smoke '${DZC_SMOKE_NAME}' did not produce CMakeCache.txt.\n"
        "--- stdout ---\n${_configure_stdout}\n"
        "--- stderr ---\n${_configure_stderr}")
endif()

file(READ "${_cache_file}" _cache_contents)
foreach(_backend IN ITEMS OPENGL VULKAN CUDA)
    if(DZC_EXPECT_${_backend})
        set(_expected_value ON)
    else()
        set(_expected_value OFF)
    endif()
    string(REGEX MATCH "(^|\n)DZC_ENABLE_${_backend}:BOOL=${_expected_value}($|\n)"
        _cache_match "${_cache_contents}")
    if(NOT _cache_match)
        message(FATAL_ERROR
            "PF-008 configure smoke '${DZC_SMOKE_NAME}' selected an unexpected "
            "${_backend} value; expected ${_expected_value}.\n"
            "--- stdout ---\n${_configure_stdout}\n"
            "--- stderr ---\n${_configure_stderr}")
    endif()
endforeach()

if(NOT EXISTS "${_smoke_binary_dir}/DzcTargetManifest.cmake")
    message(FATAL_ERROR
        "PF-008 configure smoke '${DZC_SMOKE_NAME}' did not generate the target manifest.\n"
        "--- stdout ---\n${_configure_stdout}\n"
        "--- stderr ---\n${_configure_stderr}")
endif()

message(STATUS
    "PF-008 configure smoke '${DZC_SMOKE_NAME}' passed: "
    "OpenGL=${DZC_EXPECT_OPENGL}, "
    "Vulkan=${DZC_EXPECT_VULKAN}, "
    "CUDA=${DZC_EXPECT_CUDA}")
file(REMOVE_RECURSE "${_smoke_binary_dir}")