#include "data/io/pcl/PcdReader.h"

#include <dzc/Error.h>

#include "tasks/Cancellation.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
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
        m_path = std::filesystem::temp_directory_path() / ("dzc-pcd-batch-tests-" + std::to_string(timestamp));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

void writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    assert(output.good());
}

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

template <typename Value>
void writeValue(std::ofstream& output, Value value) {
    output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(value)));
    assert(output.good());
}

float floatFromBits(std::uint32_t bits) {
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void writeBinaryColorPcd(const std::filesystem::path& path) {
    const std::string header =
        "# .PCD v0.7\nVERSION 0.7\n"
        "FIELDS x y z rgb rgba intensity\n"
        "SIZE 4 4 4 4 4 4\n"
        "TYPE F F F U F F\n"
        "COUNT 1 1 1 1 1 1\n"
        "WIDTH 3\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 3\nDATA binary\n";
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));

    // rgba is selected over rgb. Its packed source order is AARRGGBB.
    const std::uint32_t rgb[] = {0x00010203U, 0x00040506U, 0x00070809U};
    const std::uint32_t rgba[] = {0x40112233U, 0x80556677U, 0xFF99AABBU};
    const float intensity[] = {10.0F, 20.0F, 30.0F};
    for (std::size_t index = 0U; index < 3U; ++index) {
        writeValue(output, static_cast<float>(index + 1U));
        writeValue(output, static_cast<float>(index + 2U));
        writeValue(output, static_cast<float>(index + 3U));
        writeValue(output, rgb[index]);
        writeValue(output, floatFromBits(rgba[index]));
        writeValue(output, intensity[index]);
    }
}

void writeBinaryRgbPcd(const std::filesystem::path& path) {
    const std::string header =
        "# .PCD v0.7\nVERSION 0.7\n"
        "FIELDS x y z rgb\n"
        "SIZE 4 4 4 4\n"
        "TYPE F F F U\n"
        "COUNT 1 1 1 1\n"
        "WIDTH 1\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 1\nDATA binary\n";
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    writeValue(output, 1.0F);
    writeValue(output, 2.0F);
    writeValue(output, 3.0F);
    writeValue(output, std::uint32_t{0x00112233U});
}
void assertError(const dzc::Result<std::optional<dzc::PointBatch>>& result, dzc::ErrorDomain domain, std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

std::string xyzHeader(std::uint64_t count, const std::string& dataEncoding = "ascii") {
    return "# .PCD v0.7\nVERSION 0.7\n"
           "FIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH " +
           std::to_string(count) + "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " +
           std::to_string(count) + "\nDATA " + dataEncoding + "\n";
}

dzc::PointBatch readBatch(dzc::PcdReader& reader, std::size_t maximumPoints) {
    const auto result = reader.readNext(maximumPoints, {});
    assert(result.hasValue());
    assert(result.value().has_value());
    assert(result.value()->validate().hasValue());
    return std::move(*result.value());
}

void testAsciiBatchingAndEof(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "batched-ascii.pcd";
    writeTextFile(path, xyzHeader(5U) + "1 2 3\n4 5 6\n7 8 9\n10 11 12\n13 14 15\n");
    const auto bytesBefore = readBytes(path);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);

    const dzc::PointBatch first = readBatch(reader, 2U);
    const dzc::PointBatch second = readBatch(reader, 2U);
    const dzc::PointBatch third = readBatch(reader, 2U);
    assert(first.positions.size() == 2U);
    assert(second.positions.size() == 2U);
    assert(third.positions.size() == 1U);
    assert((first.positions[0] == glm::dvec3{1.0, 2.0, 3.0}));
    assert((second.positions[1] == glm::dvec3{10.0, 11.0, 12.0}));
    assert((third.positions[0] == glm::dvec3{13.0, 14.0, 15.0}));

    const auto eof = reader.readNext(2U, {});
    assert(eof.hasValue() && !eof.value().has_value());
    const auto repeatedEof = reader.readNext(2U, {});
    assert(repeatedEof.hasValue() && !repeatedEof.value().has_value());
    assert(readBytes(path) == bytesBefore);
}

void testBinaryColorAndFullFileIntensity(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "color-intensity-binary.pcd";
    writeBinaryColorPcd(path);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask | kIntensityMask));
    assert(!opened.value().intensity.available);

    const dzc::PointBatch first = readBatch(reader, 2U);
    const dzc::PointBatch second = readBatch(reader, 2U);
    assert(first.colorsRgba8 == std::vector<std::uint32_t>({0x11223340U, 0x55667780U}));
    assert(second.colorsRgba8 == std::vector<std::uint32_t>({0x99AABBFFU}));
    assert(first.intensities == std::vector<std::uint16_t>({0U, 32768U}));
    assert(second.intensities == std::vector<std::uint16_t>({65535U}));
}

