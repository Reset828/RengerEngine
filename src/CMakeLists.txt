# Public CameraTypes and data types use GLM in their exposed interfaces.
find_package(glm CONFIG REQUIRED)

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

target_link_libraries(dzc_engine_api INTERFACE glm::glm)

# Core and abstraction dependency direction.
target_link_libraries(dzc_data_core PUBLIC
    dzc_engine_api
    dzc_tasks
    dzc_diagnostics
)
target_link_libraries(dzc_data_pcl PUBLIC dzc_data_core)

target_link_libraries(dzc_render_api INTERFACE
    dzc_engine_api
    dzc_data_core
)
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
target_link_libraries(dzc_app PRIVATE
    dzc_engine_core
    dzc_platform
    dzc_render_api
    dzc_compute_api
)
if(DZC_ENABLE_OPENGL)
    target_link_libraries(dzc_app PRIVATE dzc_render_opengl)
    if(DZC_ENABLE_CUDA)
        target_link_libraries(dzc_app PRIVATE dzc_interop_gl)
    endif()
endif()
if(DZC_ENABLE_VULKAN)
    target_link_libraries(dzc_app PRIVATE dzc_render_vulkan)
    if(DZC_ENABLE_CUDA)
        target_link_libraries(dzc_app PRIVATE dzc_interop_vk)
    endif()
endif()

# dzc_app is the only Target allowed to link Qt Widgets; dzc_data_pcl is the
# only Target allowed to link PCL. The latter discovers PCL in src/data/io/pcl.
if(DZC_ENABLE_VULKAN)
    target_link_libraries(dzc_render_vulkan INTERFACE Vulkan::Vulkan)
endif()
if(DZC_ENABLE_CUDA)
    target_link_libraries(dzc_compute_cuda INTERFACE CUDA::cudart)
endif()

# Generate a small configure-time manifest consumed by the boundary check.
set(_dzc_manifest
    "set(DZC_CONFIGURED_TARGETS \"${DZC_FOUNDATION_TARGETS}\")\nset(DZC_MANIFEST_ENABLE_OPENGL \"${DZC_ENABLE_OPENGL}\")\n")
foreach(targetName IN LISTS DZC_FOUNDATION_TARGETS)
    get_target_property(targetType ${targetName} TYPE)
    get_target_property(targetLinks ${targetName} INTERFACE_LINK_LIBRARIES)
    get_target_property(targetDirectLinks ${targetName} LINK_LIBRARIES)
    get_target_property(targetSources ${targetName} SOURCES)
    get_target_property(targetIncludeDirectories ${targetName} INCLUDE_DIRECTORIES)
    get_target_property(targetInterfaceIncludeDirectories ${targetName} INTERFACE_INCLUDE_DIRECTORIES)
    foreach(propertyName IN ITEMS
            targetLinks targetDirectLinks targetSources targetIncludeDirectories
            targetInterfaceIncludeDirectories)
        if(${propertyName} STREQUAL "${propertyName}-NOTFOUND")
            set(${propertyName} "")
        endif()
        string(REPLACE ";" "|" ${propertyName}Text "${${propertyName}}")
    endforeach()
    string(APPEND _dzc_manifest
        "set(DZC_TARGET_TYPE_${targetName} \"${targetType}\")\n"
        "set(DZC_TARGET_LINKS_${targetName} \"${targetLinksText}\")\n"
        "set(DZC_TARGET_DIRECT_LINKS_${targetName} \"${targetDirectLinksText}\")\n"
        "set(DZC_TARGET_SOURCES_${targetName} \"${targetSourcesText}\")\n"
        "set(DZC_TARGET_INCLUDE_DIRECTORIES_${targetName} \"${targetIncludeDirectoriesText}\")\n"
        "set(DZC_TARGET_INTERFACE_INCLUDE_DIRECTORIES_${targetName} \"${targetInterfaceIncludeDirectoriesText}\")\n"
    )
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/DzcTargetManifest.cmake" "${_dzc_manifest}")
