if(NOT DEFINED DZC_TARGET_MANIFEST)
    message(FATAL_ERROR "DZC_TARGET_MANIFEST must point to the generated target manifest")
endif()

include("${DZC_TARGET_MANIFEST}")

set(_expected_targets
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

foreach(targetName IN LISTS _expected_targets)
    list(FIND DZC_CONFIGURED_TARGETS "${targetName}" targetIndex)
    if(targetIndex EQUAL -1)
        message(FATAL_ERROR "Missing PF-003 target: ${targetName}")
    endif()
endforeach()

foreach(targetName IN LISTS DZC_CONFIGURED_TARGETS)
    if(targetName MATCHES "^(dzc_data_pcl|dzc_app)$")
        continue()
    endif()
    if(DEFINED DZC_TARGET_LINKS_${targetName})
        if(DZC_TARGET_LINKS_${targetName} MATCHES "PCL|Qt5::Widgets|Qt6::Widgets")
            message(FATAL_ERROR "Forbidden external UI/data dependency on ${targetName}: ${DZC_TARGET_LINKS_${targetName}}")
        endif()
    endif()
endforeach()

set(_required_dependencies_dzc_engine_core "dzc_engine_api;dzc_data_core;dzc_render_api;dzc_compute_api;dzc_tasks;dzc_diagnostics")
set(_required_dependencies_dzc_data_core "dzc_engine_api;dzc_tasks;dzc_diagnostics")
set(_required_dependencies_dzc_data_pcl "dzc_data_core")
set(_required_dependencies_dzc_render_api "dzc_engine_api;dzc_data_core")
set(_required_dependencies_dzc_render_opengl "dzc_render_api")
set(_required_dependencies_dzc_render_vulkan "dzc_render_api;dzc_platform")
set(_required_dependencies_dzc_compute_api "dzc_engine_api")
set(_required_dependencies_dzc_compute_cuda "dzc_compute_api")
set(_required_dependencies_dzc_interop_gl "dzc_render_opengl;dzc_compute_cuda")
set(_required_dependencies_dzc_interop_vk "dzc_render_vulkan;dzc_compute_cuda")
set(_required_dependencies_dzc_app "dzc_engine_core;dzc_platform;dzc_render_api;dzc_compute_api")

foreach(targetName IN ITEMS
        dzc_engine_core dzc_data_core dzc_data_pcl dzc_render_api
        dzc_render_opengl dzc_render_vulkan dzc_compute_api dzc_compute_cuda
        dzc_interop_gl dzc_interop_vk dzc_app)
    set(linkText "${DZC_TARGET_LINKS_${targetName}}")
    foreach(dependencyName IN LISTS _required_dependencies_${targetName})
        if(NOT linkText MATCHES "(^|\\|)${dependencyName}($|\\|)")
            message(FATAL_ERROR "Missing dependency ${targetName} -> ${dependencyName}: ${linkText}")
        endif()
    endforeach()
endforeach()

foreach(targetName IN LISTS DZC_CONFIGURED_TARGETS)
    if(DEFINED DZC_TARGET_LINKS_${targetName})
        if(DZC_TARGET_LINKS_${targetName} MATCHES "dzc_app")
            message(FATAL_ERROR "Backend or library target must not depend on UI target ${targetName}")
        endif()
    endif()
endforeach()
message(STATUS "PF-003 target and dependency boundary check passed")