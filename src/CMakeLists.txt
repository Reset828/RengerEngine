# PF-003 creates the complete module target graph without implementation sources.

function(dzc_add_interface_target targetName)
    add_library(${targetName} INTERFACE)
    target_compile_features(${targetName} INTERFACE cxx_std_17)
endfunction()

add_subdirectory(engine)
add_subdirectory(tasks)
add_subdirectory(diagnostics)
add_subdirectory(platform)
add_subdirectory(data)
add_subdirectory(render)
add_subdirectory(compute)
add_subdirectory(app)

set(DZC_FOUNDATION_TARGETS
    dzc_engine_api
    dzc_engine_core
    dzc_data_core
    dzc_data_pcl
    dzc_render_api
    dzc_render_opengl
    dzc_render_vulkan
    dzc_compute_api
    dzc_compute_cuda
    dzc_interop_gl
    dzc_interop_vk
    dzc_tasks
    dzc_diagnostics
    dzc_platform
    dzc_app
)

foreach(targetName IN LISTS DZC_FOUNDATION_TARGETS)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "PF-003 target was not created: ${targetName}")
    endif()
endforeach()

# Public API exposes only the project public include tree. No backend SDK
# headers or implementation handles are exported here.
target_include_directories(dzc_engine_api INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Core and abstraction dependency direction.
target_link_libraries(dzc_data_core PUBLIC
    dzc_engine_api
    dzc_tasks
    dzc_diagnostics
)
target_link_libraries(dzc_data_pcl INTERFACE dzc_data_core)

target_link_libraries(dzc_render_api INTERFACE
    dzc_engine_api
    dzc_data_core
)
target_link_libraries(dzc_render_opengl INTERFACE dzc_render_api)
target_link_libraries(dzc_render_vulkan INTERFACE
    dzc_render_api
    dzc_platform
)

target_link_libraries(dzc_compute_api INTERFACE dzc_engine_api)
target_link_libraries(dzc_compute_cuda INTERFACE dzc_compute_api)
target_link_libraries(dzc_interop_gl INTERFACE
    dzc_render_opengl
    dzc_compute_cuda
)
target_link_libraries(dzc_interop_vk INTERFACE
    dzc_render_vulkan
    dzc_compute_cuda
)

target_link_libraries(dzc_engine_core PUBLIC
    dzc_engine_api
    dzc_data_core
    dzc_render_api
    dzc_compute_api
    dzc_tasks
    dzc_diagnostics
)

# Composition Root links only the capabilities selected by the build options.
target_link_libraries(dzc_app INTERFACE
    dzc_engine_core
    dzc_platform
    dzc_render_api
    dzc_compute_api
)
if(DZC_ENABLE_OPENGL)
    target_link_libraries(dzc_app INTERFACE dzc_render_opengl)
    if(DZC_ENABLE_CUDA)
        target_link_libraries(dzc_app INTERFACE dzc_interop_gl)
    endif()
endif()
if(DZC_ENABLE_VULKAN)
    target_link_libraries(dzc_app INTERFACE dzc_render_vulkan)
    if(DZC_ENABLE_CUDA)
        target_link_libraries(dzc_app INTERFACE dzc_interop_vk)
    endif()
endif()

# PF-003 intentionally defers Qt/PCL discovery. When concrete application or
# point-cloud I/O sources are implemented, only dzc_app may link Qt Widgets and
# only dzc_data_pcl may link PCL.
if(DZC_ENABLE_OPENGL)
    target_link_libraries(dzc_render_opengl INTERFACE OpenGL::GL)
endif()
if(DZC_ENABLE_VULKAN)
    target_link_libraries(dzc_render_vulkan INTERFACE Vulkan::Vulkan)
endif()
if(DZC_ENABLE_CUDA)
    target_link_libraries(dzc_compute_cuda INTERFACE CUDA::cudart)
endif()

# Generate a small configure-time manifest consumed by the boundary check.
set(_dzc_manifest "set(DZC_CONFIGURED_TARGETS \"${DZC_FOUNDATION_TARGETS}\")\n")
foreach(targetName IN LISTS DZC_FOUNDATION_TARGETS)
    get_target_property(targetLinks ${targetName} INTERFACE_LINK_LIBRARIES)
    if(targetLinks STREQUAL "targetLinks-NOTFOUND")
        set(targetLinks "")
    endif()
    string(REPLACE ";" "|" targetLinksText "${targetLinks}")
    string(APPEND _dzc_manifest
        "set(DZC_TARGET_LINKS_${targetName} \"${targetLinksText}\")\n"
    )
endforeach()
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/DzcTargetManifest.cmake"
    CONTENT "${_dzc_manifest}"
)