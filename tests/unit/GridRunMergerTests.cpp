#include "data/chunk/GridRunFile.h"
#include "data/chunk/GridRunMerger.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
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

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto timestamp = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
            ("dzc_grid_run_merger_tests_" + std::to_string(timestamp));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code errorCode;
        std::filesystem::remove_all(m_path, errorCode);
    }

    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

dzc::GridBucket makeBucket(
    dzc::GridCellKey key,
    std::vector<std::uint64_t> sourceIndices,
    std::vector<glm::dvec3> positions,
    std::vector<std::uint32_t> colors = {},
    std::vector<std::uint16_t> intensities = {}) {
    dzc::GridBucket bucket;
    bucket.key = key;
    bucket.points.schema.mask = positionMask;
    if (!colors.empty()) {
        bucket.points.schema.mask |= colorMask;
        bucket.points.colorsRgba8 = std::move(colors);
    }
    if (!intensities.empty()) {
        bucket.points.schema.mask |= intensityMask;
        bucket.points.intensities = std::move(intensities);
    }
    bucket.points.positions = std::move(positions);
    bucket.sourceIndices = std::move(sourceIndices);
    return bucket;
}

void assertSameBuckets(
    const std::vector<dzc::GridBucket>& left,
    const std::vector<dzc::GridBucket>& right) {
    assert(left.size() == right.size());
    for (std::size_t bucketIndex = 0U; bucketIndex < left.size(); ++bucketIndex) {
        const auto& a = left[bucketIndex];
        const auto& b = right[bucketIndex];
        assert(a.key == b.key);
        assert(a.points.schema.mask == b.points.schema.mask);
        assert(a.points.positions.size() == b.points.positions.size());
        for (std::size_t pointIndex = 0U; pointIndex < a.points.positions.size(); ++pointIndex) {
            assert(a.points.positions[pointIndex] == b.points.positions[pointIndex]);
        }
        assert(a.points.colorsRgba8 == b.points.colorsRgba8);
        assert(a.points.intensities == b.points.intensities);
        assert(a.sourceIndices == b.sourceIndices);
    }
}

void testSingleCellMergeSortsBySourceIndexAndPreservesAttributes() {
    const dzc::GridCellKey key{1, -2, 3};
    std::vector<std::vector<dzc::GridBucket>> inputs{
        {makeBucket(key, {4U, 8U}, {{4.0, 0.0, 0.0}, {8.0, 0.0, 0.0}}, {40U, 80U}, {400U, 800U})},
        {makeBucket(key, {1U, 6U}, {{1.0, 0.0, 0.0}, {6.0, 0.0, 0.0}}, {10U, 60U}, {100U, 600U})}};

    const auto merged = dzc::GridRunMerger::merge(inputs);
    assert(merged.hasValue());
    assert(merged.value().size() == 1U);
    const auto& bucket = merged.value()[0];
    assert(bucket.key == key);
    assert(bucket.points.schema.mask == (positionMask | colorMask | intensityMask));
    const std::vector<std::uint64_t> expectedSources{1U, 4U, 6U, 8U};
    const std::vector<std::uint32_t> expectedColors{10U, 40U, 60U, 80U};
    const std::vector<std::uint16_t> expectedIntensities{100U, 400U, 600U, 800U};
    assert(bucket.sourceIndices == expectedSources);
    assert(bucket.points.positions[0] == glm::dvec3(1.0, 0.0, 0.0));
    assert(bucket.points.positions[1] == glm::dvec3(4.0, 0.0, 0.0));
    assert(bucket.points.colorsRgba8 == expectedColors);
    assert(bucket.points.intensities == expectedIntensities);
}

void testCellOrderAndInputDistributionDeterminism() {
    const dzc::GridBucket low = makeBucket(
        dzc::GridCellKey{-1, 0, 2}, {0U}, {{0.0, 0.0, 0.0}});
    const dzc::GridBucket high = makeBucket(
        dzc::GridCellKey{2, -1, 0}, {2U}, {{2.0, 0.0, 0.0}});
    const dzc::GridBucket middlePartA = makeBucket(
        dzc::GridCellKey{0, 0, 0}, {1U}, {{1.0, 0.0, 0.0}});
    const dzc::GridBucket middlePartB = makeBucket(
        dzc::GridCellKey{0, 0, 0}, {3U}, {{3.0, 0.0, 0.0}});

    const std::vector<std::vector<dzc::GridBucket>> first{{high}, {low, middlePartA}, {middlePartB}};
    const std::vector<std::vector<dzc::GridBucket>> second{{middlePartB}, {middlePartA, high}, {low}};
    const auto firstResult = dzc::GridRunMerger::merge(first);
    const auto secondResult = dzc::GridRunMerger::merge(second);
    assert(firstResult.hasValue());
    assert(secondResult.hasValue());
    assertSameBuckets(firstResult.value(), secondResult.value());
    assert(firstResult.value().size() == 3U);
    assert(firstResult.value()[0].key == low.key);
    assert(firstResult.value()[1].key == middlePartA.key);
    assert(firstResult.value()[2].key == high.key);
    const std::vector<std::uint64_t> expectedSources{1U, 3U};
    assert(firstResult.value()[1].sourceIndices == expectedSources);
}

