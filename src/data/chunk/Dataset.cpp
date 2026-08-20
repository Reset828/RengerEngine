#include "data/chunk/Dataset.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = static_cast<std::uint32_t>(DatasetErrorCode::CorruptData);

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isValidDatasetState(DatasetState state) noexcept {
    switch (state) {
    case DatasetState::None:
    case DatasetState::Opening:
    case DatasetState::Building:
    case DatasetState::Ready:
    case DatasetState::Cancelling:
    case DatasetState::Error:
        return true;
    }

    return false;
}

bool contains(const Bounds3d& outer, const Bounds3d& inner) noexcept {
    return inner.minimum.x >= outer.minimum.x && inner.minimum.y >= outer.minimum.y && inner.minimum.z >= outer.minimum.z &&
           inner.maximum.x <= outer.maximum.x && inner.maximum.y <= outer.maximum.y && inner.maximum.z <= outer.maximum.z;
}

bool isValidIntensityMetadata(const IntensityMetadata& metadata) noexcept {
    if (!metadata.available) {
        return metadata.sourceMinimum == 0.0 && metadata.sourceMaximum == 0.0 &&
               metadata.validMinimum == 0.0 && metadata.validMaximum == 0.0;
    }

    return std::isfinite(metadata.sourceMinimum) && std::isfinite(metadata.sourceMaximum) &&
           std::isfinite(metadata.validMinimum) && std::isfinite(metadata.validMaximum) &&
           metadata.sourceMinimum <= metadata.sourceMaximum &&
           metadata.validMinimum <= metadata.validMaximum;
}

Error corruptDatasetError(const char* diagnostic, const char* context) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Dataset data is invalid",
        diagnostic,
        context};
}

} // namespace

Dataset::Dataset(DatasetMetadata metadata, std::vector<Chunk> chunks, std::uint64_t totalPointCount) noexcept
    : m_metadata(std::move(metadata)),
      m_chunks(std::move(chunks)),
      m_totalPointCount(totalPointCount) {}

Result<Dataset> Dataset::create(DatasetMetadata metadata, std::vector<Chunk> chunks) {
    if (metadata.id.value == 0U) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.id must not be zero.", "Dataset::create"));
    }

    if (metadata.sourcePath.empty()) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.sourcePath must not be empty.", "Dataset::create"));
    }

    if (!isValidDatasetState(metadata.state)) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.state is not a defined DatasetState value.", "Dataset::create"));
    }

    if (!metadata.schema.hasPosition()) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.schema must declare Position.", "Dataset::create"));
    }

    if (!metadata.bounds.isValid()) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.bounds must be valid.", "Dataset::create"));
    }

    if (!isFinite(metadata.origin)) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.origin must have finite components.", "Dataset::create"));
    }

    if (!isValidIntensityMetadata(metadata.intensityMetadata)) {
        return Result<Dataset>::failure(corruptDatasetError("DatasetMetadata.intensityMetadata is invalid.", "Dataset::create"));
    }

    std::uint64_t totalPointCount = 0U;
    for (std::size_t index = 0U; index < chunks.size(); ++index) {
        const ChunkMetadata& chunkMetadata = chunks[index].metadata();
        if (chunkMetadata.id.value == 0U) {
            return Result<Dataset>::failure(corruptDatasetError("Chunk metadata id must not be zero.", "Dataset::create"));
        }

        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (chunks[previous].metadata().id == chunkMetadata.id) {
                return Result<Dataset>::failure(corruptDatasetError("Chunk metadata ids must be unique within a Dataset.", "Dataset::create"));
            }
        }

        if (chunkMetadata.schema.mask != metadata.schema.mask) {
            return Result<Dataset>::failure(corruptDatasetError("Every Chunk schema must exactly match DatasetMetadata.schema.", "Dataset::create"));
        }

        if (!contains(metadata.bounds, chunkMetadata.bounds)) {
            return Result<Dataset>::failure(corruptDatasetError("Every Chunk bounds must be contained by DatasetMetadata.bounds.", "Dataset::create"));
        }

        if (chunkMetadata.pointCount > std::numeric_limits<std::uint64_t>::max() - totalPointCount) {
            return Result<Dataset>::failure(corruptDatasetError("Chunk point counts overflow Dataset totalPointCount.", "Dataset::create"));
        }
        totalPointCount += chunkMetadata.pointCount;
    }

    return Result<Dataset>::success(Dataset(std::move(metadata), std::move(chunks), totalPointCount));
}

const DatasetMetadata& Dataset::metadata() const noexcept {
    return m_metadata;
}

std::size_t Dataset::chunkCount() const noexcept {
    return m_chunks.size();
}

std::uint64_t Dataset::totalPointCount() const noexcept {
    return m_totalPointCount;
}

const Chunk* Dataset::findChunk(ChunkId id) const noexcept {
    for (const Chunk& chunk : m_chunks) {
        if (chunk.metadata().id == id) {
            return &chunk;
        }
    }

    return nullptr;
}

} // namespace dzc
