#include "OpenGLShaderData.h"

#include <utility>

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <string>

namespace dzc::opengl {
namespace {

using dzc::render::chunkDataBinding;
using dzc::render::chunkDataStride;
using dzc::render::frameDataBinding;
using dzc::render::frameDataSize;
using dzc::render::FrameData;
using dzc::render::ChunkData;

static_assert(frameDataBinding == 0U);
static_assert(chunkDataBinding == 1U);
static_assert(sizeof(FrameData) == frameDataSize);
static_assert(offsetof(FrameData, view) == 0U);
static_assert(offsetof(FrameData, projection) == 64U);
static_assert(offsetof(FrameData, fixedColor) == 128U);
static_assert(offsetof(FrameData, heightRange) == 144U);
static_assert(offsetof(FrameData, intensityRange) == 160U);
static_assert(offsetof(FrameData, pointSize) == 176U);
static_assert(offsetof(FrameData, shadingMode) == 180U);
static_assert(offsetof(FrameData, reservedPadding) == 184U);
static_assert(offsetof(FrameData, reservedExtension) == 192U);
static_assert(sizeof(ChunkData) == chunkDataStride);
static_assert(offsetof(ChunkData, relativeChunkOrigin) == 0U);

constexpr std::uint32_t errorCode(OpenGLShaderLayoutErrorCode code) noexcept {
    return static_cast<std::uint32_t>(code);
}

dzc::Result<void> failure(
    OpenGLShaderLayoutErrorCode code,
    const char* userMessage,
    std::string diagnostic,
    const char* context) {
    return dzc::Result<void>::failure(dzc::Error{
        dzc::ErrorDomain::OpenGL,
        errorCode(code),
        userMessage,
        std::move(diagnostic),
        context});
}

bool expected(const OpenGLShaderLayoutSnapshot& value) noexcept {
    return value.frameBinding == frameDataBinding
        && value.frameDataSize == frameDataSize
        && value.viewOffset == 0U
        && value.projectionOffset == 64U
        && value.fixedColorOffset == 128U
        && value.heightRangeOffset == 144U
        && value.intensityRangeOffset == 160U
        && value.pointSizeOffset == 176U
        && value.shadingModeOffset == 180U
        && value.reservedPaddingOffset == 184U
        && value.reservedExtensionOffset == 192U
        && value.chunkBinding == chunkDataBinding
        && value.chunkRelativeOriginOffset == 0U
        && value.chunkRelativeOriginArrayStride == chunkDataStride;
}

bool queryUniformOffsets(
    GLuint program,
    OpenGLShaderLayoutSnapshot& snapshot,
    std::string& diagnostic) {
    const std::array<const GLchar*, 9U> names{
        "view", "projection", "fixedColor", "heightRange", "intensityRange",
        "pointSize", "shadingMode", "reservedPadding", "reservedExtension"};
    std::array<GLuint, names.size()> indices{};
    glGetUniformIndices(program, static_cast<GLsizei>(names.size()), names.data(), indices.data());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] == GL_INVALID_INDEX) {
            diagnostic = "Uniform member is not active: " + std::string(names[i]);
            return false;
        }
    }
    std::array<GLint, names.size()> offsets{};
    glGetActiveUniformsiv(
        program,
        static_cast<GLsizei>(indices.size()),
        indices.data(),
        GL_UNIFORM_OFFSET,
        offsets.data());
    snapshot.viewOffset = static_cast<std::uint32_t>(offsets[0]);
    snapshot.projectionOffset = static_cast<std::uint32_t>(offsets[1]);
    snapshot.fixedColorOffset = static_cast<std::uint32_t>(offsets[2]);
    snapshot.heightRangeOffset = static_cast<std::uint32_t>(offsets[3]);
    snapshot.intensityRangeOffset = static_cast<std::uint32_t>(offsets[4]);
    snapshot.pointSizeOffset = static_cast<std::uint32_t>(offsets[5]);
    snapshot.shadingModeOffset = static_cast<std::uint32_t>(offsets[6]);
    snapshot.reservedPaddingOffset = static_cast<std::uint32_t>(offsets[7]);
    snapshot.reservedExtensionOffset = static_cast<std::uint32_t>(offsets[8]);
    return true;
}

} // namespace

dzc::Result<void> OpenGLShaderData::validate(
    const OpenGLShaderLayoutSnapshot& snapshot) {
    if (snapshot.frameBinding != frameDataBinding || snapshot.chunkBinding != chunkDataBinding) {
        return failure(
            OpenGLShaderLayoutErrorCode::LayoutMismatch,
            "Shader block bindings do not match the renderer contract",
            "Expected FrameData binding 0 and ChunkData binding 1",
            "OpenGLShaderData::validate bindings");
    }
    if (snapshot.frameDataSize != frameDataSize || snapshot.chunkRelativeOriginArrayStride != chunkDataStride) {
        return failure(
            OpenGLShaderLayoutErrorCode::LayoutMismatch,
            "Shader block sizes do not match the renderer contract",
            "Expected FrameData size 208 and ChunkData array stride 16",
            "OpenGLShaderData::validate sizes");
    }
    if (!expected(snapshot)) {
        return failure(
            OpenGLShaderLayoutErrorCode::LayoutMismatch,
            "Shader member offsets do not match the renderer contract",
            "Reflected member offsets differ from the fixed std140/std430 Golden layout",
            "OpenGLShaderData::validate offsets");
    }
    return dzc::Result<void>::success();
}

