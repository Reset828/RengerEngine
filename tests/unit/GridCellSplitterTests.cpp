#include "data/chunk/GridCellSplitter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
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

dzc::GridBucket makeBucket(std::size_t count, bool withColor = true, bool withIntensity = true) {
    dzc::GridBucket bucket;
    bucket.key = dzc::GridCellKey{4, -2, 7};
    bucket.points.schema.mask = positionMask;
    if (withColor) {
        bucket.points.schema.mask |= colorMask;
    }
    if (withIntensity) {
        bucket.points.schema.mask |= intensityMask;
    }
    bucket.points.positions.reserve(count);
    if (withColor) {
        bucket.points.colorsRgba8.reserve(count);
    }
    if (withIntensity) {
        bucket.points.intensities.reserve(count);
    }
    bucket.sourceIndices.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        bucket.points.positions.emplace_back(
            static_cast<double>(index),
            static_cast<double>((index * 3U) % 17U),
            static_cast<double>((index * 5U) % 19U));
        if (withColor) {
            bucket.points.colorsRgba8.push_back(static_cast<std::uint32_t>(index));
        }
        if (withIntensity) {
            bucket.points.intensities.push_back(static_cast<std::uint16_t>(index % 65536U));
        }
        bucket.sourceIndices.push_back(static_cast<std::uint64_t>(index));
    }
    return bucket;
}

void assertIndependentEqual(
    const dzc::GridBucket& expected,
    const dzc::GridBucket& actual) {
    assert(expected.key == actual.key);
    assert(expected.points.schema.mask == actual.points.schema.mask);
    assert(expected.points.positions == actual.points.positions);
    assert(expected.points.colorsRgba8 == actual.points.colorsRgba8);
    assert(expected.points.intensities == actual.points.intensities);
    assert(expected.sourceIndices == actual.sourceIndices);
}

void assertPartitionInvariants(
    const dzc::GridBucket& source,
    const std::vector<dzc::GridBucket>& parts) {
    std::size_t total = 0U;
    std::vector<std::uint64_t> combinedSources;
    combinedSources.reserve(source.sourceIndices.size());
    for (const dzc::GridBucket& part : parts) {
        assert(!part.points.positions.empty());
        assert(part.points.positions.size() <= 262144U);
        assert(part.points.positions.size() <= 524288U);
        assert(part.key == source.key);
        assert(part.points.schema.mask == source.points.schema.mask);
        assert(part.points.positions.size() == part.sourceIndices.size());
        if (source.points.schema.hasColor()) {
            assert(part.points.positions.size() == part.points.colorsRgba8.size());
        } else {
            assert(part.points.colorsRgba8.empty());
        }
        if (source.points.schema.hasIntensity()) {
            assert(part.points.positions.size() == part.points.intensities.size());
        } else {
            assert(part.points.intensities.empty());
        }
        for (std::size_t index = 1U; index < part.sourceIndices.size(); ++index) {
            assert(part.sourceIndices[index - 1U] < part.sourceIndices[index]);
        }
        combinedSources.insert(
            combinedSources.end(), part.sourceIndices.begin(), part.sourceIndices.end());
        total += part.points.positions.size();
    }
    assert(total == source.points.positions.size());
    std::sort(combinedSources.begin(), combinedSources.end());
    assert(combinedSources == source.sourceIndices);
}
void testEmptyAndBoundaries() {
    const dzc::GridBucket empty = makeBucket(0U);
    const auto emptyResult = dzc::GridCellSplitter::split(empty);
    assert(emptyResult.hasValue());
    assert(emptyResult.value().empty());

    for (const std::size_t count : {1U, 262144U, 524288U}) {
        const dzc::GridBucket source = makeBucket(count);
        auto result = dzc::GridCellSplitter::split(source);
        assert(result.hasValue());
        assert(result.value().size() == 1U);
        assertIndependentEqual(source, result.value()[0]);
        if (count > 0U) {
            result.value()[0].points.positions[0].x = -100.0;
            assert(source.points.positions[0].x != -100.0);
        }
    }
}

