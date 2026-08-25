#include "data/chunk/GridChunkBuilder.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t positionMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Position);
constexpr std::uint32_t colorMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Color);
constexpr std::uint32_t intensityMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Intensity);

void assertError(const dzc::Error& error, dzc::ErrorDomain domain, std::uint32_t code) {
    assert(error.domain == domain);
    assert(error.code == code);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

dzc::AttributeSchema schemaFor(bool color, bool intensity) {
    dzc::AttributeSchema schema;
    schema.mask = positionMask;
    if (color) {
        schema.mask |= colorMask;
    }
    if (intensity) {
        schema.mask |= intensityMask;
    }
    return schema;
}

dzc::GridBucket makeBucket(
    dzc::GridCellKey key,
    std::vector<glm::dvec3> positions,
    std::vector<std::uint64_t> sourceIndices,
    bool color = false,
    bool intensity = false) {
    dzc::GridBucket bucket;
    bucket.key = key;
    bucket.points.schema = schemaFor(color, intensity);
    bucket.points.positions = std::move(positions);
    bucket.sourceIndices = std::move(sourceIndices);
    if (color) {
        for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
            bucket.points.colorsRgba8.push_back(
                static_cast<std::uint32_t>(100U + index));
        }
    }
    if (intensity) {
        for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
            bucket.points.intensities.push_back(
                static_cast<std::uint16_t>(200U + index));
        }
    }
    return bucket;
}

std::uint64_t expectedChunkId(dzc::GridCellKey key, std::uint64_t subchunkIndex) {
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offsetBasis;
    const auto mix = [&hash, prime](std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            hash ^= value & 0xffU;
            hash *= prime;
            value >>= 8U;
        }
    };
    mix(static_cast<std::uint64_t>(key.x));
    mix(static_cast<std::uint64_t>(key.y));
    mix(static_cast<std::uint64_t>(key.z));
    mix(subchunkIndex);
    return hash;
}

void testEmptyInputsAndIndependentData() {
    dzc::GridBucket empty;
    empty.key = dzc::GridCellKey{9, 9, 9};
    empty.points.schema.mask = 0U;

    const auto result = dzc::GridChunkBuilder::build({{}, {empty}, {}});
    assert(result.hasValue());
    assert(result.value().empty());

    const dzc::GridCellKey key{2, -1, 4};
    const auto built = dzc::GridChunkBuilder::build({{
        makeBucket(key, {{-2.0, 0.0, 4.0}, {2.0, 4.0, 8.0}}, {7U, 8U}, true, true)}});
    assert(built.hasValue());
    assert(built.value().size() == 1U);
    const dzc::Chunk& chunk = built.value().front();
    assert(chunk.state() == dzc::ChunkState::CpuResident);
    assert(chunk.hasCpuData());
    assert(chunk.cpuData() != nullptr);
    assert(chunk.metadata().id.value == expectedChunkId(key, 0U));
    assert(chunk.metadata().pointCount == 2U);
    assert(chunk.metadata().schema.mask == (positionMask | colorMask | intensityMask));
    assert(chunk.metadata().bounds.minimum == (glm::dvec3{-2.0, 0.0, 4.0}));
    assert(chunk.metadata().bounds.maximum == (glm::dvec3{2.0, 4.0, 8.0}));
    assert(chunk.metadata().origin == (glm::dvec3{0.0, 2.0, 6.0}));
    assert(chunk.cpuData()->positions.size() == 2U);
    assert(chunk.cpuData()->positions[0] == (glm::vec3{-2.0F, -2.0F, -2.0F}));
    assert(chunk.cpuData()->positions[1] == (glm::vec3{2.0F, 2.0F, 2.0F}));
    assert(chunk.cpuData()->colorsRgba8 == std::vector<std::uint32_t>({100U, 101U}));
    assert(chunk.cpuData()->intensities == std::vector<std::uint16_t>({200U, 201U}));
}

void testOrderingAndStableSubchunkIds() {
    const dzc::GridCellKey low{-1, 5, 0};
    const dzc::GridCellKey high{1, -3, 2};
    const dzc::GridCellKey middle{0, 0, 0};
    const dzc::GridBucket lowBucket = makeBucket(low, {{-1.0, 0.0, 0.0}}, {1U});
    const dzc::GridBucket highBucket = makeBucket(high, {{10.0, 0.0, 0.0}}, {3U});
    const dzc::GridBucket firstPart = makeBucket(middle, {{2.0, 0.0, 0.0}}, {2U});
    const dzc::GridBucket secondPart = makeBucket(middle, {{4.0, 0.0, 0.0}}, {4U});

    const auto first = dzc::GridChunkBuilder::build({
        {highBucket}, {secondPart, firstPart}, {lowBucket}});
    const auto second = dzc::GridChunkBuilder::build({
        {lowBucket}, {secondPart, firstPart}, {highBucket}});
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value().size() == 4U);
    assert(second.value().size() == 4U);

    for (std::size_t index = 0U; index < first.value().size(); ++index) {
        assert(first.value()[index].metadata().id == second.value()[index].metadata().id);
        assert(first.value()[index].metadata().bounds.minimum ==
               second.value()[index].metadata().bounds.minimum);
        assert(first.value()[index].metadata().origin ==
               second.value()[index].metadata().origin);
    }
    assert(first.value()[0].metadata().id.value == expectedChunkId(low, 0U));
    assert(first.value()[1].metadata().id.value == expectedChunkId(middle, 0U));
    assert(first.value()[2].metadata().id.value == expectedChunkId(middle, 1U));
    assert(first.value()[3].metadata().id.value == expectedChunkId(high, 0U));
    assert(first.value()[1].cpuData()->positions[0].x == 0.0F);
    assert(first.value()[2].cpuData()->positions[0].x == 0.0F);
}

