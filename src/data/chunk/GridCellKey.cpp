#include "data/chunk/GridCellKey.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr double kInt64Minimum = -0x1p63;
constexpr double kInt64MaximumExclusive = 0x1p63;

Error invalidGridCellKeyError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Grid cell key input is invalid",
        diagnosticMessage,
        "GridCellKey::fromPosition"};
}

Result<std::int64_t> checkedIndex(double position, double datasetMinimum, double cellSize) noexcept {
    if (!std::isfinite(position) || !std::isfinite(datasetMinimum)) {
        return Result<std::int64_t>::failure(
            invalidGridCellKeyError("Grid position and dataset minimum must be finite."));
    }

    const double offset = position - datasetMinimum;
    if (!std::isfinite(offset)) {
        return Result<std::int64_t>::failure(
            invalidGridCellKeyError("Grid coordinate subtraction is not representable as a finite double."));
    }

    const double normalized = offset / cellSize;
    if (!std::isfinite(normalized)) {
        return Result<std::int64_t>::failure(
            invalidGridCellKeyError("Grid coordinate division is not finite."));
    }

    const double floored = std::floor(normalized);
    if (!std::isfinite(floored) || floored < kInt64Minimum || floored >= kInt64MaximumExclusive) {
        return Result<std::int64_t>::failure(
            invalidGridCellKeyError("Grid cell index is outside the int64 range."));
    }

    return Result<std::int64_t>::success(static_cast<std::int64_t>(floored));
}

} // namespace

Result<GridCellKey> GridCellKey::fromPosition(
    const glm::dvec3& position,
    const glm::dvec3& datasetMinimum,
    double cellSize) noexcept {
    if (!std::isfinite(cellSize) || cellSize <= 0.0) {
        return Result<GridCellKey>::failure(
            invalidGridCellKeyError("Grid cell size must be finite and positive."));
    }

    const auto xIndex = checkedIndex(position.x, datasetMinimum.x, cellSize);
    const auto yIndex = checkedIndex(position.y, datasetMinimum.y, cellSize);
    const auto zIndex = checkedIndex(position.z, datasetMinimum.z, cellSize);
    if (!xIndex.hasValue() || !yIndex.hasValue() || !zIndex.hasValue()) {
        return Result<GridCellKey>::failure(
            invalidGridCellKeyError("Grid position cannot be mapped to an int64 cell key."));
    }

    return Result<GridCellKey>::success(GridCellKey{
        xIndex.value(),
        yIndex.value(),
        zIndex.value()});
}

} // namespace dzc
