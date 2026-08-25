if(NOT DEFINED DZC_GL001_SOURCE_DIR OR DZC_GL001_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "GL-001 configuration test requires DZC_GL001_SOURCE_DIR")
endif()
if(NOT DEFINED DZC_GL001_BINARY_ROOT OR DZC_GL001_BINARY_ROOT STREQUAL "")
    message(FATAL_ERROR "GL-001 configuration test requires DZC_GL001_BINARY_ROOT")
endif()

set(_gl001_common_args
    -DDZC_BUILD_TESTS=OFF
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
)
if(DEFINED DZC_GL001_GENERATOR AND NOT DZC_GL001_GENERATOR STREQUAL "")
    list(APPEND _gl001_common_args -G "${DZC_GL001_GENERATOR}")
endif()
if(DEFINED DZC_GL001_MAKE_PROGRAM AND NOT DZC_GL001_MAKE_PROGRAM STREQUAL "")
    list(APPEND _gl001_common_args -DCMAKE_MAKE_PROGRAM=${DZC_GL001_MAKE_PROGRAM})
endif()
if(DEFINED DZC_GL001_CXX_COMPILER AND NOT DZC_GL001_CXX_COMPILER STREQUAL "")
    list(APPEND _gl001_common_args -DCMAKE_CXX_COMPILER=${DZC_GL001_CXX_COMPILER})
endif()
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "")
    list(APPEND _gl001_common_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()
if(DEFINED PCL_DIR AND NOT PCL_DIR STREQUAL "")
    list(APPEND _gl001_common_args -DPCL_DIR=${PCL_DIR})
endif()
if(DEFINED CMAKE_PREFIX_PATH AND NOT CMAKE_PREFIX_PATH STREQUAL "")
    list(APPEND _gl001_common_args -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH})
endif()

set(_success_binary_dir "${DZC_GL001_BINARY_ROOT}/gl001-success")
file(REMOVE_RECURSE "${_success_binary_dir}")
set(_success_args
    -S "${DZC_GL001_SOURCE_DIR}"
    -B "${_success_binary_dir}"
    ${_gl001_common_args}
    -DDZC_ENABLE_OPENGL=ON
    -DDZC_ENABLE_VULKAN=OFF
    -DDZC_ENABLE_CUDA=OFF
    -DDZC_GLAD_ROOT=${DZC_GL001_SOURCE_DIR}/third_party/glad
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_success_args}
    RESULT_VARIABLE _success_result
    OUTPUT_VARIABLE _success_stdout
    ERROR_VARIABLE _success_stderr
)
if(NOT _success_result EQUAL 0)
    message(FATAL_ERROR
        "GL-001 OpenGL-only configuration failed with exit code ${_success_result}.\n"
        "--- stdout ---\n${_success_stdout}\n--- stderr ---\n${_success_stderr}")
endif()

set(_success_manifest "${_success_binary_dir}/DzcTargetManifest.cmake")
if(NOT EXISTS "${_success_manifest}")
    message(FATAL_ERROR "GL-001 OpenGL-only configuration did not generate the target manifest")
endif()
include("${_success_manifest}")
list(FIND DZC_CONFIGURED_TARGETS dzc_render_opengl _opengl_target_index)
if(_opengl_target_index EQUAL -1)
    message(FATAL_ERROR "GL-001 OpenGL-only configuration did not create dzc_render_opengl")
endif()
if(NOT DZC_TARGET_TYPE_dzc_render_opengl STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR
        "GL-001 expected a STATIC_LIBRARY dzc_render_opengl target, got "
        "${DZC_TARGET_TYPE_dzc_render_opengl}")
endif()
if(NOT DZC_TARGET_SOURCES_dzc_render_opengl MATCHES
        "third_party/glad[/\\]src[/\\]glad\\.c")
    message(FATAL_ERROR
        "GL-001 dzc_render_opengl does not compile the repository GLAD source: "
        "${DZC_TARGET_SOURCES_dzc_render_opengl}")
