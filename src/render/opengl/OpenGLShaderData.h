#pragma once

#include "../common/ShaderData.h"
#include <dzc/Result.h>

#include <cstdint>

namespace dzc::opengl {

enum class OpenGLShaderLayoutErrorCode : std::uint32_t {
    MissingBlock = 1U,
    MissingMember = 2U,
    LayoutMismatch = 3U,
    OperationFailed = 4U
};

struct OpenGLShaderLayoutSnapshot final {
    std::uint32_t frameBinding{0U};
    std::uint32_t frameDataSize{0U};
    std::uint32_t viewOffset{0U};
    std::uint32_t projectionOffset{0U};
    std::uint32_t fixedColorOffset{0U};
    std::uint32_t heightRangeOffset{0U};
    std::uint32_t intensityRangeOffset{0U};
    std::uint32_t pointSizeOffset{0U};
    std::uint32_t shadingModeOffset{0U};
    std::uint32_t reservedPaddingOffset{0U};
    std::uint32_t reservedExtensionOffset{0U};
    std::uint32_t chunkBinding{0U};
    std::uint32_t chunkRelativeOriginOffset{0U};
    std::uint32_t chunkRelativeOriginArrayStride{0U};
};

class OpenGLShaderData final {
public:
    // Validates reflected block bindings, offsets, sizes and array stride.
    static dzc::Result<void> validate(const OpenGLShaderLayoutSnapshot& snapshot);

    // Reflects the layout of an already-linked, current-context program.
    static dzc::Result<OpenGLShaderLayoutSnapshot> queryCurrentProgram(
        std::uint32_t programId);
};

} // namespace dzc::opengl
