#pragma once

#include "data/chunk/Chunk.h"

#include <dzc/EngineSnapshot.h>
#include <dzc/Result.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dzc {

struct DatasetMetadata final {
    DatasetId id;
    std::string sourcePath;
    std::optional<std::uint64_t> sourceIdentityHash;
    DatasetState state{DatasetState::None};
    AttributeSchema schema;
    IntensityMetadata intensityMetadata;
    Bounds3d bounds;
    glm::dvec3 origin{0.0};
};

enum class DatasetErrorCode : std::uint32_t {
    CorruptData = 2U
};

class Dataset final {
public:
    static Result<Dataset> create(DatasetMetadata metadata, std::vector<Chunk> chunks);

    const DatasetMetadata& metadata() const noexcept;
    std::size_t chunkCount() const noexcept;
    std::uint64_t totalPointCount() const noexcept;
    const Chunk* findChunk(ChunkId id) const noexcept;

private:
    Dataset(DatasetMetadata metadata, std::vector<Chunk> chunks, std::uint64_t totalPointCount) noexcept;

    DatasetMetadata m_metadata;
    std::vector<Chunk> m_chunks;
    std::uint64_t m_totalPointCount{0U};
};

} // namespace dzc