dzc::Result<OpenGLShaderLayoutSnapshot> OpenGLShaderData::queryCurrentProgram(
    std::uint32_t programId) {
    if (programId == 0U) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(dzc::Error{
            dzc::ErrorDomain::OpenGL,
            errorCode(OpenGLShaderLayoutErrorCode::OperationFailed),
            "A linked OpenGL shader program is required",
            "queryCurrentProgram received program ID 0",
            "OpenGLShaderData::queryCurrentProgram"});
    }

    const GLuint program = static_cast<GLuint>(programId);
    const GLuint frameIndex = glGetUniformBlockIndex(program, "FrameData");
    if (frameIndex == GL_INVALID_INDEX) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(dzc::Error{
            dzc::ErrorDomain::OpenGL,
            errorCode(OpenGLShaderLayoutErrorCode::MissingBlock),
            "FrameData block is missing from the linked shader",
            "glGetUniformBlockIndex did not find FrameData",
            "OpenGLShaderData::queryCurrentProgram FrameData"});
    }

    OpenGLShaderLayoutSnapshot snapshot{};
    GLint value = 0;
    glGetActiveUniformBlockiv(program, frameIndex, GL_UNIFORM_BLOCK_BINDING, &value);
    snapshot.frameBinding = value < 0 ? 0U : static_cast<std::uint32_t>(value);
    glGetActiveUniformBlockiv(program, frameIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &value);
    snapshot.frameDataSize = value < 0 ? 0U : static_cast<std::uint32_t>(value);

    std::string diagnostic;
    if (!queryUniformOffsets(program, snapshot, diagnostic)) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(dzc::Error{
            dzc::ErrorDomain::OpenGL,
            errorCode(OpenGLShaderLayoutErrorCode::MissingMember),
            "FrameData member is missing from the linked shader",
            diagnostic,
            "OpenGLShaderData::queryCurrentProgram FrameData members"});
    }

    const GLuint chunkIndex = glGetProgramResourceIndex(
        program, GL_SHADER_STORAGE_BLOCK, "ChunkData");
    if (chunkIndex == GL_INVALID_INDEX) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(dzc::Error{
            dzc::ErrorDomain::OpenGL,
            errorCode(OpenGLShaderLayoutErrorCode::MissingBlock),
            "ChunkData block is missing from the linked shader",
            "glGetProgramResourceIndex did not find ChunkData",
            "OpenGLShaderData::queryCurrentProgram ChunkData"});
    }

    const GLenum blockProperties[] = {GL_BUFFER_BINDING};
    GLint blockParams[] = {0};
    glGetProgramResourceiv(
        program, GL_SHADER_STORAGE_BLOCK, chunkIndex, 1, blockProperties, 1, nullptr,
        blockParams);
    snapshot.chunkBinding = blockParams[0] < 0 ? 0U : static_cast<std::uint32_t>(blockParams[0]);

    GLuint memberIndex = glGetProgramResourceIndex(
        program, GL_BUFFER_VARIABLE, "relativeChunkOrigin[0]");
    if (memberIndex == GL_INVALID_INDEX) {
        memberIndex = glGetProgramResourceIndex(
            program, GL_BUFFER_VARIABLE, "relativeChunkOrigin");
    }
    if (memberIndex == GL_INVALID_INDEX) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(dzc::Error{
            dzc::ErrorDomain::OpenGL,
            errorCode(OpenGLShaderLayoutErrorCode::MissingMember),
            "ChunkData relativeChunkOrigin member is missing",
            "glGetProgramResourceIndex did not find relativeChunkOrigin",
            "OpenGLShaderData::queryCurrentProgram ChunkData member"});
    }

    const GLenum memberProperties[] = {GL_OFFSET, GL_ARRAY_STRIDE};
    GLint memberParams[] = {0, 0};
    glGetProgramResourceiv(
        program, GL_BUFFER_VARIABLE, memberIndex, 2, memberProperties, 2, nullptr,
        memberParams);
    snapshot.chunkRelativeOriginOffset = memberParams[0] < 0 ? 0U : static_cast<std::uint32_t>(memberParams[0]);
    snapshot.chunkRelativeOriginArrayStride = memberParams[1] < 0 ? 0U : static_cast<std::uint32_t>(memberParams[1]);

    auto validation = OpenGLShaderData::validate(snapshot);
    if (!validation.hasValue()) {
        return dzc::Result<OpenGLShaderLayoutSnapshot>::failure(validation.error());
    }
    return dzc::Result<OpenGLShaderLayoutSnapshot>::success(snapshot);
}

} // namespace dzc::opengl
