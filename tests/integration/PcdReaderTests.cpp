#include "data/io/pcl/PcdReader.h"

#include <dzc/Error.h>

#include "tasks/Cancellation.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kInternalErrorCode = 1U;
constexpr std::uint32_t kPositionMask = 1U;
constexpr std::uint32_t kColorMask = 2U;
constexpr std::uint32_t kIntensityMask = 4U;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
            ("dzc-pcd-reader-tests-" + std::to_string(timestamp));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const noexcept {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    assert(output.good());
}

void writeBinaryRgbaPcd(const std::filesystem::path& path) {
    const std::string header =
        "# .PCD v0.7\n"
        "VERSION 0.7\n"
        "FIELDS x y z rgba\n"
        "SIZE 4 4 4 4\n"
        "TYPE F F F U\n"
        "COUNT 1 1 1 1\n"
        "WIDTH 1\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS 1\n"
        "DATA binary\n";
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    const std::uint8_t pointBytes[16]{};
    output.write(
        reinterpret_cast<const char*>(pointBytes),
        static_cast<std::streamsize>(sizeof(pointBytes)));
    assert(output.good());
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void assertDefaultDeferredMetadata(const dzc::PointCloudSourceInfo& sourceInfo) {
    const dzc::Bounds3d defaultBounds{};
    assert(sourceInfo.bounds.minimum.x == defaultBounds.minimum.x);
    assert(sourceInfo.bounds.minimum.y == defaultBounds.minimum.y);
    assert(sourceInfo.bounds.minimum.z == defaultBounds.minimum.z);
    assert(sourceInfo.bounds.maximum.x == defaultBounds.maximum.x);
    assert(sourceInfo.bounds.maximum.y == defaultBounds.maximum.y);
    assert(sourceInfo.bounds.maximum.z == defaultBounds.maximum.z);
    assert(!sourceInfo.bounds.isValid());
    assert(!sourceInfo.intensity.available);
    assert(sourceInfo.intensity.sourceMinimum == 0.0);
    assert(sourceInfo.intensity.sourceMaximum == 0.0);
    assert(sourceInfo.intensity.validMinimum == 0.0);
    assert(sourceInfo.intensity.validMaximum == 0.0);
}

template <typename ResultType>
void assertError(const ResultType& result, dzc::ErrorDomain domain, std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

std::string xyzHeader(std::uint64_t pointCount) {
    return
        "# .PCD v0.7\n"
        "VERSION 0.7\n"
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "WIDTH " + std::to_string(pointCount) + "\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS " + std::to_string(pointCount) + "\n"
        "DATA ascii\n";
}

void testOpensAsciiXyzWithoutReadingOrChangingFile(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "xyz.pcd";
    writeTextFile(path, xyzHeader(2U) + "1 2 3\n4 5 6\n");
    const std::vector<std::uint8_t> before = readBytes(path);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);
    assert(opened.value().declaredPointCount == 2U);
    assertDefaultDeferredMetadata(opened.value());
    assert(readBytes(path) == before);
}

void testOpensAsciiColorAndIntensityHeader(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "rgb-intensity.pcd";
    const std::string content =
        "# .PCD v0.7\n"
        "VERSION 0.7\n"
        "FIELDS x y z rgb intensity\n"
        "SIZE 4 4 4 4 4\n"
        "TYPE F F F F F\n"
        "COUNT 1 1 1 1 1\n"
        "WIDTH 1\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS 1\n"
        "DATA ascii\n"
        "1 2 3 0 42\n";
    writeTextFile(path, content);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask | kIntensityMask));
    assert(opened.value().declaredPointCount == 1U);
    assertDefaultDeferredMetadata(opened.value());
}

void testOpensBinaryRgbaHeader(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "rgba-binary.pcd";
    writeBinaryRgbaPcd(path);
    const std::vector<std::uint8_t> before = readBytes(path);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask));
    assert(opened.value().declaredPointCount == 1U);
    assertDefaultDeferredMetadata(opened.value());
    assert(readBytes(path) == before);
}

void testAllowsZeroPointHeader(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "empty.pcd";
    writeTextFile(path, xyzHeader(0U));

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);
    assert(opened.value().declaredPointCount == 0U);
}

void testReportsInvalidHeadersAsCorruptData(const TemporaryDirectory& directory) {
    const std::filesystem::path corruptPath = directory.path() / "corrupt.pcd";
    const std::filesystem::path missingCoordinatePath = directory.path() / "missing-z.pcd";
    const std::filesystem::path invalidCoordinatePath = directory.path() / "invalid-x.pcd";
    const std::filesystem::path duplicateCoordinatePath = directory.path() / "duplicate-x.pcd";
    const std::filesystem::path upperCaseCoordinatePath = directory.path() / "upper-x.pcd";
    writeTextFile(corruptPath, "not a PCD header\n");
    writeTextFile(
        missingCoordinatePath,
        "# .PCD v0.7\nVERSION 0.7\nFIELDS x y\nSIZE 4 4\nTYPE F F\nCOUNT 1 1\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n1 2\n");
    writeTextFile(
        invalidCoordinatePath,
        "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 2 1 1\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n1 2 3 4\n");
    writeTextFile(
        duplicateCoordinatePath,
        "# .PCD v0.7\nVERSION 0.7\nFIELDS x x y z\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n1 2 3 4\n");
    writeTextFile(
        upperCaseCoordinatePath,
        "# .PCD v0.7\nVERSION 0.7\nFIELDS X y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n1 2 3\n");

    for (const std::filesystem::path& path : {
             corruptPath,
             missingCoordinatePath,
             invalidCoordinatePath,
             duplicateCoordinatePath,
             upperCaseCoordinatePath,
             directory.path() / "does-not-exist.pcd"}) {
        dzc::PcdReader reader;
        assertError(reader.open(path.string()), dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    }
}

void testLifecycleAndDeferredReadErrors(const TemporaryDirectory& directory) {
    const std::filesystem::path firstPath = directory.path() / "first.pcd";
    const std::filesystem::path secondPath = directory.path() / "second.pcd";
    writeTextFile(firstPath, xyzHeader(1U) + "1 2 3\n");
    writeTextFile(secondPath, xyzHeader(1U) + "4 5 6\n");

    dzc::PcdReader reader;
    dzc::tasks::CancellationSource cancellationSource;
    assert(cancellationSource.requestCancellation());

    assertError(reader.readNext(0U, cancellationSource.token()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(firstPath.string()).hasValue());
    assertError(reader.open(secondPath.string()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assertError(reader.readNext(0U, cancellationSource.token()), dzc::ErrorDomain::Configuration, kInvalidValueCode);
    assertError(reader.readNext(1U, cancellationSource.token()), dzc::ErrorDomain::Task, kCancelledCode);
    const auto firstBatch = reader.readNext(1U, {});
    assert(firstBatch.hasValue());
    assert(firstBatch.value().has_value());
    assert(firstBatch.value()->validate().hasValue());

    reader.close();
    reader.close();
    assertError(reader.readNext(1U, {}), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(secondPath.string()).hasValue());
}

} // namespace

int main() {
    const TemporaryDirectory directory;
    testOpensAsciiXyzWithoutReadingOrChangingFile(directory);
    testOpensAsciiColorAndIntensityHeader(directory);
    testOpensBinaryRgbaHeader(directory);
    testAllowsZeroPointHeader(directory);
    testReportsInvalidHeadersAsCorruptData(directory);
    testLifecycleAndDeferredReadErrors(directory);
    return 0;
}
