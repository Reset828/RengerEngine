#pragma once

#include "data/chunk/GridCellKey.h"
#include "data/chunk/PointBatch.h"
#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace dzc {

struct GridBucket final {
    GridCellKey key;
    PointBatch points;
    std::vector<std::uint64_t> sourceIndices;
};

class GridBucketStore final {
public:
    static Result<GridBucketStore> create(
        const glm::dvec3& datasetMinimum,
        double cellSize,
        std::uint64_t byteBudget);

    Result<void> appendBatch(const PointBatch& batch);

    std::vector<GridBucket> snapshot() const;

    void clear() noexcept;

    std::uint64_t residentBytes() const noexcept;
    std::uint64_t pointCount() const noexcept;

private:
    GridBucketStore(
        const glm::dvec3& datasetMinimum,
        double cellSize,
        std::uint64_t byteBudget) noexcept;

    glm::dvec3 m_datasetMinimum;
    double m_cellSize{0.0};
    std::uint64_t m_byteBudget{0U};
    std::uint64_t m_residentBytes{0U};
    std::uint64_t m_pointCount{0U};
    std::uint64_t m_nextSourceIndex{0U};
    std::optional<AttributeSchema> m_schema;
    std::map<GridCellKey, GridBucket> m_buckets;
};

} // namespace dzc