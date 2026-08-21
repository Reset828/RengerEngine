#pragma once

#include "data/chunk/PointAttributes.h"

#include <dzc/Bounds3d.h>

#include <cstdint>

namespace dzc {

struct PointCloudSourceInfo final {
    AttributeSchema schema;
    std::uint64_t declaredPointCount{0U};
    Bounds3d bounds;
    IntensityMetadata intensity;
};

} // namespace dzc
