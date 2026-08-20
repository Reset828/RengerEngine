#pragma once

#include <dzc/Result.h>

#include <glm/glm.hpp>

namespace dzc {

class RelativeOrigin final {
public:
    // Returns the finite float offset from camera origin to Chunk origin.
    static Result<glm::vec3> calculate(
        const glm::dvec3& chunkOrigin,
        const glm::dvec3& cameraOrigin) noexcept;
};

} // namespace dzc
