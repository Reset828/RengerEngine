#pragma once

#include "data/chunk/Bounds3d.h"
#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <vector>

namespace dzc {

struct CoordinateLocalizationResult final {
    Bounds3d bounds;
    glm::dvec3 origin{0.0};
    std::vector<glm::vec3> localPositions;
};

class CoordinateLocalizer final {
public:
    // Builds a finite Chunk-centered float local-position stream from source positions.
    static Result<CoordinateLocalizationResult> localize(
        const std::vector<glm::dvec3>& sourcePositions) noexcept;
};

} // namespace dzc