void testDeterministicOversizedSplitAndAttributes() {
    const dzc::GridBucket source = makeBucket(524289U);
    const auto first = dzc::GridCellSplitter::split(source);
    const auto second = dzc::GridCellSplitter::split(source);
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value().size() == 3U);
    assert(second.value().size() == first.value().size());
    for (std::size_t partIndex = 0U; partIndex < first.value().size(); ++partIndex) {
        assertIndependentEqual(first.value()[partIndex], second.value()[partIndex]);
        assert(first.value()[partIndex].points.positions.size() <= 262144U);
        assert(first.value()[partIndex].points.positions.size() ==
               first.value()[partIndex].points.colorsRgba8.size());
        assert(first.value()[partIndex].points.positions.size() ==
               first.value()[partIndex].points.intensities.size());
        for (std::size_t index = 0U; index < first.value()[partIndex].sourceIndices.size(); ++index) {
            const std::size_t sourceIndex = static_cast<std::size_t>(
                first.value()[partIndex].sourceIndices[index]);
            assert(first.value()[partIndex].points.colorsRgba8[index] == sourceIndex);
            assert(first.value()[partIndex].points.intensities[index] ==
                   sourceIndex % 65536U);
        }
    }
    assertPartitionInvariants(source, first.value());
}

void testStableRankWithRepeatedCoordinates() {
    dzc::GridBucket source = makeBucket(524289U, false, false);
    for (glm::dvec3& position : source.points.positions) {
        position = glm::dvec3{1.0, 1.0, 1.0};
    }
    const auto result = dzc::GridCellSplitter::split(source);
    assert(result.hasValue());
    assert(result.value().size() == 3U);
    assertPartitionInvariants(source, result.value());
    assert(result.value()[0].sourceIndices.front() == 0U);
    assert(result.value()[1].sourceIndices.front() == result.value()[0].sourceIndices.back() + 1U);
}

void testAxisTieUsesXFirstAndInvalidInput() {
    dzc::GridBucket source = makeBucket(524289U, false, false);
    for (std::size_t index = 0U; index < source.points.positions.size(); ++index) {
        source.points.positions[index] = glm::dvec3{
            static_cast<double>(index % 1024U),
            static_cast<double>(index % 1024U),
            0.0};
    }
    const auto result = dzc::GridCellSplitter::split(source);
    assert(result.hasValue());
    assertPartitionInvariants(source, result.value());

    dzc::GridBucket invalid = makeBucket(1U);
    invalid.points.schema.mask = colorMask;
    auto failed = dzc::GridCellSplitter::split(invalid);
    assert(!failed.hasValue());
    assertError(failed.error(), dzc::ErrorDomain::DataFormat, 2U);

    invalid = makeBucket(1U);
    invalid.sourceIndices.clear();
    failed = dzc::GridCellSplitter::split(invalid);
    assert(!failed.hasValue());
    assertError(failed.error(), dzc::ErrorDomain::DataFormat, 2U);

    invalid = makeBucket(1U);
    invalid.points.positions[0].x = std::numeric_limits<double>::quiet_NaN();
    failed = dzc::GridCellSplitter::split(invalid);
    assert(!failed.hasValue());
    assertError(failed.error(), dzc::ErrorDomain::DataFormat, 2U);
}

void testCoordinateRangeOverflowFails() {
    dzc::GridBucket source = makeBucket(524289U, false, false);
    source.points.positions[0] = glm::dvec3{
        std::numeric_limits<double>::max(), 0.0, 0.0};
    source.points.positions[1] = glm::dvec3{
        -std::numeric_limits<double>::max(), 0.0, 0.0};
    const auto result = dzc::GridCellSplitter::split(source);
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);
}
void testCancellation() {
    dzc::tasks::CancellationSource cancellation;
    cancellation.requestCancellation();
    const auto result = dzc::GridCellSplitter::split(makeBucket(1U), cancellation.token());
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::Task, 7U);
}

void testTypeProperties() {
    static_assert(std::is_copy_constructible_v<dzc::GridCellSplitter>);
    static_assert(std::is_copy_assignable_v<dzc::GridCellSplitter>);
    static_assert(std::is_copy_constructible_v<dzc::GridBucket>);
}

} // namespace

int main() {
    testEmptyAndBoundaries();
    testDeterministicOversizedSplitAndAttributes();
    testStableRankWithRepeatedCoordinates();
    testAxisTieUsesXFirstAndInvalidInput();
    testCoordinateRangeOverflowFails();
    testCancellation();
    testTypeProperties();
    return 0;
}