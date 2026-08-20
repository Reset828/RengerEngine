#include "data/chunk/Bounds3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isEmptyBounds(const Bounds3d& bounds) noexcept {
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    return bounds.minimum == glm::dvec3{positiveInfinity} &&
           bounds.maximum == glm::dvec3{negativeInfinity};
}

Error invalidBoundsError(const char* context) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Bounds input is invalid",
        "Bounds3d requires finite, axis-ordered coordinates.",
        context};
}

} // namespace

bool Bounds3d::isValid() const noexcept {
    return isFinite(minimum) && isFinite(maximum) &&
           minimum.x <= maximum.x &&
           minimum.y <= maximum.y &&
           minimum.z <= maximum.z;
}

bool Bounds3d::isDegenerate() const noexcept {
    if (!isValid()) {
        return false;
    }

    const glm::dvec3 boundsSize = maximum - minimum;
    return boundsSize.x == 0.0 || boundsSize.y == 0.0 || boundsSize.z == 0.0;
}

Result<void> Bounds3d::extend(const glm::dvec3& point) noexcept {
    if (!isFinite(point)) {
        return Result<void>::failure(invalidBoundsError("Bounds3d::extend(point)"));
    }

    if (isEmptyBounds(*this)) {
        minimum = point;
        maximum = point;
        return Result<void>::success();
    }

    if (!isValid()) {
        return Result<void>::failure(invalidBoundsError("Bounds3d::extend(point)"));
    }

    minimum = glm::dvec3{
        std::min(minimum.x, point.x),
        std::min(minimum.y, point.y),
        std::min(minimum.z, point.z)};
    maximum = glm::dvec3{
        std::max(maximum.x, point.x),
        std::max(maximum.y, point.y),
        std::max(maximum.z, point.z)};
    return Result<void>::success();
}

Result<void> Bounds3d::extend(const Bounds3d& bounds) noexcept {
    if (!bounds.isValid()) {
        return Result<void>::failure(invalidBoundsError("Bounds3d::extend(bounds)"));
    }

    if (isEmptyBounds(*this)) {
        minimum = bounds.minimum;
        maximum = bounds.maximum;
        return Result<void>::success();
    }

    if (!isValid()) {
        return Result<void>::failure(invalidBoundsError("Bounds3d::extend(bounds)"));
    }

    minimum = glm::dvec3{
        std::min(minimum.x, bounds.minimum.x),
        std::min(minimum.y, bounds.minimum.y),
        std::min(minimum.z, bounds.minimum.z)};
    maximum = glm::dvec3{
        std::max(maximum.x, bounds.maximum.x),
        std::max(maximum.y, bounds.maximum.y),
        std::max(maximum.z, bounds.maximum.z)};
    return Result<void>::success();
}

Result<glm::dvec3> Bounds3d::center() const noexcept {
    if (!isValid()) {
        return Result<glm::dvec3>::failure(invalidBoundsError("Bounds3d::center"));
    }

    return Result<glm::dvec3>::success(minimum + (maximum - minimum) / 2.0);
}

Result<glm::dvec3> Bounds3d::size() const noexcept {
    if (!isValid()) {
        return Result<glm::dvec3>::failure(invalidBoundsError("Bounds3d::size"));
    }

    return Result<glm::dvec3>::success(maximum - minimum);
}

} // namespace dzc
