#include "render/common/ShaderData.h"
#include "render/opengl/OpenGLShaderData.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

using dzc::opengl::OpenGLShaderData;
using dzc::opengl::OpenGLShaderLayoutSnapshot;
using dzc::render::ChunkData;
using dzc::render::FrameData;

OpenGLShaderLayoutSnapshot goldenLayout() {
    return OpenGLShaderLayoutSnapshot{
        0U, 208U, 0U, 64U, 128U, 144U, 160U, 176U, 180U,
        184U, 192U, 1U, 0U, 16U};
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void testGoldenLayout() {
    static_assert(sizeof(FrameData) == 208U);
    static_assert(offsetof(FrameData, view) == 0U);
    static_assert(offsetof(FrameData, projection) == 64U);
    static_assert(offsetof(FrameData, fixedColor) == 128U);
    static_assert(offsetof(FrameData, heightRange) == 144U);
    static_assert(offsetof(FrameData, intensityRange) == 160U);
    static_assert(offsetof(FrameData, pointSize) == 176U);
    static_assert(offsetof(FrameData, shadingMode) == 180U);
    static_assert(offsetof(FrameData, reservedPadding) == 184U);
    static_assert(offsetof(FrameData, reservedExtension) == 192U);
    static_assert(sizeof(ChunkData) == 16U);
    static_assert(offsetof(ChunkData, relativeChunkOrigin) == 0U);

    assert(OpenGLShaderData::validate(goldenLayout()).hasValue());
    auto invalid = goldenLayout();
    invalid.frameDataSize = 192U;
    const auto result = OpenGLShaderData::validate(invalid);
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::OpenGL);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testMatrixAndScalarWrites() {
    glm::mat4 matrix(0.0F);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            matrix[column][row] = static_cast<float>(column * 10 + row);
        }
    }
    const auto values = dzc::render::toColumnMajorArray(matrix);
    for (std::size_t i = 0; i < values.size(); ++i) {
        assert(values[i] == static_cast<float>((i / 4U) * 10U + (i % 4U)));
    }

    FrameData frame{};
    frame.view = values;
    frame.projection = values;
    frame.fixedColor = {1.0F, 0.5F, 0.25F, 1.0F};
    frame.heightRange = {-2.0F, 8.0F, 0.0F, 0.0F};
    frame.intensityRange = {10.0F, 100.0F, 0.0F, 0.0F};
    frame.pointSize = 4.0F;
    frame.shadingMode = dzc::render::toShaderShadingMode(dzc::ShadingMode::Intensity);
    assert(frame.view[15] == 33.0F);
    assert(frame.fixedColor[3] == 1.0F);
    assert(frame.heightRange[1] == 8.0F);
    assert(frame.intensityRange[0] == 10.0F);
    assert(frame.pointSize == 4.0F);
    assert(frame.shadingMode == 3U);

    const ChunkData chunk = dzc::render::makeChunkData(glm::vec3{2.0F, -3.0F, 4.5F});
    assert(chunk.relativeChunkOrigin[0] == 2.0F);
    assert(chunk.relativeChunkOrigin[1] == -3.0F);
    assert(chunk.relativeChunkOrigin[2] == 4.5F);
    assert(chunk.relativeChunkOrigin[3] == 0.0F);
}

void testShaderContract() {
    const std::filesystem::path root = DZC_SOURCE_DIR;
    const std::string vertex = readText(root / "shaders" / "opengl" / "point_cloud.vert");
    const std::string fragment = readText(root / "shaders" / "opengl" / "point_cloud.frag");
    for (const std::string& source : {vertex, fragment}) {
        assert(source.find("#version 450 core") != std::string::npos);
    }
    assert(vertex.find("layout(location = 0) in vec3 position") != std::string::npos);
    assert(vertex.find("layout(location = 1) in vec4 color") != std::string::npos);
    assert(vertex.find("layout(location = 2) in float intensity") != std::string::npos);
    assert(vertex.find("layout(std140, binding = 0) uniform FrameData") != std::string::npos);
    assert(vertex.find("layout(std430, binding = 1) readonly buffer ChunkData") != std::string::npos);
    for (const std::string& member : {
             "mat4 view", "mat4 projection", "vec4 fixedColor", "vec4 heightRange",
             "vec4 intensityRange", "float pointSize", "uint shadingMode",
             "vec2 reservedPadding", "vec4 reservedExtension", "vec4 relativeChunkOrigin[]"}) {
        assert(vertex.find(member) != std::string::npos);
    }
    assert(vertex.find("projection * view") != std::string::npos);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--real-context") {
        std::cout << "Skipped: GL-005 real block reflection requires a Context supplied by GL-007.\n";
        return 77;
    }
    testGoldenLayout();
    testMatrixAndScalarWrites();
    testShaderContract();
    std::cout << "Shader layout tests passed\n";
    return 0;
}