void testRgbFallbackAddsOpaqueAlpha(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "rgb-binary.pcd";
    writeBinaryRgbPcd(path);

    dzc::PcdReader reader;
    const auto opened = reader.open(path.string());
    assert(opened.hasValue());
    assert(opened.value().schema.mask == (kPositionMask | kColorMask));
    const dzc::PointBatch batch = readBatch(reader, 1U);
    assert(batch.colorsRgba8 == std::vector<std::uint32_t>({0x112233FFU}));
}
void testCoordinateFilteringAndIntensityEdgeCases(const TemporaryDirectory& directory) {
    const auto filteredPath = directory.path() / "filtered.pcd";
    writeTextFile(
        filteredPath,
        "# .PCD v0.7\nVERSION 0.7\n"
        "FIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n"
        "WIDTH 4\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 4\nDATA ascii\n"
        "1 2 3 7\nnan 3 4 99\n5 inf 7 9\n8 9 10 nan\n");
    dzc::PcdReader filteredReader;
    assert(filteredReader.open(filteredPath.string()).hasValue());
    const dzc::PointBatch filtered = readBatch(filteredReader, 10U);
    assert(filtered.positions.size() == 2U);
    assert(filtered.intensities == std::vector<std::uint16_t>({0U, 0U}));

    const auto invalidPath = directory.path() / "all-invalid.pcd";
    writeTextFile(invalidPath, xyzHeader(2U) + "nan 2 3\n1 inf 3\n");
    dzc::PcdReader invalidReader;
    assert(invalidReader.open(invalidPath.string()).hasValue());
    const auto firstFailure = invalidReader.readNext(1U, {});
    assertError(firstFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    const auto repeatedFailure = invalidReader.readNext(1U, {});
    assertError(repeatedFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    assert(firstFailure.error().diagnosticMessage == repeatedFailure.error().diagnosticMessage);
}

void testEmptyAndInvalidBodies(const TemporaryDirectory& directory) {
    const auto emptyPath = directory.path() / "empty.pcd";
    writeTextFile(emptyPath, xyzHeader(0U));
    dzc::PcdReader emptyReader;
    assert(emptyReader.open(emptyPath.string()).hasValue());
    const auto eof = emptyReader.readNext(1U, {});
    assert(eof.hasValue() && !eof.value().has_value());

    const auto truncatedPath = directory.path() / "truncated-binary.pcd";
    writeTextFile(truncatedPath, xyzHeader(1U, "binary") + "\x00\x00\x80\x3F");
    dzc::PcdReader truncatedReader;
    assert(truncatedReader.open(truncatedPath.string()).hasValue());
    assertError(truncatedReader.readNext(1U, {}), dzc::ErrorDomain::DataFormat, kCorruptDataCode);

    const auto invalidOptionalPath = directory.path() / "invalid-rgb.pcd";
    writeTextFile(
        invalidOptionalPath,
        "# .PCD v0.7\nVERSION 0.7\n"
        "FIELDS x y z rgb\nSIZE 4 4 4 2\nTYPE F F F U\nCOUNT 1 1 1 1\n"
        "WIDTH 1\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 1\nDATA ascii\n1 2 3 7\n");
    dzc::PcdReader invalidOptionalReader;
    assert(invalidOptionalReader.open(invalidOptionalPath.string()).hasValue());
    assertError(invalidOptionalReader.readNext(1U, {}), dzc::ErrorDomain::DataFormat, kCorruptDataCode);

    const auto duplicateOptionalPath = directory.path() / "duplicate-intensity.pcd";
    writeTextFile(
        duplicateOptionalPath,
        "# .PCD v0.7\nVERSION 0.7\n"
        "FIELDS x y z intensity intensity\nSIZE 4 4 4 4 4\nTYPE F F F F F\nCOUNT 1 1 1 1 1\n"
        "WIDTH 1\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS 1\nDATA ascii\n1 2 3 4 5\n");
    dzc::PcdReader duplicateOptionalReader;
    assert(duplicateOptionalReader.open(duplicateOptionalPath.string()).hasValue());
    assertError(duplicateOptionalReader.readNext(1U, {}), dzc::ErrorDomain::DataFormat, kCorruptDataCode);

    const auto compressedPath = directory.path() / "compressed.pcd";
    writeTextFile(compressedPath, xyzHeader(1U, "binary_compressed"));
    dzc::PcdReader compressedReader;
    const auto compressedOpen = compressedReader.open(compressedPath.string());
    assert(!compressedOpen.hasValue());
    assert(compressedOpen.error().domain == dzc::ErrorDomain::DataFormat);
    assert(compressedOpen.error().code == kCorruptDataCode);
}

void testCancellationAndLifecycle(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "cancellation.pcd";
    writeTextFile(path, xyzHeader(3U) + "1 2 3\n4 5 6\n7 8 9\n");
    dzc::PcdReader reader;
    dzc::tasks::CancellationSource source;
    assert(source.requestCancellation());
    assertError(reader.readNext(1U, source.token()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(path.string()).hasValue());
    assertError(reader.readNext(0U, {}), dzc::ErrorDomain::Configuration, kInvalidValueCode);
    assertError(reader.readNext(1U, source.token()), dzc::ErrorDomain::Task, kCancelledCode);
    const dzc::PointBatch first = readBatch(reader, 1U);
    assert((first.positions[0] == glm::dvec3{1.0, 2.0, 3.0}));
    reader.close();
    reader.close();
    assertError(reader.readNext(1U, {}), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(path.string()).hasValue());
    assert(readBatch(reader, 3U).positions.size() == 3U);
}

} // namespace

int main() {
    const TemporaryDirectory directory;
    testAsciiBatchingAndEof(directory);
    testBinaryColorAndFullFileIntensity(directory);
    testCoordinateFilteringAndIntensityEdgeCases(directory);
    testEmptyAndInvalidBodies(directory);
    testCancellationAndLifecycle(directory);
    return 0;
}