void testAllSupportedSchemas() {
    const dzc::GridCellKey key{0, 0, 0};
    for (const auto [color, intensity] : {
             std::pair<bool, bool>{false, false},
             std::pair<bool, bool>{true, false},
             std::pair<bool, bool>{false, true},
             std::pair<bool, bool>{true, true}}) {
        const auto result = dzc::GridChunkBuilder::build({{
            makeBucket(key, {{1.0, 2.0, 3.0}}, {0U}, color, intensity)}});
        assert(result.hasValue());
        assert(result.value().size() == 1U);
        const dzc::ChunkCpuData* data = result.value()[0].cpuData();
        assert(data != nullptr);
        assert(data->positions.size() == 1U);
        assert(data->colorsRgba8.size() == (color ? 1U : 0U));
        assert(data->intensities.size() == (intensity ? 1U : 0U));
    }
}

void testInvalidInputs() {
    const dzc::GridCellKey key{0, 0, 0};
    const dzc::GridBucket valid = makeBucket(key, {{0.0, 0.0, 0.0}}, {1U});

    auto invalid = valid;
    invalid.points.schema.mask = colorMask;
    auto result = dzc::GridChunkBuilder::build({{invalid}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    invalid = valid;
    invalid.points.positions.clear();
    invalid.sourceIndices = {1U};
    result = dzc::GridChunkBuilder::build({{invalid}});
    assert(result.hasValue());
    assert(result.value().empty());

    invalid = valid;
    invalid.points.colorsRgba8 = {1U};
    invalid.points.schema.mask |= colorMask;
    invalid.points.colorsRgba8.clear();
    result = dzc::GridChunkBuilder::build({{invalid}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    invalid = valid;
    invalid.sourceIndices = {2U, 1U};
    invalid.points.positions.push_back({1.0, 1.0, 1.0});
    result = dzc::GridChunkBuilder::build({{invalid}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    invalid = valid;
    invalid.points.positions[0].x = std::numeric_limits<double>::quiet_NaN();
    result = dzc::GridChunkBuilder::build({{invalid}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    const auto duplicateCell = dzc::GridChunkBuilder::build({{valid}, {valid}});
    assert(!duplicateCell.hasValue());
    assertError(duplicateCell.error(), dzc::ErrorDomain::DataFormat, 2U);

    const auto inconsistentKeys = dzc::GridChunkBuilder::build({{
        valid, makeBucket(dzc::GridCellKey{1, 0, 0}, {{1.0, 0.0, 0.0}}, {2U})}});
    assert(!inconsistentKeys.hasValue());
    assertError(inconsistentKeys.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto schemaMismatch = valid;
    schemaMismatch.points.schema.mask |= colorMask;
    schemaMismatch.points.colorsRgba8 = {5U};
    const auto mismatchedSchema = dzc::GridChunkBuilder::build({{valid}, {schemaMismatch}});
    assert(!mismatchedSchema.hasValue());
    assertError(mismatchedSchema.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto duplicateSource = makeBucket(key, {{2.0, 0.0, 0.0}}, {1U});
    const auto duplicateAcrossParts = dzc::GridChunkBuilder::build({{valid, duplicateSource}});
    assert(!duplicateAcrossParts.hasValue());
    assertError(duplicateAcrossParts.error(), dzc::ErrorDomain::DataFormat, 2U);
}

void testCancellationAndRepeatability() {
    dzc::tasks::CancellationSource cancellation;
    assert(cancellation.requestCancellation());
    const auto cancelled = dzc::GridChunkBuilder::build(
        {{makeBucket(dzc::GridCellKey{0, 0, 0}, {{0.0, 0.0, 0.0}}, {0U})}},
        cancellation.token());
    assert(!cancelled.hasValue());
    assertError(cancelled.error(), dzc::ErrorDomain::Task, 7U);

    const dzc::GridBucket bucket = makeBucket(
        dzc::GridCellKey{4, -2, 7}, {{-10.0, 1.0, 3.0}, {20.0, 5.0, 9.0}}, {0U, 1U}, true, true);
    const auto first = dzc::GridChunkBuilder::build({{bucket}});
    const auto second = dzc::GridChunkBuilder::build({{bucket}});
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value().size() == second.value().size());
    assert(first.value()[0].metadata().id == second.value()[0].metadata().id);
    assert(first.value()[0].metadata().bounds.minimum ==
           second.value()[0].metadata().bounds.minimum);
    assert(first.value()[0].metadata().bounds.maximum ==
           second.value()[0].metadata().bounds.maximum);
    assert(first.value()[0].metadata().origin == second.value()[0].metadata().origin);
    assert(first.value()[0].cpuData()->positions == second.value()[0].cpuData()->positions);
}

} // namespace

int main() {
    testEmptyInputsAndIndependentData();
    testOrderingAndStableSubchunkIds();
    testAllSupportedSchemas();
    testInvalidInputs();
    testCancellationAndRepeatability();
    return 0;
}
