#include "scene/GridVisibilitySelector.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kPositionMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Color);

void assertDataFormat(const dzc::Result<dzc::GridVisibilityResult>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::DataFormat);
    assert(result.error().code == 2U);
}

dzc::Bounds3d bounds(const glm::dvec3& minimum, const glm::dvec3& maximum) {
    return dzc::Bounds3d{minimum, maximum};
}

dzc::ViewFrustum identityFrustum() {
    const auto result = dzc::ViewFrustum::fromViewProjection(
        glm::mat4{1.0F},
        dzc::ClipDepthRange::NegativeOneToOne);
    assert(result.hasValue());
    return result.value();
}

dzc::Chunk createChunk(
    std::uint64_t id,
    std::uint64_t pointCount,
    const dzc::Bounds3d& chunkBounds,
    std::uint32_t schemaMask = kPositionMask,
    glm::dvec3 origin = glm::dvec3{0.0}) {
    dzc::ChunkMetadata metadata{};
    metadata.id = dzc::ChunkId{id};
    metadata.pointCount = pointCount;
    metadata.bounds = chunkBounds;
    metadata.origin = origin;
    metadata.schema.mask = schemaMask;
    const auto result = dzc::Chunk::create(std::move(metadata));
    assert(result.hasValue());
    return std::move(result.value());
}

void makeCpuResident(dzc::Chunk& chunk) {
    assert(chunk.beginCpuLoad().hasValue());
    dzc::ChunkCpuData data;
    data.positions.resize(static_cast<std::size_t>(chunk.metadata().pointCount));
    if (chunk.metadata().schema.hasColor()) {
        data.colorsRgba8.resize(data.positions.size());
    }
    assert(chunk.completeCpuLoad(std::move(data)).hasValue());
}

void makeGpuResident(dzc::Chunk& chunk) {
    makeCpuResident(chunk);
    assert(chunk.queueUpload().hasValue());
    assert(chunk.completeUpload().hasValue());
}

dzc::Dataset createDataset(
    std::uint64_t id,
    dzc::DatasetState state,
    std::vector<dzc::Chunk> chunks,
    std::uint32_t schemaMask = kPositionMask) {
    dzc::DatasetMetadata metadata{};
    metadata.id = dzc::DatasetId{id};
    metadata.sourcePath = "memory://visibility";
    metadata.state = state;
    metadata.schema.mask = schemaMask;
    metadata.bounds = bounds(glm::dvec3{-10.0}, glm::dvec3{10.0});
    const auto result = dzc::Dataset::create(std::move(metadata), std::move(chunks));
    assert(result.hasValue());
    return std::move(result.value());
}

void testVisibleOrderingAndStatistics() {
    std::vector<dzc::Chunk> chunks;
    chunks.push_back(createChunk(
        11U, 4U, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}),
        kPositionMask | kColorMask, glm::dvec3{10.0, 11.0, 12.0}));
    chunks.push_back(createChunk(
        12U, 7U, bounds(glm::dvec3{2.0, -0.1, -0.1}, glm::dvec3{3.0, 0.1, 0.1}),
        kPositionMask | kColorMask, glm::dvec3{20.0}));
    chunks.push_back(createChunk(
        13U, 5U, bounds(glm::dvec3{0.9, -0.1, -0.1}, glm::dvec3{1.1, 0.1, 0.1}),
        kPositionMask | kColorMask, glm::dvec3{30.0}));
    makeCpuResident(chunks[0]);
    makeGpuResident(chunks[1]);
    makeGpuResident(chunks[2]);

    dzc::Dataset dataset = createDataset(
        41U, dzc::DatasetState::Opening, std::move(chunks), kPositionMask | kColorMask);
    dzc::GridVisibilitySelector selector;
    const auto result = selector.select(dataset, identityFrustum());
    assert(result.hasValue());
    assert(result.value().totalPointCount == 16U);
    assert(result.value().visiblePointCount == 9U);
    assert(result.value().visibleChunkCount == 2U);
    assert(result.value().draws.size() == 2U);
    assert(result.value().draws[0].chunkId == dzc::ChunkId{11U});
    assert(result.value().draws[1].chunkId == dzc::ChunkId{13U});
    assert(result.value().draws[0].pointCount == 4U);
    const glm::vec3 expectedOrigin{10.0F, 11.0F, 12.0F};
    assert(result.value().draws[0].relativeOrigin == expectedOrigin);
    assert(result.value().draws[0].schema.mask == (kPositionMask | kColorMask));
}

