#pragma once

#include <dzc/Bounds3d.h>
#include <dzc/Result.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace dzc {

enum class ClipDepthRange : std::uint8_t {
    NegativeOneToOne,
    ZeroToOne
};

struct FrustumPlane final {
    glm::dvec4 equation{0.0};

    // Returns a copy with its plane equation normalized by the normal length.
    Result<FrustumPlane> normalized() const noexcept;
};

struct ViewFrustum final {
    enum PlaneIndex : std::size_t {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far
    };

    std::array<FrustumPlane, 6> planes{};

    // Normalizes all six planes without modifying this value.
    Result<ViewFrustum> normalized() const noexcept;

    // Extracts and normalizes planes from a float view-projection matrix.
    static Result<ViewFrustum> fromViewProjection(
        const glm::mat4& viewProjection,
        ClipDepthRange depthRange) noexcept;

    // Returns true for an AABB that intersects or lies inside this frustum.
    Result<bool> intersects(const Bounds3d& bounds) const noexcept;
};

} // namespace dzc
