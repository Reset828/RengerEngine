#include "data/io/pcl/PlyReader.h"

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
constexpr std::uint32_t kInternalErrorCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kPositionMask = 1U;
constexpr std::uint32_t kColorMask = 2U;
constexpr std::uint32_t kIntensityMask = 4U;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
            ("dzc-ply-reader-tests-" + std::to_string(timestamp));
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

void writeBinaryRgbaPly(const std::filesystem::path& path) {
    const std::string header =
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property uchar red\n"
        "property uchar green\n"
        "property uchar blue\n"
        "property uchar alpha\n"
        "end_header\n";
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

std::string xyzHeader(std::uint64_t pointCount, const std::string& format = "ascii 1.0") {
    return
        "ply\n"
        "format " + format + "\n"
        "element vertex " + std::to_string(pointCount) + "\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "end_header\n";
}

void testOpensAsciiXyzWithoutReadingOrChangingFile(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "xyz.ply";
    writeTextFile(path, xyzHeader(2U) + "1 2 3\n4 5 6\n");
    const std::vector<std::uint8_t> before = readBytes(path);

    dzc::PlyReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);
    assert(opened.value().declaredPointCount == 2U);
    assertDefaultDeferredMetadata(opened.value());
    assert(readBytes(path) == before);
}

void testOpensAsciiColorIntensityUnknownAndFace(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "color-intensity.ply";
    const std::string content =
        "ply\n"
        "format ascii 1.0\n"
        "comment fixture keeps an unknown vertex property and face list\n"
        "element vertex 1\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "property uchar red\n"
        "property uchar green\n"
        "property uchar blue\n"
        "property double intensity\n"
        "property float vendor_score\n"
        "element face 0\n"
        "property list uchar int vertex_indices\n"
        "end_header\n"
        "1 2 3 10 20 30 42 9\n";
    writeTextFile(path, content);

    dzc::PlyReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask | kIntensityMask));
    assert(opened.value().declaredPointCount == 1U);
    assertDefaultDeferredMetadata(opened.value());
}

void testOpensBinaryLittleEndianRgba(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "rgba-binary.ply";
    writeBinaryRgbaPly(path);
    const std::vector<std::uint8_t> before = readBytes(path);

    dzc::PlyReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask));
    assert(opened.value().declaredPointCount == 1U);
    assertDefaultDeferredMetadata(opened.value());
    assert(readBytes(path) == before);
}

void testAllowsZeroPointHeader(const TemporaryDirectory& directory) {
    const std::filesystem::path path = directory.path() / "empty.ply";
    writeTextFile(path, xyzHeader(0U));

    dzc::PlyReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);
    assert(opened.value().declaredPointCount == 0U);
}

void testReportsInvalidHeadersAsCorruptData(const TemporaryDirectory& directory) {
    const std::filesystem::path corruptPath = directory.path() / "corrupt.ply";
    const std::filesystem::path missingCoordinatePath = directory.path() / "missing-z.ply";
    const std::filesystem::path duplicateCoordinatePath = directory.path() / "duplicate-x.ply";
    const std::filesystem::path upperCaseCoordinatePath = directory.path() / "upper-x.ply";
    const std::filesystem::path partialColorPath = directory.path() / "partial-color.ply";
    const std::filesystem::path invalidColorTypePath = directory.path() / "invalid-color-type.ply";
    const std::filesystem::path invalidAlphaTypePath = directory.path() / "invalid-alpha-type.ply";
    const std::filesystem::path duplicateIntensityPath = directory.path() / "duplicate-intensity.ply";
    const std::filesystem::path invalidIntensityPath = directory.path() / "invalid-intensity.ply";
    const std::filesystem::path bigEndianPath = directory.path() / "big-endian.ply";

    writeTextFile(corruptPath, "not a PLY header\n");
    writeTextFile(
        missingCoordinatePath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nend_header\n1 2\n");
    writeTextFile(
        duplicateCoordinatePath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float x\nproperty float y\nproperty float z\nend_header\n1 2 3 4\n");
    writeTextFile(
        upperCaseCoordinatePath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float X\nproperty float y\nproperty float z\nend_header\n1 2 3\n");
    writeTextFile(
        partialColorPath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nend_header\n1 2 3 4\n");
    writeTextFile(
        invalidColorTypePath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty float red\nproperty float green\nproperty float blue\nend_header\n1 2 3 4 5 6\n");
    writeTextFile(
        invalidAlphaTypePath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nproperty ushort alpha\nend_header\n1 2 3 4 5 6 7\n");
    writeTextFile(
        duplicateIntensityPath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty float intensity\nproperty float intensity\nend_header\n1 2 3 4 5\n");
    writeTextFile(
        invalidIntensityPath,
        "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty list uchar float intensity\nend_header\n1 2 3 0\n");
    writeTextFile(
        bigEndianPath,
        xyzHeader(1U, "binary_big_endian 1.0") + std::string(12U, '\0'));

    for (const std::filesystem::path& path : {
             corruptPath,
             missingCoordinatePath,
             duplicateCoordinatePath,
             upperCaseCoordinatePath,
             partialColorPath,
             invalidColorTypePath,
             invalidAlphaTypePath,
             duplicateIntensityPath,
             invalidIntensityPath,
             bigEndianPath,
             directory.path() / "does-not-exist.ply"}) {
        dzc::PlyReader reader;
        assertError(reader.open(path.string()), dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    }
}

void testLifecycleAndDeferredReadErrors(const TemporaryDirectory& directory) {
    const std::filesystem::path firstPath = directory.path() / "first.ply";
    const std::filesystem::path secondPath = directory.path() / "second.ply";
    writeTextFile(firstPath, xyzHeader(1U) + "1 2 3\n");
    writeTextFile(secondPath, xyzHeader(1U) + "4 5 6\n");

    dzc::PlyReader reader;
    dzc::tasks::CancellationSource cancellationSource;
    assert(cancellationSource.requestCancellation());

    assertError(reader.readNext(0U, cancellationSource.token()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(firstPath.string()).hasValue());
    assertError(reader.open(secondPath.string()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assertError(reader.readNext(0U, cancellationSource.token()), dzc::ErrorDomain::Configuration, kInvalidValueCode);
    assertError(reader.readNext(1U, cancellationSource.token()), dzc::ErrorDomain::Task, kCancelledCode);
    const auto batch = reader.readNext(1U, {});
    assert(batch.hasValue());
    assert(batch.value().has_value());
    assert(batch.value()->positions.size() == 1U);
    assert((batch.value()->positions[0] == glm::dvec3{1.0, 2.0, 3.0}));

    reader.close();
    reader.close();
    assertError(reader.readNext(1U, {}), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(secondPath.string()).hasValue());
}

} // namespace

int main() {
    const TemporaryDirectory directory;
    testOpensAsciiXyzWithoutReadingOrChangingFile(directory);
    testOpensAsciiColorIntensityUnknownAndFace(directory);
    testOpensBinaryLittleEndianRgba(directory);
    testAllowsZeroPointHeader(directory);
    testReportsInvalidHeadersAsCorruptData(directory);
    testLifecycleAndDeferredReadErrors(directory);
    return 0;
}