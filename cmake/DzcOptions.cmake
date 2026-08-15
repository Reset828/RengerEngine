# Project-wide build options and dependency checks.

option(DZC_ENABLE_OPENGL "Build OpenGL backend" ON)
option(DZC_ENABLE_VULKAN "Build Vulkan backend" OFF)
option(DZC_ENABLE_CUDA "Build CUDA compute and interop" OFF)
option(DZC_BUILD_TESTS "Build tests" ON)

if(NOT DZC_ENABLE_OPENGL AND NOT DZC_ENABLE_VULKAN)
    message(FATAL_ERROR
        "At least one graphics backend must be enabled. "
        "Set DZC_ENABLE_OPENGL=ON or DZC_ENABLE_VULKAN=ON."
    )
endif()

# Only probe dependencies for explicitly enabled capabilities. This keeps an
# OpenGL-only build independent from Vulkan and CUDA SDK installations.
if(DZC_ENABLE_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

if(DZC_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

if(DZC_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()

set(DZC_SELECTED_GRAPHICS_BACKENDS)
if(DZC_ENABLE_OPENGL)
    list(APPEND DZC_SELECTED_GRAPHICS_BACKENDS "OpenGL")
endif()
if(DZC_ENABLE_VULKAN)
    list(APPEND DZC_SELECTED_GRAPHICS_BACKENDS "Vulkan")
endif()
string(JOIN ", " DZC_SELECTED_GRAPHICS_BACKENDS_TEXT ${DZC_SELECTED_GRAPHICS_BACKENDS})

message(STATUS "Dzc-RenderEngine build options:")
message(STATUS "  OpenGL: ${DZC_ENABLE_OPENGL}")
message(STATUS "  Vulkan: ${DZC_ENABLE_VULKAN}")
message(STATUS "  CUDA: ${DZC_ENABLE_CUDA}")
message(STATUS "  Tests: ${DZC_BUILD_TESTS}")
message(STATUS "  Graphics backends: ${DZC_SELECTED_GRAPHICS_BACKENDS_TEXT}")