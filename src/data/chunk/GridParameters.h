#pragma once

#include <dzc/Bounds3d.h>
#include <dzc/Result.h>

#include <cstdint>
#include <optional>

namespace dzc {

class GridParameters final {
public:
    static constexpr std::uint64_t kTargetPointCount = 262144U;

    // Estimates a finite positive cubic grid cell edge from bounds and point count.
    static Result<double> estimateCellSize(
        const Bounds3d& bounds,
        std::optional<std::uint64_t> pointCount) noexcept;
};

} // namespace dzc