#pragma once

#include <dzc/EngineConfig.h>
#include <dzc/EngineTypes.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dzc::render {

// CPU representation of the std140 FrameData block. The arrays are stored in
// GLSL/GLM column-major order and the explicit padding preserves the GPU ABI.
struct alignas(16) FrameData final {
    std::array<float, 16> view{};
    std::array<float, 16> projection{};
    std::array<float, 4> fixedColor{};
    std::array<float, 4> heightRange{};
    std::array<float, 4> intensityRange{};
    float pointSize{1.0F};
    std::uint32_t shadingMode{0U};
    std::array<std::uint32_t, 2> reservedPadding{};
    std::array<float, 4> reservedExtension{};
};

// CPU representation of one std430 ChunkData record.
struct alignas(16) ChunkData final {
    std::array<float, 4> relativeChunkOrigin{};
};

inline constexpr std::uint32_t frameDataBinding = 0U;
inline constexpr std::uint32_t chunkDataBinding = 1U;
inline constexpr std::size_t frameDataSize = 208U;
inline constexpr std::size_t chunkDataStride = 16U;

static_assert(offsetof(FrameData, view) == 0U);
static_assert(offsetof(FrameData, projection) == 64U);
static_assert(offsetof(FrameData, fixedColor) == 128U);
static_assert(offsetof(FrameData, heightRange) == 144U);
static_assert(offsetof(FrameData, intensityRange) == 160U);
static_assert(offsetof(FrameData, pointSize) == 176U);
static_assert(offsetof(FrameData, shadingMode) == 180U);
static_assert(offsetof(FrameData, reservedPadding) == 184U);
static_assert(offsetof(FrameData, reservedExtension) == 192U);
static_assert(sizeof(FrameData) == frameDataSize);
static_assert(alignof(FrameData) == 16U);
static_assert(offsetof(ChunkData, relativeChunkOrigin) == 0U);
static_assert(sizeof(ChunkData) == chunkDataStride);
static_assert(alignof(ChunkData) == 16U);

// Converts a GLM column-major matrix into the explicit shader array layout.
inline std::array<float, 16> toColumnMajorArray(const glm::mat4& matrix) noexcept {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            result[column * 4U + row] = matrix[column][row];
        }
    }
    return result;
}

// Converts the project shading enum to the fixed-width shader scalar.
inline constexpr std::uint32_t toShaderShadingMode(dzc::ShadingMode mode) noexcept {
    return static_cast<std::uint32_t>(mode);
}

// Converts a camera-relative origin into one ChunkData record.
inline ChunkData makeChunkData(const glm::vec3& relativeOrigin) noexcept {
    return ChunkData{{relativeOrigin.x, relativeOrigin.y, relativeOrigin.z, 0.0F}};
}

} // namespace dzc::render
