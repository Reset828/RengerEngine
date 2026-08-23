#include "data/chunk/GridParameters.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

Error invalidGridParametersError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Grid parameter input is invalid",
        diagnosticMessage,
        "GridParameters::estimateCellSize"};
}

Result<double> failure(const char* diagnosticMessage) {
    return Result<double>::failure(invalidGridParametersError(diagnosticMessage));
}

bool isFinitePositive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

Result<double> checkedExtent(double minimum, double maximum) noexcept {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) {
        return failure("Grid bounds must contain finite, axis-ordered coordinates.");
    }

    const double extent = maximum - minimum;
    if (!std::isfinite(extent) || extent < 0.0) {
        return failure("Grid bounds extent is not representable as a finite double.");
    }

    return Result<double>::success(extent);
}

} // namespace

Result<double> GridParameters::estimateCellSize(
    const Bounds3d& bounds,
    std::optional<std::uint64_t> pointCount) noexcept {
    if (!bounds.isValid()) {
        return failure("GridParameters requires valid finite bounds.");
    }

    const auto xExtent = checkedExtent(bounds.minimum.x, bounds.maximum.x);
    const auto yExtent = checkedExtent(bounds.minimum.y, bounds.maximum.y);
    const auto zExtent = checkedExtent(bounds.minimum.z, bounds.maximum.z);
    if (!xExtent.hasValue() || !yExtent.hasValue() || !zExtent.hasValue()) {
        return failure("Grid bounds contain an invalid or non-finite extent.");
    }

    const double longestExtent = std::max({xExtent.value(), yExtent.value(), zExtent.value()});
    if (longestExtent == 0.0) {
        return Result<double>::success(1.0);
    }

    if (!pointCount.has_value() || pointCount.value() == 0U) {
        return Result<double>::success(longestExtent);
    }

    const double effectiveX = xExtent.value() == 0.0 ? longestExtent : xExtent.value();
    const double effectiveY = yExtent.value() == 0.0 ? longestExtent : yExtent.value();
    const double effectiveZ = zExtent.value() == 0.0 ? longestExtent : zExtent.value();

    const double effectiveVolume = effectiveX * effectiveY * effectiveZ;
    if (!isFinitePositive(effectiveVolume)) {
        return failure("Grid bounds volume is not representable as a finite positive double.");
    }

    const double scaledVolume = effectiveVolume * static_cast<double>(kTargetPointCount);
    if (!isFinitePositive(scaledVolume)) {
        return failure("Grid bounds target volume is not representable as a finite positive double.");
    }

    const double targetCellVolume = scaledVolume / static_cast<double>(pointCount.value());
    if (!isFinitePositive(targetCellVolume)) {
        return failure("Grid target cell volume is not finite and positive.");
    }

    const double cellSize = std::cbrt(targetCellVolume);
    if (!isFinitePositive(cellSize)) {
        return failure("Grid cell size is not finite and positive.");
    }

    return Result<double>::success(cellSize);
}

} // namespace dzc