#pragma once

#include "data/chunk/Dataset.h"
#include "dzc/Result.h"
#include "dzc/ViewFrustum.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace dzc {

struct DrawChunk final {
    ChunkId chunkId;
    std::uint64_t pointCount{0U};
    glm::vec3 relativeOrigin{0.0F};
    AttributeSchema schema;
};

struct GridVisibilityResult final {
    std::vector<DrawChunk> draws;
    std::uint64_t totalPointCount{0U};
    std::uint64_t visiblePointCount{0U};
    std::uint64_t visibleChunkCount{0U};
};

class GridVisibilitySelector final {
public:
    GridVisibilitySelector();
    ~GridVisibilitySelector();

    GridVisibilitySelector(const GridVisibilitySelector&) = delete;
    GridVisibilitySelector& operator=(const GridVisibilitySelector&) = delete;
    GridVisibilitySelector(GridVisibilitySelector&&) noexcept;
    GridVisibilitySelector& operator=(GridVisibilitySelector&&) noexcept;

    Result<GridVisibilityResult> select(
        const Dataset& dataset,
        const ViewFrustum& frustum);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
