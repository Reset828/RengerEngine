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
add_executable(dzc_gl_resource_tests
    GlResourceTests.cpp
)

target_compile_features(dzc_gl_resource_tests PRIVATE cxx_std_17)
target_include_directories(dzc_gl_resource_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_gl_resource_tests PRIVATE dzc_render_opengl)

add_test(NAME dzc_gl_resource COMMAND dzc_gl_resource_tests)
set_tests_properties(dzc_gl_resource PROPERTIES
    LABELS "graphics;opengl;gl003")
add_test(NAME dzc_gl_resource_real_context
    COMMAND dzc_gl_resource_tests --real-context)
set_tests_properties(dzc_gl_resource_real_context PROPERTIES
    LABELS "graphics;opengl;gl003;skipped"
    SKIP_RETURN_CODE 77)

add_executable(dzc_gl_shader_tests
    GlShaderTests.cpp
)

target_compile_features(dzc_gl_shader_tests PRIVATE cxx_std_17)
target_compile_definitions(dzc_gl_shader_tests PRIVATE
    DZC_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_include_directories(dzc_gl_shader_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_gl_shader_tests PRIVATE dzc_render_opengl)

add_test(NAME dzc_gl_shader COMMAND dzc_gl_shader_tests)
set_tests_properties(dzc_gl_shader PROPERTIES
    LABELS "graphics;opengl;gl004")

add_test(NAME dzc_gl_shader_real_context
    COMMAND dzc_gl_shader_tests --real-context)
set_tests_properties(dzc_gl_shader_real_context PROPERTIES
    LABELS "graphics;opengl;gl004;skipped"
    SKIP_RETURN_CODE 77)

add_executable(dzc_gl_chunk_upload_tests
    GlChunkUploadTests.cpp
)

target_compile_features(dzc_gl_chunk_upload_tests PRIVATE cxx_std_17)
target_include_directories(dzc_gl_chunk_upload_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_gl_chunk_upload_tests PRIVATE dzc_render_opengl)

add_test(NAME dzc_gl_chunk_upload COMMAND dzc_gl_chunk_upload_tests)
set_tests_properties(dzc_gl_chunk_upload PROPERTIES
    LABELS "graphics;opengl;gl006")
add_executable(dzc_opengl_backend_lifecycle_tests
    OpenGLBackendLifecycleTests.cpp
)

target_compile_features(dzc_opengl_backend_lifecycle_tests PRIVATE cxx_std_17)
target_include_directories(dzc_opengl_backend_lifecycle_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_opengl_backend_lifecycle_tests PRIVATE dzc_render_opengl)

add_test(NAME dzc_opengl_backend_lifecycle
    COMMAND dzc_opengl_backend_lifecycle_tests)
set_tests_properties(dzc_opengl_backend_lifecycle PROPERTIES
    LABELS "graphics;opengl;gl007")

add_test(NAME dzc_opengl_backend_real_context
    COMMAND dzc_opengl_backend_lifecycle_tests --real-context)
set_tests_properties(dzc_opengl_backend_real_context PROPERTIES
    LABELS "graphics;opengl;gl007;skipped"
    SKIP_RETURN_CODE 77)
add_executable(dzc_opengl_draw_tests
    OpenGLDrawTests.cpp
)
target_compile_features(dzc_opengl_draw_tests PRIVATE cxx_std_17)
target_include_directories(dzc_opengl_draw_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/render/opengl
)
target_link_libraries(dzc_opengl_draw_tests PRIVATE dzc_render_opengl)
add_test(NAME dzc_opengl_draw COMMAND dzc_opengl_draw_tests)
set_tests_properties(dzc_opengl_draw PROPERTIES LABELS "graphics;opengl;gl008")
add_test(NAME dzc_opengl_draw_real_context COMMAND dzc_opengl_draw_tests --real-context)
set_tests_properties(dzc_opengl_draw_real_context PROPERTIES
    LABELS "graphics;opengl;gl008;skipped"
    SKIP_RETURN_CODE 77)
