if(NOT DEFINED DZC_GLAD_SOURCE OR NOT DEFINED DZC_GLAD_INCLUDE_DIR)
    message(FATAL_ERROR
        "GLAD was not configured before creating dzc_render_opengl; "
        "check DZC_GLAD_ROOT while DZC_ENABLE_OPENGL=ON.")
endif()

add_library(dzc_render_opengl STATIC
    "${DZC_GLAD_SOURCE}"
    OpenGLCapabilities.cpp
    GlResource.cpp
    GlBuffer.cpp
    GlVertexArray.cpp
    GlShaderProgram.cpp
    OpenGLShaderData.cpp
)

# The project enables only CXX; generated GLAD C sources are C++ compatible.
set_source_files_properties("${DZC_GLAD_SOURCE}" PROPERTIES LANGUAGE CXX)

target_compile_features(dzc_render_opengl PUBLIC cxx_std_17)
target_include_directories(dzc_render_opengl PRIVATE
    "${DZC_GLAD_INCLUDE_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(dzc_render_opengl
    PUBLIC dzc_render_api
    PRIVATE OpenGL::GL
)
