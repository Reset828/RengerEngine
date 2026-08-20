#pragma once

#include "data/chunk/PointAttributes.h"
#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dzc {

struct PointBatch final {
    AttributeSchema schema;
    std::vector<glm::dvec3> positions;
    std::vector<std::uint32_t> colorsRgba8;
    std::vector<std::uint16_t> intensities;

    // Validates schema declarations against the structure-of-arrays streams.
    Result<void> validate() const noexcept;
};

} // namespace dzc
