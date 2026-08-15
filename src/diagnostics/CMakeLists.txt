dzc_add_interface_target(dzc_diagnostics)

target_include_directories(dzc_diagnostics INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)
