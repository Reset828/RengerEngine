#include "data/io/pcl/PlyReader.h"

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
        m_path = std::filesystem::temp_directory_path() / ("dzc-ply-batch-tests-" + std::to_string(timestamp));
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

std::string xyzHeader(std::uint64_t count, const std::string& format = "ascii 1.0") {
    return "ply\nformat " + format + "\n"
           "element vertex " + std::to_string(count) + "\n"
           "property float x\nproperty float y\nproperty float z\nend_header\n";
}

void writeBinaryColorIntensityPly(const std::filesystem::path& path, bool alpha) {
    const std::string header =
        "ply\nformat binary_little_endian 1.0\n"
        "element vertex 3\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\nproperty uchar blue\n" +
        std::string(alpha ? "property uchar alpha\n" : "") +
        "property float intensity\nend_header\n";
    std::ofstream output(path, std::ios::binary);
    assert(output.is_open());
    output.write(header.data(), static_cast<std::streamsize>(header.size()));

    const std::uint8_t red[] = {0x11U, 0x55U, 0x99U};
    const std::uint8_t green[] = {0x22U, 0x66U, 0xAAU};
    const std::uint8_t blue[] = {0x33U, 0x77U, 0xBBU};
    const std::uint8_t alphaValues[] = {0x40U, 0x80U, 0xFFU};
    const float intensity[] = {10.0F, 20.0F, 30.0F};
    for (std::size_t index = 0U; index < 3U; ++index) {
        writeValue(output, static_cast<float>(index + 1U));
        writeValue(output, static_cast<float>(index + 2U));
        writeValue(output, static_cast<float>(index + 3U));
        writeValue(output, red[index]);
        writeValue(output, green[index]);
        writeValue(output, blue[index]);
        if (alpha) {
            writeValue(output, alphaValues[index]);
        }
        writeValue(output, intensity[index]);
    }
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

dzc::PointBatch readBatch(dzc::PlyReader& reader, std::size_t maximumPoints) {
    const auto result = reader.readNext(maximumPoints, {});
    assert(result.hasValue());
    assert(result.value().has_value());
    assert(result.value()->validate().hasValue());
    return std::move(*result.value());
}

void testAsciiBatchingAndEof(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "batched-ascii.ply";
    writeTextFile(path, xyzHeader(5U) + "1 2 3\n4 5 6\n7 8 9\n10 11 12\n13 14 15\n");
    const auto bytesBefore = readBytes(path);

    dzc::PlyReader reader;
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
    const auto rgbaPath = directory.path() / "rgba-intensity-binary.ply";
    writeBinaryColorIntensityPly(rgbaPath, true);

    dzc::PlyReader rgbaReader;
    const auto openedRgba = rgbaReader.open(rgbaPath.string());
    assert(openedRgba.hasValue());
    assert(openedRgba.value().schema.mask == (kPositionMask | kColorMask | kIntensityMask));
    const dzc::PointBatch rgbaFirst = readBatch(rgbaReader, 2U);
    const dzc::PointBatch rgbaSecond = readBatch(rgbaReader, 2U);
    assert(rgbaFirst.colorsRgba8 == std::vector<std::uint32_t>({0x11223340U, 0x55667780U}));
    assert(rgbaSecond.colorsRgba8 == std::vector<std::uint32_t>({0x99AABBFFU}));
    assert(rgbaFirst.intensities == std::vector<std::uint16_t>({0U, 32768U}));
    assert(rgbaSecond.intensities == std::vector<std::uint16_t>({65535U}));

    const auto rgbPath = directory.path() / "rgb-intensity-binary.ply";
    writeBinaryColorIntensityPly(rgbPath, false);
    dzc::PlyReader rgbReader;
    assert(rgbReader.open(rgbPath.string()).hasValue());
    const dzc::PointBatch rgbBatch = readBatch(rgbReader, 3U);
    assert(rgbBatch.colorsRgba8 == std::vector<std::uint32_t>({0x112233FFU, 0x556677FFU, 0x99AABBFFU}));
}

void testCoordinateFilteringAndIntensityEdgeCases(const TemporaryDirectory& directory) {
    const auto filteredPath = directory.path() / "filtered.ply";
    writeTextFile(
        filteredPath,
        "ply\nformat ascii 1.0\nelement vertex 4\n"
        "property float x\nproperty float y\nproperty float z\nproperty float intensity\nend_header\n"
        "1 2 3 7\nnan 3 4 99\n5 inf 7 9\n8 9 10 nan\n");
    dzc::PlyReader filteredReader;
    assert(filteredReader.open(filteredPath.string()).hasValue());
    const dzc::PointBatch filtered = readBatch(filteredReader, 10U);
    assert(filtered.positions.size() == 2U);
    assert(filtered.intensities == std::vector<std::uint16_t>({0U, 0U}));

    const auto invalidPath = directory.path() / "all-invalid.ply";
    writeTextFile(invalidPath, xyzHeader(2U) + "nan 2 3\n1 inf 3\n");
    dzc::PlyReader invalidReader;
    assert(invalidReader.open(invalidPath.string()).hasValue());
    const auto firstFailure = invalidReader.readNext(1U, {});
    assertError(firstFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    const auto repeatedFailure = invalidReader.readNext(1U, {});
    assertError(repeatedFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    assert(firstFailure.error().diagnosticMessage == repeatedFailure.error().diagnosticMessage);
}

void assertCorruptAtOpenOrFirstRead(const std::filesystem::path& path) {
    dzc::PlyReader reader;
    const auto opened = reader.open(path.string());
    if (!opened.hasValue()) {
        // PCL 1.15.1 can discover a truncated body while parsing its header.
        assertError(opened, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
        return;
    }

    const auto firstFailure = reader.readNext(1U, {});
    assertError(firstFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    const auto repeatedFailure = reader.readNext(1U, {});
    assertError(repeatedFailure, dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    assert(firstFailure.error().diagnosticMessage == repeatedFailure.error().diagnosticMessage);
}

void testEmptyAndInvalidBodies(const TemporaryDirectory& directory) {
    const auto emptyPath = directory.path() / "empty.ply";
    writeTextFile(emptyPath, xyzHeader(0U));
    dzc::PlyReader emptyReader;
    assert(emptyReader.open(emptyPath.string()).hasValue());
    const auto eof = emptyReader.readNext(1U, {});
    assert(eof.hasValue() && !eof.value().has_value());

    const auto truncatedAsciiPath = directory.path() / "truncated-ascii.ply";
    writeTextFile(truncatedAsciiPath, xyzHeader(2U) + "1 2 3\n");
    assertCorruptAtOpenOrFirstRead(truncatedAsciiPath);

    const auto truncatedBinaryPath = directory.path() / "truncated-binary.ply";
    writeTextFile(truncatedBinaryPath, xyzHeader(1U, "binary_little_endian 1.0") + "\x00\x00\x80\x3F");
    assertCorruptAtOpenOrFirstRead(truncatedBinaryPath);
}

void testCancellationAndLifecycle(const TemporaryDirectory& directory) {
    const auto path = directory.path() / "cancellation.ply";
    writeTextFile(path, xyzHeader(3U) + "1 2 3\n4 5 6\n7 8 9\n");
    dzc::PlyReader reader;
    dzc::tasks::CancellationSource source;
    assert(source.requestCancellation());
    assertError(reader.readNext(1U, source.token()), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open(path.string()).hasValue());
    assertError(reader.readNext(0U, {}), dzc::ErrorDomain::Configuration, kInvalidValueCode);
    assertError(reader.readNext(1U, source.token()), dzc::ErrorDomain::Task, kCancelledCode);
    const dzc::PointBatch first = readBatch(reader, 1U);
    assert((first.positions[0] == glm::dvec3{1.0, 2.0, 3.0}));

    dzc::tasks::CancellationSource postConversionSource;
    assert(postConversionSource.requestCancellation());
    assertError(reader.readNext(1U, postConversionSource.token()), dzc::ErrorDomain::Task, kCancelledCode);
    const dzc::PointBatch second = readBatch(reader, 1U);
    assert((second.positions[0] == glm::dvec3{4.0, 5.0, 6.0}));

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
