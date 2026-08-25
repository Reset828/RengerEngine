# Locate the generated GLAD source tree without network access.

if(NOT DZC_ENABLE_OPENGL)
    return()
endif()

if(NOT DEFINED DZC_GLAD_ROOT OR DZC_GLAD_ROOT STREQUAL "")
    set(DZC_GLAD_ROOT "${PROJECT_SOURCE_DIR}/third_party/glad")
endif()
get_filename_component(DZC_GLAD_ROOT "${DZC_GLAD_ROOT}" ABSOLUTE
    BASE_DIR "${PROJECT_SOURCE_DIR}")
set(DZC_GLAD_ROOT "${DZC_GLAD_ROOT}" CACHE PATH
    "Root of generated GLAD sources (include/glad/glad.h and src/glad.c)" FORCE)

set(_dzc_glad_required_files
    "include/glad/glad.h"
    "include/KHR/khrplatform.h"
    "src/glad.c"
)
foreach(_dzc_glad_relative_path IN LISTS _dzc_glad_required_files)
    if(NOT EXISTS "${DZC_GLAD_ROOT}/${_dzc_glad_relative_path}")
        message(FATAL_ERROR
            "DZC_GLAD_ROOT does not contain required GLAD file: "
            "${DZC_GLAD_ROOT}/${_dzc_glad_relative_path}")
    endif()
endforeach()

set(DZC_GLAD_INCLUDE_DIR "${DZC_GLAD_ROOT}/include")
set(DZC_GLAD_SOURCE "${DZC_GLAD_ROOT}/src/glad.c")