void testEmptyInputsAndRepeatability() {
    const std::vector<std::vector<dzc::GridBucket>> inputs{{}, {}};
    const auto empty = dzc::GridRunMerger::merge(inputs);
    assert(empty.hasValue());
    assert(empty.value().empty());

    const std::vector<std::vector<dzc::GridBucket>> nonEmpty{{makeBucket(
        dzc::GridCellKey{0, 0, 0}, {5U}, {{5.0, 0.0, 0.0}})}};
    const auto first = dzc::GridRunMerger::merge(nonEmpty);
    const auto second = dzc::GridRunMerger::merge(nonEmpty);
    assert(first.hasValue());
    assert(second.hasValue());
    assertSameBuckets(first.value(), second.value());
}

void testInvalidInputsAreRejected() {
    const dzc::GridBucket valid = makeBucket(
        dzc::GridCellKey{0, 0, 0}, {1U}, {{1.0, 0.0, 0.0}});

    auto invalidOrder = std::vector<dzc::GridBucket>{
        makeBucket(dzc::GridCellKey{1, 0, 0}, {1U}, {{1.0, 0.0, 0.0}}),
        makeBucket(dzc::GridCellKey{0, 0, 0}, {2U}, {{2.0, 0.0, 0.0}})};
    auto result = dzc::GridRunMerger::merge({invalidOrder});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto schemaMismatch = valid;
    schemaMismatch.points.schema.mask |= colorMask;
    schemaMismatch.points.colorsRgba8 = {2U};
    result = dzc::GridRunMerger::merge({{valid}, {schemaMismatch}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto duplicate = makeBucket(
        valid.key, {1U}, {{2.0, 0.0, 0.0}});
    result = dzc::GridRunMerger::merge({{valid}, {duplicate}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto nonFinite = valid;
    nonFinite.points.positions[0].x = std::numeric_limits<double>::quiet_NaN();
    result = dzc::GridRunMerger::merge({{nonFinite}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);

    auto badLength = valid;
    badLength.sourceIndices.clear();
    result = dzc::GridRunMerger::merge({{badLength}});
    assert(!result.hasValue());
    assertError(result.error(), dzc::ErrorDomain::DataFormat, 2U);
}

void testCancellationAndRunRoundTrip() {
    dzc::tasks::CancellationSource source;
    assert(source.requestCancellation());
    const auto cancelled = dzc::GridRunMerger::merge(
        {{makeBucket(dzc::GridCellKey{0, 0, 0}, {0U}, {{0.0, 0.0, 0.0}})}},
        source.token());
    assert(!cancelled.hasValue());
    assertError(cancelled.error(), dzc::ErrorDomain::Task, 7U);

    TemporaryDirectory directory;
    auto runResult = dzc::GridRunFile::create(directory.path());
    assert(runResult.hasValue());
    auto run = std::move(runResult.value());
    const std::vector<dzc::GridBucket> expected{
        makeBucket(dzc::GridCellKey{0, 0, 0}, {0U}, {{0.0, 0.0, 0.0}}),
        makeBucket(dzc::GridCellKey{1, 0, 0}, {1U}, {{1.0, 0.0, 0.0}})};
    assert(run.write(expected).hasValue());
    assert(run.complete().hasValue());
    const auto read = run.read();
    assert(read.hasValue());
    const auto merged = dzc::GridRunMerger::merge({read.value()});
    assert(merged.hasValue());
    assertSameBuckets(merged.value(), expected);

    auto outputRunResult = dzc::GridRunFile::create(directory.path());
    assert(outputRunResult.hasValue());
    auto outputRun = std::move(outputRunResult.value());
    assert(outputRun.write(merged.value()).hasValue());
    assert(outputRun.complete().hasValue());
    const auto outputRead = outputRun.read();
    assert(outputRead.hasValue());
    assertSameBuckets(outputRead.value(), merged.value());
}

} // namespace

int main() {
    testSingleCellMergeSortsBySourceIndexAndPreservesAttributes();
    testCellOrderAndInputDistributionDeterminism();
    testEmptyInputsAndRepeatability();
    testInvalidInputsAreRejected();
    testCancellationAndRunRoundTrip();
    return 0;
}