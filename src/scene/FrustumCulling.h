#pragma once

#include <dzc/Bounds3d.h>
#include <dzc/Result.h>
#include <dzc/ViewFrustum.h>

#include <cstdint>
#include <optional>

namespace dzc {

enum class FrustumClassification : std::uint8_t {
    Outside,
    Intersecting,
    Inside
};

struct FrustumCullingResult final {
    FrustumClassification classification;
    std::optional<ViewFrustum::PlaneIndex> separatingPlane;
};

class FrustumCulling final {
public:
    static Result<FrustumCullingResult> classify(
        const ViewFrustum& frustum,
        const Bounds3d& bounds,
        std::optional<ViewFrustum::PlaneIndex> previousSeparatingPlane =
            std::nullopt) noexcept;
};

} // namespace dzc