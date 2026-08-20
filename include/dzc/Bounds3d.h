#pragma once

#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <limits>

namespace dzc {

struct Bounds3d final {
    glm::dvec3 minimum{std::numeric_limits<double>::infinity()};
    glm::dvec3 maximum{-std::numeric_limits<double>::infinity()};

    // Returns whether all bounds components are finite and ordered.
    bool isValid() const noexcept;

    // Returns whether a valid bounds has zero size on any axis.
    bool isDegenerate() const noexcept;

    // Extends this bounds with a finite source-space point.
    Result<void> extend(const glm::dvec3& point) noexcept;

    // Extends this bounds with a valid source-space bounds.
    Result<void> extend(const Bounds3d& bounds) noexcept;

    // Returns the center of a valid bounds.
    Result<glm::dvec3> center() const noexcept;

    // Returns the size of a valid bounds.
    Result<glm::dvec3> size() const noexcept;
};

} // namespace dzc
