#include "data/chunk/Dataset.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t attributeMask(dzc::PointAttribute attribute) noexcept {
    return static_cast<std::uint32_t>(attribute);
}

constexpr std::uint32_t kPositionMask = attributeMask(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask = attributeMask(dzc::PointAttribute::Color);
constexpr std::uint32_t kIntensityMask = attributeMask(dzc::PointAttribute::Intensity);
constexpr std::uint32_t kUnknownMask = 1U << 31U;

void assertCorruptData(const dzc::Result<dzc::Dataset>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::DataFormat);
    assert(result.error().code == 2U);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

dzc::Bounds3d bounds(double minimum, double maximum) {
    dzc::Bounds3d result{};
    result.minimum = glm::dvec3{minimum};
    result.maximum = glm::dvec3{maximum};
    return result;
}

dzc::Chunk makeChunk(std::uint64_t id, std::uint64_t pointCount,
                     std::uint32_t schemaMask = kPositionMask,
                     dzc::Bounds3d chunkBounds = bounds(1.0, 2.0)) {
    dzc::ChunkMetadata metadata{};
    metadata.id = dzc::ChunkId{id};
    metadata.pointCount = pointCount;
    metadata.bounds = chunkBounds;
    metadata.origin = glm::dvec3{1.5};
    metadata.schema.mask = schemaMask;
    auto result = dzc::Chunk::create(std::move(metadata));
    assert(result.hasValue());
    return std::move(result.value());
}

dzc::DatasetMetadata validMetadata(std::uint32_t schemaMask = kPositionMask) {
    dzc::DatasetMetadata metadata{};
    metadata.id = dzc::DatasetId{91U};
    metadata.sourcePath = "D:/data/sample.pcd";
    metadata.sourceIdentityHash = 0x123456789ABCDEF0U;
    metadata.state = dzc::DatasetState::Ready;
    metadata.schema.mask = schemaMask;
    metadata.intensityMetadata.available = true;
    metadata.intensityMetadata.sourceMinimum = -10.0;
    metadata.intensityMetadata.sourceMaximum = 100.0;
    metadata.intensityMetadata.validMinimum = 0.0;
    metadata.intensityMetadata.validMaximum = 80.0;
    metadata.bounds = bounds(0.0, 10.0);
    metadata.origin = glm::dvec3{5.0};
    return metadata;
}

dzc::Dataset createDataset(dzc::DatasetMetadata metadata, std::vector<dzc::Chunk> chunks) {
    auto result = dzc::Dataset::create(std::move(metadata), std::move(chunks));
    assert(result.hasValue());
    return std::move(result.value());
}

void testMetadataStatisticsAndStableChunkQueries() {
    const std::uint32_t schemaMask = kPositionMask | kColorMask | kIntensityMask | kUnknownMask;
    dzc::DatasetMetadata metadata = validMetadata(schemaMask);
    std::vector<dzc::Chunk> chunks{};
    chunks.push_back(makeChunk(4U, 2U, schemaMask, bounds(1.0, 2.0)));
    chunks.push_back(makeChunk(9U, 3U, schemaMask, bounds(4.0, 8.0)));

    dzc::Dataset dataset = createDataset(std::move(metadata), std::move(chunks));
    assert(dataset.metadata().id == dzc::DatasetId{91U});
    assert(dataset.metadata().sourcePath == "D:/data/sample.pcd");
    assert(dataset.metadata().sourceIdentityHash.has_value());
    assert(dataset.metadata().sourceIdentityHash.value() == 0x123456789ABCDEF0U);
    assert(dataset.metadata().state == dzc::DatasetState::Ready);
    assert(dataset.metadata().schema.mask == schemaMask);
    assert(dataset.metadata().intensityMetadata.available);
    assert(dataset.chunkCount() == 2U);
    assert(dataset.totalPointCount() == 5U);

    const dzc::Chunk* first = dataset.findChunk(dzc::ChunkId{4U});
    const dzc::Chunk* second = dataset.findChunk(dzc::ChunkId{9U});
    assert(first != nullptr);
    assert(second != nullptr);
    assert(first->metadata().id == dzc::ChunkId{4U});
    assert(second->metadata().id == dzc::ChunkId{9U});
    assert(dataset.findChunk(dzc::ChunkId{404U}) == nullptr);
    assert(dataset.findChunk(dzc::ChunkId{4U}) == first);
}

void testEmptyIndexIsValid() {
    dzc::DatasetMetadata metadata = validMetadata();
    metadata.sourceIdentityHash.reset();
    metadata.intensityMetadata = dzc::IntensityMetadata{};
    metadata.state = dzc::DatasetState::Opening;
    dzc::Dataset dataset = createDataset(std::move(metadata), {});
    assert(dataset.chunkCount() == 0U);
    assert(dataset.totalPointCount() == 0U);
    assert(dataset.findChunk(dzc::ChunkId{1U}) == nullptr);
    assert(!dataset.metadata().sourceIdentityHash.has_value());
    assert(dataset.metadata().state == dzc::DatasetState::Opening);
}

void testDatasetIdsAreIsolated() {
    auto firstMetadata = validMetadata();
    firstMetadata.id = dzc::DatasetId{1U};
    auto secondMetadata = validMetadata();
    secondMetadata.id = dzc::DatasetId{2U};
    secondMetadata.sourcePath = "D:/data/second.ply";

    dzc::Dataset first = createDataset(std::move(firstMetadata), {makeChunk(10U, 2U)});
    dzc::Dataset second = createDataset(std::move(secondMetadata), {makeChunk(10U, 7U)});
    assert(first.metadata().id != second.metadata().id);
    assert(first.metadata().sourcePath != second.metadata().sourcePath);
    assert(first.totalPointCount() == 2U);
    assert(second.totalPointCount() == 7U);
    assert(first.findChunk(dzc::ChunkId{10U})->metadata().pointCount == 2U);
    assert(second.findChunk(dzc::ChunkId{10U})->metadata().pointCount == 7U);
}

void testDatasetMetadataValidation() {
    auto zeroId = validMetadata();
    zeroId.id = dzc::DatasetId{};
    assertCorruptData(dzc::Dataset::create(std::move(zeroId), {}));

    auto emptyPath = validMetadata();
    emptyPath.sourcePath.clear();
    assertCorruptData(dzc::Dataset::create(std::move(emptyPath), {}));

    auto noPosition = validMetadata();
    noPosition.schema.mask = kColorMask;
    assertCorruptData(dzc::Dataset::create(std::move(noPosition), {}));

    auto invalidBounds = validMetadata();
    invalidBounds.bounds = dzc::Bounds3d{};
    assertCorruptData(dzc::Dataset::create(std::move(invalidBounds), {}));

    auto nanOrigin = validMetadata();
    nanOrigin.origin.x = std::numeric_limits<double>::quiet_NaN();
    assertCorruptData(dzc::Dataset::create(std::move(nanOrigin), {}));

    auto infiniteOrigin = validMetadata();
    infiniteOrigin.origin.z = std::numeric_limits<double>::infinity();
    assertCorruptData(dzc::Dataset::create(std::move(infiniteOrigin), {}));

    auto invalidState = validMetadata();
    invalidState.state = static_cast<dzc::DatasetState>(255U);
    assertCorruptData(dzc::Dataset::create(std::move(invalidState), {}));
}

void testIntensityMetadataValidation() {
    auto nonAvailableNonZero = validMetadata();
    nonAvailableNonZero.intensityMetadata = dzc::IntensityMetadata{};
    nonAvailableNonZero.intensityMetadata.sourceMinimum = 1.0;
    assertCorruptData(dzc::Dataset::create(std::move(nonAvailableNonZero), {}));

    auto nonFinite = validMetadata();
    nonFinite.intensityMetadata.sourceMaximum = std::numeric_limits<double>::infinity();
    assertCorruptData(dzc::Dataset::create(std::move(nonFinite), {}));

    auto sourceReversed = validMetadata();
    sourceReversed.intensityMetadata.sourceMinimum = 2.0;
    sourceReversed.intensityMetadata.sourceMaximum = 1.0;
    assertCorruptData(dzc::Dataset::create(std::move(sourceReversed), {}));

    auto validReversed = validMetadata();
    validReversed.intensityMetadata.validMinimum = 9.0;
    validReversed.intensityMetadata.validMaximum = 8.0;
    assertCorruptData(dzc::Dataset::create(std::move(validReversed), {}));

    auto intensityNotDeclared = validMetadata(kPositionMask);
    intensityNotDeclared.intensityMetadata.available = true;
    dzc::Dataset dataset = createDataset(std::move(intensityNotDeclared), {});
    assert(dataset.metadata().intensityMetadata.available);
}

void testChunkIndexValidation() {
    auto zeroChunkId = makeChunk(1U, 2U);
    auto zeroChunkMetadata = zeroChunkId.metadata();
    zeroChunkMetadata.id = dzc::ChunkId{};
    auto zeroChunkResult = dzc::Chunk::create(std::move(zeroChunkMetadata));
    assert(zeroChunkResult.hasValue());
    assertCorruptData(dzc::Dataset::create(validMetadata(), {std::move(zeroChunkResult.value())}));

    assertCorruptData(dzc::Dataset::create(validMetadata(), {makeChunk(1U, 2U), makeChunk(1U, 3U)}));

    assertCorruptData(dzc::Dataset::create(validMetadata(kPositionMask), {makeChunk(1U, 2U, kPositionMask | kColorMask)}));

    assertCorruptData(dzc::Dataset::create(validMetadata(), {makeChunk(1U, 2U, kPositionMask, bounds(-1.0, 2.0))}));

    auto metadata = validMetadata();
    std::vector<dzc::Chunk> overflowChunks{};
    overflowChunks.push_back(makeChunk(1U, std::numeric_limits<std::uint64_t>::max()));
    overflowChunks.push_back(makeChunk(2U, 1U));
    assertCorruptData(dzc::Dataset::create(std::move(metadata), std::move(overflowChunks)));
}

} // namespace

int main() {
    testMetadataStatisticsAndStableChunkQueries();
    testEmptyIndexIsValid();
    testDatasetIdsAreIsolated();
    testDatasetMetadataValidation();
    testIntensityMetadataValidation();
    testChunkIndexValidation();
    return 0;
}
