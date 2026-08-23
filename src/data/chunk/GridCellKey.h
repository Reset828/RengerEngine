#pragma once

#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace dzc {

struct GridCellKey final {
    std::int64_t x{0};
    std::int64_t y{0};
    std::int64_t z{0};

    // Maps a source position to a checked grid cell key.
    static Result<GridCellKey> fromPosition(
        const glm::dvec3& position,
        const glm::dvec3& datasetMinimum,
        double cellSize) noexcept;

    friend constexpr bool operator==(const GridCellKey& lhs, const GridCellKey& rhs) noexcept {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }

    friend constexpr bool operator!=(const GridCellKey& lhs, const GridCellKey& rhs) noexcept {
        return !(lhs == rhs);
    }

    friend constexpr bool operator<(const GridCellKey& lhs, const GridCellKey& rhs) noexcept {
        if (lhs.x != rhs.x) {
            return lhs.x < rhs.x;
        }
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        return lhs.z < rhs.z;
    }
};

} // namespace dzc