void testNonResidentChunksAreNotDrawn() {
    std::vector<dzc::Chunk> chunks;
    chunks.push_back(createChunk(21U, 1U, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5})));
    chunks.push_back(createChunk(22U, 2U, bounds(glm::dvec3{-0.4}, glm::dvec3{0.4})));
    chunks.push_back(createChunk(23U, 3U, bounds(glm::dvec3{-0.3}, glm::dvec3{0.3})));
    chunks.push_back(createChunk(24U, 4U, bounds(glm::dvec3{-0.2}, glm::dvec3{0.2})));
    chunks.push_back(createChunk(25U, 5U, bounds(glm::dvec3{-0.1}, glm::dvec3{0.1})));
    chunks.push_back(createChunk(26U, 6U, bounds(glm::dvec3{0.0}, glm::dvec3{0.1})));
    chunks.push_back(createChunk(27U, 7U, bounds(glm::dvec3{0.1}, glm::dvec3{0.2})));
    makeCpuResident(chunks[1]);
    assert(chunks[2].beginCpuLoad().hasValue());
    assert(chunks[3].beginCpuLoad().hasValue());
    makeCpuResident(chunks[4]);
    assert(chunks[4].beginCpuEviction().hasValue());
    assert(chunks[5].beginCpuLoad().hasValue());
    assert(chunks[5].failCpuLoad().hasValue());
    makeCpuResident(chunks[6]);
    assert(chunks[6].queueUpload().hasValue());

    dzc::Dataset dataset = createDataset(42U, dzc::DatasetState::Ready, std::move(chunks));
    dzc::GridVisibilitySelector selector;
    const auto result = selector.select(dataset, identityFrustum());
    assert(result.hasValue());
    assert(result.value().totalPointCount == 28U);
    assert(result.value().visiblePointCount == 2U);
    assert(result.value().visibleChunkCount == 1U);
    assert(result.value().draws.size() == 1U);
    assert(result.value().draws.front().chunkId == dzc::ChunkId{22U});
}

void testSeparatingPlaneCachePreservesSemantics() {
    std::vector<dzc::Chunk> chunks;
    chunks.push_back(createChunk(
        31U, 8U, bounds(glm::dvec3{2.0, -0.1, -0.1}, glm::dvec3{3.0, 0.1, 0.1})));
    makeCpuResident(chunks[0]);
    dzc::Dataset dataset = createDataset(43U, dzc::DatasetState::Ready, std::move(chunks));
    dzc::GridVisibilitySelector selector;
    const dzc::ViewFrustum frustum = identityFrustum();
    const auto first = selector.select(dataset, frustum);
    const auto second = selector.select(dataset, frustum);
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value().draws.empty());
    assert(second.value().draws.empty());
    assert(first.value().visiblePointCount == second.value().visiblePointCount);
    assert(first.value().visibleChunkCount == second.value().visibleChunkCount);
}

void testInvalidInputsAndEmptyDataset() {
    dzc::Dataset empty = createDataset(44U, dzc::DatasetState::Ready, {});
    dzc::GridVisibilitySelector selector;
    const auto emptyResult = selector.select(empty, identityFrustum());
    assert(emptyResult.hasValue());
    assert(emptyResult.value().draws.empty());
    assert(emptyResult.value().totalPointCount == 0U);
    assert(emptyResult.value().visiblePointCount == 0U);
    assert(emptyResult.value().visibleChunkCount == 0U);

    dzc::ViewFrustum invalidFrustum = identityFrustum();
    invalidFrustum.planes[dzc::ViewFrustum::Top] = dzc::FrustumPlane{glm::dvec4{0.0}};
    const auto invalidFrustumResult = selector.select(empty, invalidFrustum);
    assertDataFormat(invalidFrustumResult);

    std::vector<dzc::Chunk> invalidBoundsChunks;
    invalidBoundsChunks.push_back(createChunk(
        46U, 1U, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5})));
    makeCpuResident(invalidBoundsChunks[0]);
    dzc::Dataset invalidBounds = createDataset(
        46U, dzc::DatasetState::Ready, std::move(invalidBoundsChunks));
    dzc::ChunkMetadata& corruptedMetadata = const_cast<dzc::ChunkMetadata&>(
        invalidBounds.chunks().front().metadata());
    corruptedMetadata.bounds = bounds(glm::dvec3{1.0}, glm::dvec3{-1.0});
    const auto invalidBoundsResult = selector.select(invalidBounds, identityFrustum());
    assertDataFormat(invalidBoundsResult);

    std::vector<dzc::Chunk> chunks;
    chunks.push_back(createChunk(
        45U, 1U, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}),
        kPositionMask, glm::dvec3{std::numeric_limits<double>::max()}));
    makeCpuResident(chunks[0]);
    dzc::Dataset invalidOrigin = createDataset(45U, dzc::DatasetState::Ready, std::move(chunks));
    const auto invalidOriginResult = selector.select(invalidOrigin, identityFrustum());
    assertDataFormat(invalidOriginResult);
}

} // namespace

int main() {
    testVisibleOrderingAndStatistics();
    testNonResidentChunksAreNotDrawn();
    testSeparatingPlaneCachePreservesSemantics();
    testInvalidInputsAndEmptyDataset();
    return 0;
}
