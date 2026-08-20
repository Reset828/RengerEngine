#include "data/chunk/Chunk.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

constexpr std::uint32_t attributeMask(dzc::PointAttribute attribute) noexcept {
    return static_cast<std::uint32_t>(attribute);
}

constexpr std::uint32_t kPositionMask = attributeMask(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask = attributeMask(dzc::PointAttribute::Color);
constexpr std::uint32_t kIntensityMask = attributeMask(dzc::PointAttribute::Intensity);
constexpr std::uint32_t kUnknownMask = 1U << 31U;

void assertError(const dzc::Result<void>& result, dzc::ErrorDomain domain, std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void assertCreateError(const dzc::Result<dzc::Chunk>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::DataFormat);
    assert(result.error().code == 2U);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

dzc::ChunkMetadata validMetadata(std::uint32_t schemaMask = kPositionMask) {
    dzc::ChunkMetadata metadata{};
    metadata.id = dzc::ChunkId{41U};
    metadata.pointCount = 2U;
    metadata.bounds.minimum = glm::dvec3{10.0, 20.0, 30.0};
    metadata.bounds.maximum = glm::dvec3{12.0, 22.0, 32.0};
    metadata.origin = glm::dvec3{11.0, 21.0, 31.0};
    metadata.schema.mask = schemaMask;
    return metadata;
}

dzc::ChunkCpuData validCpuData(std::uint32_t schemaMask = kPositionMask) {
    dzc::ChunkCpuData data{};
    data.positions = {glm::vec3{-1.0F, 0.0F, 1.0F}, glm::vec3{1.0F, 0.0F, -1.0F}};
    if ((schemaMask & kColorMask) != 0U) {
        data.colorsRgba8 = {0x11223344U, 0x55667788U};
    }
    if ((schemaMask & kIntensityMask) != 0U) {
        data.intensities = {5U, 65535U};
    }
    return data;
}

dzc::Chunk createChunk(std::uint32_t schemaMask = kPositionMask) {
    auto result = dzc::Chunk::create(validMetadata(schemaMask));
    assert(result.hasValue());
    return std::move(result.value());
}

void loadCpuData(dzc::Chunk& chunk, std::uint32_t schemaMask = kPositionMask) {
    assert(chunk.beginCpuLoad().hasValue());
    assert(chunk.completeCpuLoad(validCpuData(schemaMask)).hasValue());
    assert(chunk.state() == dzc::ChunkState::CpuResident);
    assert(chunk.hasCpuData());
}

void assertInvalidStateKeepsChunk(dzc::Chunk& chunk, const dzc::Result<void>& result,
                                  dzc::ChunkState expectedState, bool expectedCpuData) {
    assertError(result, dzc::ErrorDomain::Internal, 1U);
    assert(chunk.state() == expectedState);
    assert(chunk.hasCpuData() == expectedCpuData);
}

void testCreationAndMetadataValidation() {
    auto created = dzc::Chunk::create(validMetadata());
    assert(created.hasValue());
    const dzc::Chunk& chunk = created.value();
    assert(chunk.metadata().id == dzc::ChunkId{41U});
    assert(chunk.metadata().pointCount == 2U);
    assert((chunk.metadata().origin == glm::dvec3{11.0, 21.0, 31.0}));
    assert(chunk.state() == dzc::ChunkState::MetadataOnly);
    assert(!chunk.hasCpuData());
    assert(chunk.cpuData() == nullptr);

    auto noPosition = validMetadata();
    noPosition.schema.mask = kColorMask;
    assertCreateError(dzc::Chunk::create(noPosition));

    auto zeroPoints = validMetadata();
    zeroPoints.pointCount = 0U;
    assertCreateError(dzc::Chunk::create(zeroPoints));

    auto invalidBounds = validMetadata();
    invalidBounds.bounds = dzc::Bounds3d{};
    assertCreateError(dzc::Chunk::create(invalidBounds));

    auto nanOrigin = validMetadata();
    nanOrigin.origin.x = std::numeric_limits<double>::quiet_NaN();
    assertCreateError(dzc::Chunk::create(nanOrigin));

    auto infOrigin = validMetadata();
    infOrigin.origin.y = std::numeric_limits<double>::infinity();
    assertCreateError(dzc::Chunk::create(infOrigin));

    auto unknownSchema = dzc::Chunk::create(validMetadata(kPositionMask | kUnknownMask));
    assert(unknownSchema.hasValue());
}

void testCompleteLifecycleAndCpuRetention() {
    dzc::Chunk chunk = createChunk(kPositionMask | kColorMask | kIntensityMask);
    const dzc::ChunkId stableId = chunk.metadata().id;

    loadCpuData(chunk, kPositionMask | kColorMask | kIntensityMask);
    const dzc::ChunkCpuData* original = chunk.cpuData();
    assert(original != nullptr);
    const glm::vec3 firstPosition = original->positions.front();
    const std::uint32_t firstColor = original->colorsRgba8.front();
    const std::uint16_t firstIntensity = original->intensities.front();

    assert(chunk.queueUpload().hasValue());
    assert(chunk.state() == dzc::ChunkState::UploadQueued);
    assert(chunk.completeUpload().hasValue());
    assert(chunk.state() == dzc::ChunkState::GpuResident);
    assert(chunk.hasCpuData());
    assert(chunk.cpuData()->positions.front() == firstPosition);
    assert(chunk.cpuData()->colorsRgba8.front() == firstColor);
    assert(chunk.cpuData()->intensities.front() == firstIntensity);

    assert(chunk.beginGpuEviction().hasValue());
    assert(chunk.state() == dzc::ChunkState::EvictPending);
    assert(chunk.completeGpuEviction().hasValue());
    assert(chunk.state() == dzc::ChunkState::CpuResident);
    assert(chunk.hasCpuData());

    assert(chunk.beginCpuEviction().hasValue());
    assert(chunk.state() == dzc::ChunkState::EvictPending);
    assert(chunk.completeCpuEviction().hasValue());
    assert(chunk.state() == dzc::ChunkState::MetadataOnly);
    assert(!chunk.hasCpuData());
    assert(chunk.cpuData() == nullptr);
    assert(chunk.metadata().id == stableId);
}

void testRecoverablePaths() {
    dzc::Chunk chunk = createChunk();
    assert(chunk.beginCpuLoad().hasValue());
    assert(chunk.cancelCpuLoad().hasValue());
    assert(chunk.state() == dzc::ChunkState::MetadataOnly);

    loadCpuData(chunk);
    assert(chunk.queueUpload().hasValue());
    assert(chunk.cancelUpload().hasValue());
    assert(chunk.state() == dzc::ChunkState::CpuResident);
    assert(chunk.queueUpload().hasValue());
    assert(chunk.failUpload().hasValue());
    assert(chunk.state() == dzc::ChunkState::CpuResident);
    assert(chunk.hasCpuData());

    assert(chunk.beginCpuEviction().hasValue());
    assert(chunk.completeCpuEviction().hasValue());
    assert(chunk.beginCpuLoad().hasValue());
    assert(chunk.failCpuLoad().hasValue());
    assert(chunk.state() == dzc::ChunkState::Error);
    assert(!chunk.hasCpuData());
    assert(chunk.resetError().hasValue());
    assert(chunk.state() == dzc::ChunkState::MetadataOnly);
}

void testInvalidOperationsDoNotMutateChunk() {
    dzc::Chunk chunk = createChunk();
    assertInvalidStateKeepsChunk(chunk, chunk.cancelCpuLoad(), dzc::ChunkState::MetadataOnly, false);
    assertInvalidStateKeepsChunk(chunk, chunk.queueUpload(), dzc::ChunkState::MetadataOnly, false);
    assertInvalidStateKeepsChunk(chunk, chunk.resetError(), dzc::ChunkState::MetadataOnly, false);

    assert(chunk.beginCpuLoad().hasValue());
    assertInvalidStateKeepsChunk(chunk, chunk.completeUpload(), dzc::ChunkState::LoadingCpu, false);
    assertInvalidStateKeepsChunk(chunk, chunk.completeCpuEviction(), dzc::ChunkState::LoadingCpu, false);

    assert(chunk.completeCpuLoad(validCpuData()).hasValue());
    assertInvalidStateKeepsChunk(chunk, chunk.completeGpuEviction(), dzc::ChunkState::CpuResident, true);

    assert(chunk.queueUpload().hasValue());
    assertInvalidStateKeepsChunk(chunk, chunk.beginCpuEviction(), dzc::ChunkState::UploadQueued, true);

    assert(chunk.completeUpload().hasValue());
    assert(chunk.beginGpuEviction().hasValue());
    assertInvalidStateKeepsChunk(chunk, chunk.completeCpuEviction(), dzc::ChunkState::EvictPending, true);
    assert(chunk.completeGpuEviction().hasValue());

    assert(chunk.beginCpuEviction().hasValue());
    assertInvalidStateKeepsChunk(chunk, chunk.completeGpuEviction(), dzc::ChunkState::EvictPending, true);
}

void testCpuDataValidationAndErrorState() {
    const std::uint32_t allAttributes = kPositionMask | kColorMask | kIntensityMask;

    auto assertCorruptLoad = [allAttributes](dzc::ChunkCpuData data) {
        dzc::Chunk chunk = createChunk(allAttributes);
        assert(chunk.beginCpuLoad().hasValue());
        const auto result = chunk.completeCpuLoad(std::move(data));
        assertError(result, dzc::ErrorDomain::DataFormat, 2U);
        assert(chunk.state() == dzc::ChunkState::Error);
        assert(!chunk.hasCpuData());
        assert(chunk.resetError().hasValue());
        assert(chunk.state() == dzc::ChunkState::MetadataOnly);
    };

    auto shortPositions = validCpuData(allAttributes);
    shortPositions.positions.pop_back();
    assertCorruptLoad(std::move(shortPositions));

    auto longPositions = validCpuData(allAttributes);
    longPositions.positions.push_back(glm::vec3{0.0F});
    assertCorruptLoad(std::move(longPositions));

    auto shortColors = validCpuData(allAttributes);
    shortColors.colorsRgba8.pop_back();
    assertCorruptLoad(std::move(shortColors));

    auto longColors = validCpuData(allAttributes);
    longColors.colorsRgba8.push_back(0U);
    assertCorruptLoad(std::move(longColors));

    auto shortIntensities = validCpuData(allAttributes);
    shortIntensities.intensities.pop_back();
    assertCorruptLoad(std::move(shortIntensities));

    auto longIntensities = validCpuData(allAttributes);
    longIntensities.intensities.push_back(0U);
    assertCorruptLoad(std::move(longIntensities));

    dzc::Chunk colorUndeclared = createChunk(kPositionMask);
    assert(colorUndeclared.beginCpuLoad().hasValue());
    auto dataWithColor = validCpuData(kPositionMask);
    dataWithColor.colorsRgba8 = {0x11223344U, 0x55667788U};
    assertError(colorUndeclared.completeCpuLoad(std::move(dataWithColor)), dzc::ErrorDomain::DataFormat, 2U);
    assert(colorUndeclared.state() == dzc::ChunkState::Error);

    dzc::Chunk intensityUndeclared = createChunk(kPositionMask);
    assert(intensityUndeclared.beginCpuLoad().hasValue());
    auto dataWithIntensity = validCpuData(kPositionMask);
    dataWithIntensity.intensities = {1U, 2U};
    assertError(intensityUndeclared.completeCpuLoad(std::move(dataWithIntensity)), dzc::ErrorDomain::DataFormat, 2U);
    assert(intensityUndeclared.state() == dzc::ChunkState::Error);

    dzc::Chunk unknownSchema = createChunk(kPositionMask | kUnknownMask);
    loadCpuData(unknownSchema, kPositionMask | kUnknownMask);

    dzc::Chunk nonFiniteLocalPositions = createChunk();
    auto nonFiniteData = validCpuData();
    nonFiniteData.positions[0].x = std::numeric_limits<float>::quiet_NaN();
    assert(nonFiniteLocalPositions.beginCpuLoad().hasValue());
    assert(nonFiniteLocalPositions.completeCpuLoad(std::move(nonFiniteData)).hasValue());
}

} // namespace

int main() {
    testCreationAndMetadataValidation();
    testCompleteLifecycleAndCpuRetention();
    testRecoverablePaths();
    testInvalidOperationsDoNotMutateChunk();
    testCpuDataValidationAndErrorState();
    return 0;
}
