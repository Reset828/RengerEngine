dzc_add_interface_target(dzc_render_api)

if(DZC_ENABLE_OPENGL)
    add_subdirectory(opengl)
else()
    dzc_add_interface_target(dzc_render_opengl)
    target_link_libraries(dzc_render_opengl INTERFACE dzc_render_api)
endif()

dzc_add_interface_target(dzc_render_vulkan)
