add_executable(dzc_opengl_capabilities_tests
    OpenGLCapabilitiesTests.cpp
)

target_compile_features(dzc_opengl_capabilities_tests PRIVATE cxx_std_17)
target_include_directories(dzc_opengl_capabilities_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_opengl_capabilities_tests PRIVATE
    dzc_render_opengl
    OpenGL::GL
)

add_test(NAME dzc_opengl_capabilities COMMAND dzc_opengl_capabilities_tests)
set_tests_properties(dzc_opengl_capabilities PROPERTIES
    LABELS "graphics;opengl;gl002")

add_test(NAME dzc_opengl_capabilities_real_context
    COMMAND dzc_opengl_capabilities_tests --real-context)
set_tests_properties(dzc_opengl_capabilities_real_context PROPERTIES
    LABELS "graphics;opengl;gl002;skipped")
set_tests_properties(dzc_opengl_capabilities_real_context PROPERTIES SKIP_RETURN_CODE 77)
