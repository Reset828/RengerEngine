#include "data/chunk/GridRunFile.h"

#include <dzc/Error.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kPositionMask = static_cast<std::uint32_t>(dzc::PointAttribute::Position);
constexpr std::uint32_t kColorMask = static_cast<std::uint32_t>(dzc::PointAttribute::Color);
constexpr std::uint32_t kIntensityMask = static_cast<std::uint32_t>(dzc::PointAttribute::Intensity);

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto timestamp = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        m_path = std::filesystem::temp_directory_path() /
            ("dzc_grid_run_file_tests_" + std::to_string(timestamp));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code errorCode;
        std::filesystem::remove_all(m_path, errorCode);
    }

    const std::filesystem::path& path() const noexcept {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

void assertError(const dzc::Error& error, dzc::ErrorDomain domain, std::uint32_t code) {
    assert(error.domain == domain);
    assert(error.code == code);
}

std::vector<dzc::GridBucket> makeBuckets() {
    dzc::GridBucket first;
    first.key = dzc::GridCellKey{-2, 0, 3};
    first.points.schema.mask = kPositionMask | kColorMask | kIntensityMask;
    first.points.positions = {
        glm::dvec3{-1.5, 2.0, 3.5},
        glm::dvec3{-1.0, 2.5, 4.0}};
    first.points.colorsRgba8 = {0x01020304U, 0xAABBCCDDU};
    first.points.intensities = {7U, 65535U};
    first.sourceIndices = {1U, 5U};

    dzc::GridBucket second;
    second.key = dzc::GridCellKey{4, -1, 0};
    second.points.schema.mask = kPositionMask;
    second.points.positions = {glm::dvec3{9.0, 8.0, 7.0}};
    second.sourceIndices = {9U};

    return {first, second};
}

void assertBucketsEqual(
    const std::vector<dzc::GridBucket>& expected,
    const std::vector<dzc::GridBucket>& actual) {
    assert(expected.size() == actual.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto& expectedBucket = expected[index];
        const auto& actualBucket = actual[index];
        assert(expectedBucket.key == actualBucket.key);
        assert(expectedBucket.points.schema.mask == actualBucket.points.schema.mask);
        assert(expectedBucket.points.positions == actualBucket.points.positions);
        assert(expectedBucket.points.colorsRgba8 == actualBucket.points.colorsRgba8);
        assert(expectedBucket.points.intensities == actualBucket.points.intensities);
        assert(expectedBucket.sourceIndices == actualBucket.sourceIndices);
    }
}

dzc::GridRunFile makeRun(const std::filesystem::path& directory) {
    auto result = dzc::GridRunFile::create(directory);
    assert(result.hasValue());
    return std::move(result.value());
}

void writeAndComplete(dzc::GridRunFile& run, const std::vector<dzc::GridBucket>& buckets) {
    assert(run.write(buckets).hasValue());
    assert(run.complete().hasValue());
    assert(run.isComplete());
}

void testCreateAndRoundTripPreserveAllStreams() {
    TemporaryDirectory directory;
    const std::vector<dzc::GridBucket> expected = makeBuckets();
    std::filesystem::path completedPath;
    {
        auto run = makeRun(directory.path());
        assert(!run.isComplete());
        assert(std::filesystem::exists(run.path()));
        writeAndComplete(run, expected);
        completedPath = run.path();
        assert(std::filesystem::exists(completedPath));

        const auto firstRead = run.read();
        const auto secondRead = run.read();
        assert(firstRead.hasValue());
        assert(secondRead.hasValue());
        assertBucketsEqual(expected, firstRead.value());
        assertBucketsEqual(firstRead.value(), secondRead.value());
    }
    assert(std::filesystem::exists(completedPath));
}

void testIncompleteRunsAreRemovedAndCompletedRunsAreRetained() {
    TemporaryDirectory directory;
    std::filesystem::path unfinishedPath;
    {
        auto run = makeRun(directory.path());
        unfinishedPath = run.path();
        assert(std::filesystem::exists(unfinishedPath));
    }
    assert(!std::filesystem::exists(unfinishedPath));

    std::filesystem::path completedPath;
    {
        auto run = makeRun(directory.path());
        writeAndComplete(run, makeBuckets());
        completedPath = run.path();
    }
    assert(std::filesystem::exists(completedPath));
}

void testStateValidationAndInvalidInputCleanup() {
    TemporaryDirectory directory;
    auto run = makeRun(directory.path());
    const auto beforeWriteComplete = run.complete();
    assert(!beforeWriteComplete.hasValue());
    assertError(beforeWriteComplete.error(), dzc::ErrorDomain::Task, 1U);

    auto invalidBuckets = makeBuckets();
    invalidBuckets[1].key = invalidBuckets[0].key;
    const std::filesystem::path path = run.path();
    const auto invalidWrite = run.write(invalidBuckets);
    assert(!invalidWrite.hasValue());
    assertError(invalidWrite.error(), dzc::ErrorDomain::DataFormat, 2U);
    assert(!std::filesystem::exists(path));

    const auto secondWrite = run.write(makeBuckets());
    assert(!secondWrite.hasValue());
    assertError(secondWrite.error(), dzc::ErrorDomain::Task, 1U);
}

void testCancellationCleansRuns() {
    TemporaryDirectory directory;
    const auto buckets = makeBuckets();

    dzc::tasks::CancellationSource writeSource;
    assert(writeSource.requestCancellation());
    auto writeRun = makeRun(directory.path());
    const std::filesystem::path writePath = writeRun.path();
    const auto writeResult = writeRun.write(buckets, writeSource.token());
    assert(!writeResult.hasValue());
    assertError(writeResult.error(), dzc::ErrorDomain::Task, 7U);
    assert(!std::filesystem::exists(writePath));

    auto readRun = makeRun(directory.path());
    writeAndComplete(readRun, buckets);
    const std::filesystem::path readPath = readRun.path();
    dzc::tasks::CancellationSource readSource;
    assert(readSource.requestCancellation());
    const auto readResult = readRun.read(readSource.token());
    assert(!readResult.hasValue());
    assertError(readResult.error(), dzc::ErrorDomain::Task, 7U);
    assert(!std::filesystem::exists(readPath));
    assert(!readRun.isComplete());
}

void testCorruptAndTruncatedFilesAreRejectedAndRemoved() {
    TemporaryDirectory directory;

    auto badMagicRun = makeRun(directory.path());
    writeAndComplete(badMagicRun, makeBuckets());
    const std::filesystem::path badMagicPath = badMagicRun.path();
    {
        std::fstream file(badMagicPath, std::ios::binary | std::ios::in | std::ios::out);
        assert(file);
        file.put('X');
        file.flush();
        assert(file);
    }
    const auto badMagic = badMagicRun.read();
    assert(!badMagic.hasValue());
    assertError(badMagic.error(), dzc::ErrorDomain::DataFormat, 2U);
    assert(!std::filesystem::exists(badMagicPath));

    auto badVersionRun = makeRun(directory.path());
    writeAndComplete(badVersionRun, makeBuckets());
    const std::filesystem::path badVersionPath = badVersionRun.path();
    {
        std::fstream file(badVersionPath, std::ios::binary | std::ios::in | std::ios::out);
        assert(file);
        file.seekp(4, std::ios::beg);
        const std::uint32_t unsupportedVersion = 2U;
        file.write(reinterpret_cast<const char*>(&unsupportedVersion), sizeof(unsupportedVersion));
        file.flush();
        assert(file);
    }
    const auto badVersion = badVersionRun.read();
    assert(!badVersion.hasValue());
    assertError(badVersion.error(), dzc::ErrorDomain::DataFormat, 2U);
    assert(!std::filesystem::exists(badVersionPath));

    auto truncatedRun = makeRun(directory.path());
    writeAndComplete(truncatedRun, makeBuckets());
    const std::filesystem::path truncatedPath = truncatedRun.path();
    std::filesystem::resize_file(truncatedPath, 5U);
    const auto truncated = truncatedRun.read();
    assert(!truncated.hasValue());
    assertError(truncated.error(), dzc::ErrorDomain::DataFormat, 2U);
    assert(!std::filesystem::exists(truncatedPath));

    auto missingRun = makeRun(directory.path());
    writeAndComplete(missingRun, makeBuckets());
    const std::filesystem::path missingPath = missingRun.path();
    std::filesystem::remove(missingPath);
    const auto missing = missingRun.read();
    assert(!missing.hasValue());
    assertError(missing.error(), dzc::ErrorDomain::FileIo, 2U);
}

void testDirectoryAndMoveSemantics() {
    TemporaryDirectory directory;
    const auto filePath = directory.path() / "not_a_directory";
    {
        std::ofstream file(filePath);
        assert(file);
    }
    const auto invalidDirectory = dzc::GridRunFile::create(filePath);
    assert(!invalidDirectory.hasValue());
    assertError(invalidDirectory.error(), dzc::ErrorDomain::FileIo, 1U);

    auto first = makeRun(directory.path());
    const std::filesystem::path firstPath = first.path();
    auto second = std::move(first);
    assert(std::filesystem::exists(firstPath));
    writeAndComplete(second, makeBuckets());
    assert(second.complete().hasValue());
}

void testTypeProperties() {
    static_assert(!std::is_copy_constructible_v<dzc::GridRunFile>);
    static_assert(!std::is_copy_assignable_v<dzc::GridRunFile>);
    static_assert(std::is_move_constructible_v<dzc::GridRunFile>);
    static_assert(std::is_move_assignable_v<dzc::GridRunFile>);
}

} // namespace

int main() {
    testCreateAndRoundTripPreserveAllStreams();
    testIncompleteRunsAreRemovedAndCompletedRunsAreRetained();
    testStateValidationAndInvalidInputCleanup();
    testCancellationCleansRuns();
    testCorruptAndTruncatedFilesAreRejectedAndRemoved();
    testDirectoryAndMoveSemantics();
    testTypeProperties();
    return 0;
}