endif()
if(NOT DZC_TARGET_INCLUDE_DIRECTORIES_dzc_render_opengl MATCHES
        "third_party/glad[/\\]include")
    message(FATAL_ERROR
        "GL-001 GLAD include directory is not private to dzc_render_opengl: "
        "${DZC_TARGET_INCLUDE_DIRECTORIES_dzc_render_opengl}")
endif()
if(DZC_TARGET_INTERFACE_INCLUDE_DIRECTORIES_dzc_render_opengl MATCHES "glad")
    message(FATAL_ERROR
        "GL-001 GLAD include directory leaked through the public interface: "
        "${DZC_TARGET_INTERFACE_INCLUDE_DIRECTORIES_dzc_render_opengl}")
endif()
if(NOT DZC_TARGET_DIRECT_LINKS_dzc_render_opengl MATCHES "OpenGL::GL")
    message(FATAL_ERROR
        "GL-001 dzc_render_opengl does not link OpenGL::GL privately: "
        "${DZC_TARGET_DIRECT_LINKS_dzc_render_opengl}")
endif()

set(_missing_binary_dir "${DZC_GL001_BINARY_ROOT}/gl001-missing")
file(REMOVE_RECURSE "${_missing_binary_dir}")
set(_missing_args
    -S "${DZC_GL001_SOURCE_DIR}"
    -B "${_missing_binary_dir}"
    ${_gl001_common_args}
    -DDZC_ENABLE_OPENGL=ON
    -DDZC_ENABLE_VULKAN=OFF
    -DDZC_ENABLE_CUDA=OFF
    -DDZC_GLAD_ROOT=${_missing_binary_dir}/does-not-exist
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_missing_args}
    RESULT_VARIABLE _missing_result
    OUTPUT_VARIABLE _missing_stdout
    ERROR_VARIABLE _missing_stderr
)
set(_missing_output "${_missing_stdout}\n${_missing_stderr}")
if(_missing_result EQUAL 0)
    message(FATAL_ERROR
        "GL-001 expected configuration with a missing DZC_GLAD_ROOT to fail.\n"
        "${_missing_output}")
endif()
if(NOT _missing_output MATCHES
        "DZC_GLAD_ROOT does not contain required GLAD file:.*include/glad/glad.h")
    message(FATAL_ERROR
        "GL-001 missing GLAD failure did not contain the required diagnostic.\n"
        "${_missing_output}")
endif()

set(_disabled_binary_dir "${DZC_GL001_BINARY_ROOT}/gl001-disabled")
file(REMOVE_RECURSE "${_disabled_binary_dir}")
set(_disabled_args
    -S "${DZC_GL001_SOURCE_DIR}"
    -B "${_disabled_binary_dir}"
    ${_gl001_common_args}
    -DDZC_ENABLE_OPENGL=OFF
    -DDZC_ENABLE_VULKAN=ON
    -DDZC_ENABLE_CUDA=OFF
    -DDZC_GLAD_ROOT=${_disabled_binary_dir}/does-not-exist
)
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_disabled_args}
    RESULT_VARIABLE _disabled_result
    OUTPUT_VARIABLE _disabled_stdout
    ERROR_VARIABLE _disabled_stderr
)
if(NOT _disabled_result EQUAL 0)
    message(FATAL_ERROR
        "GL-001 OpenGL-disabled configuration failed with exit code ${_disabled_result}.\n"
        "--- stdout ---\n${_disabled_stdout}\n--- stderr ---\n${_disabled_stderr}")
endif()
if(NOT EXISTS "${_disabled_binary_dir}/CMakeCache.txt")
    message(FATAL_ERROR "GL-001 OpenGL-disabled configuration did not produce CMakeCache.txt")
endif()
file(READ "${_disabled_binary_dir}/CMakeCache.txt" _disabled_cache)
if(NOT _disabled_cache MATCHES "(^|\n)DZC_ENABLE_OPENGL:BOOL=OFF($|\n)")
    message(FATAL_ERROR "GL-001 OpenGL-disabled configuration selected an unexpected OpenGL value")
endif()

message(STATUS "GL-001 GLAD configuration checks passed")
file(REMOVE_RECURSE "${_success_binary_dir}")
file(REMOVE_RECURSE "${_missing_binary_dir}")
file(REMOVE_RECURSE "${_disabled_binary_dir}")
