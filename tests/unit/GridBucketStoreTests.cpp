#include "data/chunk/GridBucketStore.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t attributeMask(dzc::PointAttribute attribute) noexcept {
    return static_cast<std::uint32_t>(attribute);
}

constexpr std::uint32_t kPositionMask = attributeMask(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask = attributeMask(dzc::PointAttribute::Color);
constexpr std::uint32_t kIntensityMask = attributeMask(dzc::PointAttribute::Intensity);

void assertDataFormat(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

void assertBudgetError(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::Resource);
    assert(error.code == 1U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

dzc::GridBucketStore makeStore(std::uint64_t byteBudget = 4096U) {
    auto result = dzc::GridBucketStore::create(glm::dvec3{0.0}, 1.0, byteBudget);
    assert(result.hasValue());
    return std::move(result.value());
}

dzc::PointBatch makeBatch(
    std::uint32_t schemaMask,
    std::vector<glm::dvec3> positions,
    std::vector<std::uint32_t> colors = {},
    std::vector<std::uint16_t> intensities = {}) {
    dzc::PointBatch batch{};
    batch.schema.mask = schemaMask;
    batch.positions = std::move(positions);
    batch.colorsRgba8 = std::move(colors);
    batch.intensities = std::move(intensities);
    return batch;
}

void assertPosition(const glm::dvec3& actual, const glm::dvec3& expected) {
    assert(actual.x == expected.x);
    assert(actual.y == expected.y);
    assert(actual.z == expected.z);
}

void testValidConfigurationAndInvalidConfiguration() {
    const auto valid = dzc::GridBucketStore::create(glm::dvec3{1.0, 2.0, 3.0}, 0.5, 128U);
    assert(valid.hasValue());
    assert(valid.value().residentBytes() == 0U);
    assert(valid.value().pointCount() == 0U);

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const glm::dvec3 minimum : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{0.0, infinity, 0.0}}) {
        const auto result = dzc::GridBucketStore::create(minimum, 1.0, 128U);
        assert(!result.hasValue());
        assertDataFormat(result.error());
    }

    for (const double cellSize : {0.0, -1.0, nan, infinity}) {
        const auto result = dzc::GridBucketStore::create(glm::dvec3{0.0}, cellSize, 128U);
        assert(!result.hasValue());
        assertDataFormat(result.error());
    }

    const auto zeroBudget = dzc::GridBucketStore::create(glm::dvec3{0.0}, 1.0, 0U);
    assert(!zeroBudget.hasValue());
    assertBudgetError(zeroBudget.error());
}

void testSingleBatchBucketsAreSortedAndPreserveAttributes() {
    auto store = makeStore();
    const dzc::PointBatch batch = makeBatch(
        kPositionMask | kColorMask | kIntensityMask,
        {glm::dvec3{1.1, 0.0, 0.0}, glm::dvec3{0.1, 0.0, 0.0}, glm::dvec3{1.9, 0.0, 0.0}},
        {0x01020304U, 0x05060708U, 0x090A0B0CU},
        {11U, 22U, 33U});

    const auto appendResult = store.appendBatch(batch);
    assert(appendResult.hasValue());
    assert(store.pointCount() == 3U);
    assert(store.residentBytes() == 3U * (sizeof(glm::dvec3) + sizeof(std::uint32_t) + sizeof(std::uint16_t) + sizeof(std::uint64_t)));

    const auto buckets = store.snapshot();
    assert(buckets.size() == 2U);
    const dzc::GridCellKey firstKey{0, 0, 0};
    assert(buckets[0].key == firstKey);
    const dzc::GridCellKey secondKey{1, 0, 0};
    assert(buckets[1].key == secondKey);

    assert(buckets[0].points.schema.mask == batch.schema.mask);
    assert(buckets[0].points.positions.size() == 1U);
    assertPosition(buckets[0].points.positions[0], batch.positions[1]);
    assert(buckets[0].points.colorsRgba8[0] == batch.colorsRgba8[1]);
    assert(buckets[0].points.intensities[0] == batch.intensities[1]);
    const std::vector<std::uint64_t> firstIndices{1U};
    assert(buckets[0].sourceIndices == firstIndices);

    assert(buckets[1].points.positions.size() == 2U);
    assertPosition(buckets[1].points.positions[0], batch.positions[0]);
    assertPosition(buckets[1].points.positions[1], batch.positions[2]);
    assert(buckets[1].sourceIndices == std::vector<std::uint64_t>({0U, 2U}));
}

void testMultipleBatchesPreserveStableSourceOrder() {
    auto store = makeStore();
    assert(store.appendBatch(makeBatch(
        kPositionMask,
        {glm::dvec3{0.2, 0.0, 0.0}, glm::dvec3{2.2, 0.0, 0.0}})).hasValue());
    assert(store.appendBatch(makeBatch(
        kPositionMask,
        {glm::dvec3{0.3, 0.0, 0.0}, glm::dvec3{0.4, 0.0, 0.0}})).hasValue());

    const auto buckets = store.snapshot();
    assert(buckets.size() == 2U);
    const dzc::GridCellKey firstKey{0, 0, 0};
    assert(buckets[0].key == firstKey);
    assert(buckets[0].sourceIndices == std::vector<std::uint64_t>({0U, 2U, 3U}));
    const dzc::GridCellKey thirdKey{2, 0, 0};
    assert(buckets[1].key == thirdKey);
    const std::vector<std::uint64_t> secondIndices{1U};
    assert(buckets[1].sourceIndices == secondIndices);
}

void testEmptyBatchDoesNotFixSchemaOrConsumeSourceIndices() {
    auto store = makeStore();
    const auto empty = makeBatch(kPositionMask | kColorMask, {});
    assert(store.appendBatch(empty).hasValue());
    assert(store.snapshot().empty());

    const auto intensityBatch = makeBatch(kPositionMask | kIntensityMask, {glm::dvec3{0.2, 0.0, 0.0}}, {}, {7U});
    assert(store.appendBatch(intensityBatch).hasValue());
    const auto buckets = store.snapshot();
    assert(buckets.size() == 1U);
    assert(buckets[0].points.schema.mask == (kPositionMask | kIntensityMask));
    assert(buckets[0].sourceIndices == std::vector<std::uint64_t>{0U});
}

void testInvalidBatchAndSchemaMismatchAreAtomic() {
    auto store = makeStore();
    const auto valid = makeBatch(kPositionMask, {glm::dvec3{0.2, 0.0, 0.0}});
    assert(store.appendBatch(valid).hasValue());
    const auto before = store.snapshot();
    const auto beforeBytes = store.residentBytes();
    const auto beforePoints = store.pointCount();

    dzc::PointBatch missingPosition{};
    missingPosition.positions = {glm::dvec3{1.0, 0.0, 0.0}};
    const auto invalidResult = store.appendBatch(missingPosition);
    assert(!invalidResult.hasValue());
    assertDataFormat(invalidResult.error());

    const auto mismatch = makeBatch(kPositionMask | kColorMask, {glm::dvec3{1.2, 0.0, 0.0}}, {1U});
    const auto mismatchResult = store.appendBatch(mismatch);
    assert(!mismatchResult.hasValue());
    assertDataFormat(mismatchResult.error());

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto nonFinite = makeBatch(kPositionMask, {glm::dvec3{nan, 0.0, 0.0}});
    const auto nonFiniteResult = store.appendBatch(nonFinite);
    assert(!nonFiniteResult.hasValue());
    assertDataFormat(nonFiniteResult.error());

    assert(store.residentBytes() == beforeBytes);
    assert(store.pointCount() == beforePoints);
    assert(store.snapshot().size() == before.size());
    assert(store.snapshot()[0].sourceIndices == before[0].sourceIndices);
}

void testBudgetBoundaryRejectsWholeBatch() {
    constexpr std::uint64_t bytesPerPositionOnlyPoint = sizeof(glm::dvec3) + sizeof(std::uint64_t);
    auto store = makeStore(2U * bytesPerPositionOnlyPoint);
    const auto first = makeBatch(kPositionMask, {glm::dvec3{0.1, 0.0, 0.0}, glm::dvec3{0.2, 0.0, 0.0}});
    assert(store.appendBatch(first).hasValue());
    assert(store.residentBytes() == 2U * bytesPerPositionOnlyPoint);

    const auto before = store.snapshot();
    const auto rejected = makeBatch(kPositionMask, {glm::dvec3{0.3, 0.0, 0.0}});
    const auto result = store.appendBatch(rejected);
    assert(!result.hasValue());
    assertBudgetError(result.error());
    assert(store.residentBytes() == 2U * bytesPerPositionOnlyPoint);
    assert(store.pointCount() == 2U);
    assert(store.snapshot()[0].sourceIndices == before[0].sourceIndices);
}

void testSnapshotIsIndependentAndClearResetsState() {
    auto store = makeStore();
    assert(store.appendBatch(makeBatch(kPositionMask, {glm::dvec3{0.1, 0.0, 0.0}})).hasValue());
    auto first = store.snapshot();
    first[0].points.positions[0].x = 999.0;
    first[0].sourceIndices[0] = 999U;
    const auto second = store.snapshot();
    assert(second[0].points.positions[0].x == 0.1);
    assert(second[0].sourceIndices[0] == 0U);

    store.clear();
    assert(store.snapshot().empty());
    assert(store.residentBytes() == 0U);
    assert(store.pointCount() == 0U);
    assert(store.appendBatch(makeBatch(kPositionMask, {glm::dvec3{0.1, 0.0, 0.0}})).hasValue());
    assert(store.snapshot()[0].sourceIndices == std::vector<std::uint64_t>{0U});
}

void testMaximumBudgetAndDeterminism() {
    auto store = makeStore(std::numeric_limits<std::uint64_t>::max());
    const auto batch = makeBatch(kPositionMask, {glm::dvec3{0.1, 0.0, 0.0}, glm::dvec3{1.1, 0.0, 0.0}});
    assert(store.appendBatch(batch).hasValue());
    const auto first = store.snapshot();
    const auto second = store.snapshot();
    assert(first.size() == second.size());
    assert(first[0].key == second[0].key);
    assert(first[0].sourceIndices == second[0].sourceIndices);
    assert(first[1].key == second[1].key);
    assert(first[1].sourceIndices == second[1].sourceIndices);
}

void testTypeProperties() {
    static_assert(std::is_copy_constructible_v<dzc::GridBucket>);
    static_assert(std::is_copy_assignable_v<dzc::GridBucket>);
    static_assert(std::is_move_constructible_v<dzc::GridBucketStore>);
    static_assert(std::is_move_assignable_v<dzc::GridBucketStore>);
}

} // namespace

int main() {
    testValidConfigurationAndInvalidConfiguration();
    testSingleBatchBucketsAreSortedAndPreserveAttributes();
    testMultipleBatchesPreserveStableSourceOrder();
    testEmptyBatchDoesNotFixSchemaOrConsumeSourceIndices();
    testInvalidBatchAndSchemaMismatchAreAtomic();
    testBudgetBoundaryRejectsWholeBatch();
    testSnapshotIsIndependentAndClearResetsState();
    testMaximumBudgetAndDeterminism();
    testTypeProperties();
    return 0;
}