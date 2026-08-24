#include "data/chunk/GridRunFile.h"

#include <dzc/Error.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kFileIoOpenFailedCode = 1U;
constexpr std::uint32_t kFileIoReadFailedCode = 2U;
constexpr std::uint32_t kFileIoWriteFailedCode = 3U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kResourceExhaustedCode = 1U;
constexpr std::uint32_t kFormatVersion = 1U;
constexpr std::uint32_t kSupportedSchemaMask =
    static_cast<std::uint32_t>(PointAttribute::Position) |
    static_cast<std::uint32_t>(PointAttribute::Color) |
    static_cast<std::uint32_t>(PointAttribute::Intensity);
constexpr std::array<char, 4U> kMagic{{'D', 'Z', 'G', 'R'}};
constexpr std::uint64_t kEndMarker = 0x445A4752554E4544ULL;
constexpr std::uint64_t kHeaderBytes = 4U + sizeof(std::uint32_t) + sizeof(std::uint64_t);
constexpr std::uint64_t kBucketFixedBytes =
    (3U * sizeof(std::int64_t)) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
constexpr std::uint64_t kFooterBytes = sizeof(kEndMarker);

Error makeError(
    ErrorDomain domain,
    std::uint32_t code,
    const char* userMessage,
    const char* diagnosticMessage) {
    return Error{domain, code, userMessage, diagnosticMessage, "GridRunFile"};
}

Error openError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::FileIo,
        kFileIoOpenFailedCode,
        "Grid run file could not be opened.",
        diagnosticMessage);
}

Error readError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::FileIo,
        kFileIoReadFailedCode,
        "Grid run file could not be read.",
        diagnosticMessage);
}

Error writeError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::FileIo,
        kFileIoWriteFailedCode,
        "Grid run file could not be written.",
        diagnosticMessage);
}

Error corruptDataError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Grid run file data is invalid.",
        diagnosticMessage);
}

Error cancelledError() {
    return makeError(
        ErrorDomain::Task,
        kCancelledCode,
        "Grid run file operation cancelled.",
        "GridRunFile observed a requested cancellation.");
}

Error invalidStateError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::Task,
        kInvalidTaskCode,
        "Grid run file operation is not valid in the current state.",
        diagnosticMessage);
}

Error resourceError(const char* diagnosticMessage) {
    return makeError(
        ErrorDomain::Resource,
        kResourceExhaustedCode,
        "Grid run file operation ran out of memory.",
        diagnosticMessage);
}

bool checkedAdd(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool checkedSizeToUint64(std::size_t value, std::uint64_t& result) noexcept {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
    }
    result = static_cast<std::uint64_t>(value);
    return true;
}

bool hasValidSchema(const AttributeSchema& schema) noexcept {
    return schema.hasPosition() && (schema.mask & ~kSupportedSchemaMask) == 0U;
}

bool hasStrictlyIncreasingSourceIndices(const std::vector<std::uint64_t>& sourceIndices) noexcept {
    for (std::size_t index = 1U; index < sourceIndices.size(); ++index) {
        if (sourceIndices[index - 1U] >= sourceIndices[index]) {
            return false;
        }
    }
    return true;
}

Result<void> validateBucketsForWrite(const std::vector<GridBucket>& buckets) {
    for (std::size_t index = 0U; index < buckets.size(); ++index) {
        const GridBucket& bucket = buckets[index];
        if (index > 0U && !(buckets[index - 1U].key < bucket.key)) {
            return Result<void>::failure(corruptDataError(
                "Grid buckets must be in strictly increasing GridCellKey order."));
        }
        if (!hasValidSchema(bucket.points.schema)) {
            return Result<void>::failure(corruptDataError(
                "Grid bucket declares an unsupported attribute schema."));
        }
        const Result<void> validation = bucket.points.validate();
        if (!validation.hasValue()) {
            return Result<void>::failure(corruptDataError(
                "Grid bucket point streams do not match its declared schema."));
        }
        for (const glm::dvec3& position : bucket.points.positions) {
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z)) {
                return Result<void>::failure(corruptDataError(
                    "Grid bucket contains a non-finite point position."));
            }
        }
        if (bucket.points.positions.size() != bucket.sourceIndices.size()) {
            return Result<void>::failure(corruptDataError(
                "Grid bucket source index stream length does not match the point count."));
        }
        if (!hasStrictlyIncreasingSourceIndices(bucket.sourceIndices)) {
            return Result<void>::failure(corruptDataError(
                "Grid bucket source indices must be strictly increasing."));
        }
    }
    return Result<void>::success();
}

bool checkedBucketPayloadSize(
    const AttributeSchema& schema,
    std::uint64_t pointCount,
    std::uint64_t& result) noexcept {
    std::uint64_t bytesPerPoint = 3U * sizeof(double);
    if (schema.hasColor() && !checkedAdd(bytesPerPoint, sizeof(std::uint32_t), bytesPerPoint)) {
        return false;
    }
    if (schema.hasIntensity() && !checkedAdd(bytesPerPoint, sizeof(std::uint16_t), bytesPerPoint)) {
        return false;
    }
    if (!checkedAdd(bytesPerPoint, sizeof(std::uint64_t), bytesPerPoint)) {
        return false;
    }

    std::uint64_t streamBytes = 0U;
    return checkedMultiply(pointCount, bytesPerPoint, streamBytes) &&
        checkedAdd(kBucketFixedBytes, streamBytes, result);
}

bool hasVectorCapacityFor(std::uint64_t count) noexcept {
    return count <= static_cast<std::uint64_t>(std::vector<glm::dvec3>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint32_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint16_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint64_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

class BinaryWriter final {
public:
    explicit BinaryWriter(std::ofstream& stream) noexcept
        : m_stream(stream) {}

    template <typename T>
    bool writeValue(const T& value) noexcept {
        m_stream.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
        return static_cast<bool>(m_stream);
    }

    bool writeBytes(const char* bytes, std::size_t count) noexcept {
        m_stream.write(bytes, static_cast<std::streamsize>(count));
        return static_cast<bool>(m_stream);
    }

private:
    std::ofstream& m_stream;
};

class BinaryReader final {
public:
    BinaryReader(std::ifstream& stream, std::uint64_t fileSize) noexcept
        : m_stream(stream),
          m_fileSize(fileSize) {}

    template <typename T>
    Result<T> readValue() {
        T value{};
        const Result<void> result = readBytes(reinterpret_cast<char*>(&value), sizeof(T));
        if (!result.hasValue()) {
            return Result<T>::failure(result.error());
        }
        return Result<T>::success(value);
    }

    Result<void> readBytes(char* destination, std::size_t count) {
        std::uint64_t count64 = 0U;
        if (!checkedSizeToUint64(count, count64) || count64 > m_fileSize - m_offset) {
            return Result<void>::failure(corruptDataError("Grid run file is truncated."));
        }
        m_stream.read(destination, static_cast<std::streamsize>(count));
        if (!m_stream) {
            if (m_stream.bad()) {
                return Result<void>::failure(readError("Operating system I/O failed while reading a grid run file."));
            }
            return Result<void>::failure(corruptDataError("Grid run file ended before its declared data."));
        }
        m_offset += count64;
        return Result<void>::success();
    }

    std::uint64_t remainingBytes() const noexcept {
        return m_fileSize - m_offset;
    }

private:
    std::ifstream& m_stream;
    std::uint64_t m_fileSize{0U};
    std::uint64_t m_offset{0U};
};

bool writeBucket(BinaryWriter& writer, const GridBucket& bucket) noexcept {
    std::uint64_t pointCount = 0U;
    if (!checkedSizeToUint64(bucket.points.positions.size(), pointCount)) {
        return false;
    }

    if (!writer.writeValue(bucket.key.x) ||
        !writer.writeValue(bucket.key.y) ||
        !writer.writeValue(bucket.key.z) ||
        !writer.writeValue(bucket.points.schema.mask) ||
        !writer.writeValue(pointCount)) {
        return false;
    }

    for (const glm::dvec3& position : bucket.points.positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
            !writer.writeValue(position.x) || !writer.writeValue(position.y) || !writer.writeValue(position.z)) {
            return false;
        }
    }
    if (bucket.points.schema.hasColor()) {
        for (const std::uint32_t color : bucket.points.colorsRgba8) {
            if (!writer.writeValue(color)) {
                return false;
            }
        }
    }
    if (bucket.points.schema.hasIntensity()) {
        for (const std::uint16_t intensity : bucket.points.intensities) {
            if (!writer.writeValue(intensity)) {
                return false;
            }
        }
    }
    for (const std::uint64_t sourceIndex : bucket.sourceIndices) {
        if (!writer.writeValue(sourceIndex)) {
            return false;
        }
    }
    return true;
}

Result<GridBucket> readBucket(BinaryReader& reader, tasks::CancellationToken token) {
    if (token.isCancellationRequested()) {
        return Result<GridBucket>::failure(cancelledError());
    }

    const auto x = reader.readValue<std::int64_t>();
    const auto y = reader.readValue<std::int64_t>();
    const auto z = reader.readValue<std::int64_t>();
    const auto schemaMask = reader.readValue<std::uint32_t>();
    const auto pointCountResult = reader.readValue<std::uint64_t>();
    if (!x.hasValue()) return Result<GridBucket>::failure(x.error());
    if (!y.hasValue()) return Result<GridBucket>::failure(y.error());
    if (!z.hasValue()) return Result<GridBucket>::failure(z.error());
    if (!schemaMask.hasValue()) return Result<GridBucket>::failure(schemaMask.error());
    if (!pointCountResult.hasValue()) return Result<GridBucket>::failure(pointCountResult.error());

    const AttributeSchema schema{schemaMask.value()};
    const std::uint64_t pointCount = pointCountResult.value();
    if (!hasValidSchema(schema)) {
        return Result<GridBucket>::failure(corruptDataError(
            "Grid run file contains an unsupported attribute schema."));
    }
    if (!hasVectorCapacityFor(pointCount)) {
        return Result<GridBucket>::failure(corruptDataError(
            "Grid run file point count cannot be represented by local vectors."));
    }

    std::uint64_t payloadBytes = 0U;
    if (!checkedBucketPayloadSize(schema, pointCount, payloadBytes) ||
        payloadBytes < kBucketFixedBytes ||
        payloadBytes - kBucketFixedBytes > reader.remainingBytes()) {
        return Result<GridBucket>::failure(corruptDataError(
            "Grid run file bucket payload length is invalid or truncated."));
    }

    const std::size_t count = static_cast<std::size_t>(pointCount);
    GridBucket bucket;
    bucket.key = GridCellKey{x.value(), y.value(), z.value()};
    bucket.points.schema = schema;
    try {
        bucket.points.positions.resize(count);
        if (schema.hasColor()) {
            bucket.points.colorsRgba8.resize(count);
        }
        if (schema.hasIntensity()) {
            bucket.points.intensities.resize(count);
        }
        bucket.sourceIndices.resize(count);
    } catch (const std::bad_alloc&) {
        return Result<GridBucket>::failure(resourceError(
            "Grid run file requires more memory than is available."));
    }

    for (glm::dvec3& position : bucket.points.positions) {
        if (token.isCancellationRequested()) {
            return Result<GridBucket>::failure(cancelledError());
        }
        const auto positionX = reader.readValue<double>();
        const auto positionY = reader.readValue<double>();
        const auto positionZ = reader.readValue<double>();
        if (!positionX.hasValue()) return Result<GridBucket>::failure(positionX.error());
        if (!positionY.hasValue()) return Result<GridBucket>::failure(positionY.error());
        if (!positionZ.hasValue()) return Result<GridBucket>::failure(positionZ.error());
        if (!std::isfinite(positionX.value()) ||
            !std::isfinite(positionY.value()) ||
            !std::isfinite(positionZ.value())) {
            return Result<GridBucket>::failure(corruptDataError(
                "Grid run file contains a non-finite point position."));
        }
        position = glm::dvec3{positionX.value(), positionY.value(), positionZ.value()};
    }
    for (std::uint32_t& color : bucket.points.colorsRgba8) {
        if (token.isCancellationRequested()) {
            return Result<GridBucket>::failure(cancelledError());
        }
        const auto value = reader.readValue<std::uint32_t>();
        if (!value.hasValue()) return Result<GridBucket>::failure(value.error());
        color = value.value();
    }
    for (std::uint16_t& intensity : bucket.points.intensities) {
        if (token.isCancellationRequested()) {
            return Result<GridBucket>::failure(cancelledError());
        }
        const auto value = reader.readValue<std::uint16_t>();
        if (!value.hasValue()) return Result<GridBucket>::failure(value.error());
        intensity = value.value();
    }
    for (std::uint64_t& sourceIndex : bucket.sourceIndices) {
        if (token.isCancellationRequested()) {
            return Result<GridBucket>::failure(cancelledError());
        }
        const auto value = reader.readValue<std::uint64_t>();
        if (!value.hasValue()) return Result<GridBucket>::failure(value.error());
        sourceIndex = value.value();
    }

    const Result<void> validation = bucket.points.validate();
    if (!validation.hasValue() || !hasStrictlyIncreasingSourceIndices(bucket.sourceIndices)) {
        return Result<GridBucket>::failure(corruptDataError(
            "Grid run file bucket streams are inconsistent."));
    }
    return Result<GridBucket>::success(std::move(bucket));
}

std::atomic<std::uint64_t> g_nextRunId{0U};

} // namespace

class GridRunFile::Impl final {
public:
    std::filesystem::path temporaryPath;
    std::filesystem::path completedPath;
    bool written{false};
    bool completed{false};
    bool discarded{false};
};

GridRunFile::GridRunFile(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}

GridRunFile::GridRunFile(GridRunFile&& other) noexcept = default;

GridRunFile& GridRunFile::operator=(GridRunFile&& other) noexcept {
    if (this != &other) {
        if (m_impl != nullptr && !m_impl->completed) {
            discard();
        }
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

GridRunFile::~GridRunFile() {
    if (m_impl != nullptr && !m_impl->completed) {
        discard();
    }
}

Result<GridRunFile> GridRunFile::create(const std::filesystem::path& directory) {
    std::error_code errorCode;
    if (directory.empty() || !std::filesystem::is_directory(directory, errorCode) || errorCode) {
        return Result<GridRunFile>::failure(openError(
            "Grid run directory must exist and be a directory."));
    }

    for (std::uint64_t attempt = 0U; attempt < 128U; ++attempt) {
        const std::uint64_t sequence = g_nextRunId.fetch_add(1U, std::memory_order_relaxed);
        const std::uint64_t timestamp = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const std::string name = "grid_run_" + std::to_string(timestamp) + "_" +
            std::to_string(sequence) + ".dzgrun";
        const std::filesystem::path completedPath = directory / name;
        const std::filesystem::path temporaryPath = completedPath.string() + ".pending";

        errorCode.clear();
        if (std::filesystem::exists(temporaryPath, errorCode) || errorCode ||
            std::filesystem::exists(completedPath, errorCode) || errorCode) {
            continue;
        }

        std::ofstream stream(temporaryPath, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!stream) {
            continue;
        }
        stream.close();
        if (!stream) {
            std::filesystem::remove(temporaryPath, errorCode);
            continue;
        }

        auto impl = std::make_unique<Impl>();
        impl->temporaryPath = temporaryPath;
        impl->completedPath = completedPath;
        return Result<GridRunFile>::success(GridRunFile(std::move(impl)));
    }

    return Result<GridRunFile>::failure(openError(
        "A unique temporary grid run file could not be created."));
}

Result<void> GridRunFile::write(
    const std::vector<GridBucket>& buckets,
    tasks::CancellationToken token) {
    if (m_impl == nullptr || m_impl->discarded || m_impl->completed || m_impl->written) {
        return Result<void>::failure(invalidStateError(
            "Grid run files can be written exactly once before completion."));
    }
    if (token.isCancellationRequested()) {
        discard();
        return Result<void>::failure(cancelledError());
    }

    const Result<void> validation = validateBucketsForWrite(buckets);
    if (!validation.hasValue()) {
        discard();
        return validation;
    }

    std::uint64_t bucketCount = 0U;
    if (!checkedSizeToUint64(buckets.size(), bucketCount)) {
        discard();
        return Result<void>::failure(corruptDataError(
            "Grid bucket count cannot be represented in the run format."));
    }

    std::ofstream stream(m_impl->temporaryPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream) {
        discard();
        return Result<void>::failure(writeError("Temporary grid run file could not be opened for writing."));
    }

    BinaryWriter writer(stream);
    if (!writer.writeBytes(kMagic.data(), kMagic.size()) ||
        !writer.writeValue(kFormatVersion) ||
        !writer.writeValue(bucketCount)) {
        stream.close();
        discard();
        return Result<void>::failure(writeError("Grid run file header could not be written."));
    }

    for (const GridBucket& bucket : buckets) {
        if (token.isCancellationRequested()) {
            stream.close();
            discard();
            return Result<void>::failure(cancelledError());
        }
        if (!writeBucket(writer, bucket)) {
            stream.close();
            discard();
            return Result<void>::failure(writeError("Grid run file bucket data could not be written."));
        }
    }
    if (!writer.writeValue(kEndMarker)) {
        stream.close();
        discard();
        return Result<void>::failure(writeError("Grid run file end marker could not be written."));
    }

    stream.flush();
    if (!stream) {
        stream.close();
        discard();
        return Result<void>::failure(writeError("Grid run file could not be flushed."));
    }
    stream.close();
    if (!stream) {
        discard();
        return Result<void>::failure(writeError("Grid run file could not be closed after writing."));
    }

    m_impl->written = true;
    return Result<void>::success();
}

Result<void> GridRunFile::complete() noexcept {
    if (m_impl == nullptr || m_impl->discarded) {
        return Result<void>::failure(invalidStateError("Grid run file is no longer available."));
    }
    if (m_impl->completed) {
        return Result<void>::success();
    }
    if (!m_impl->written) {
        return Result<void>::failure(invalidStateError(
            "Grid run file must be written before it can be completed."));
    }

    std::error_code errorCode;
    std::filesystem::rename(m_impl->temporaryPath, m_impl->completedPath, errorCode);
    if (errorCode) {
        discard();
        return Result<void>::failure(writeError("Grid run file could not be promoted atomically."));
    }

    m_impl->completed = true;
    return Result<void>::success();
}

Result<std::vector<GridBucket>> GridRunFile::read(tasks::CancellationToken token) const {
    if (m_impl == nullptr || m_impl->discarded || !m_impl->completed) {
        return Result<std::vector<GridBucket>>::failure(invalidStateError(
            "Only a completed grid run file can be read."));
    }
    if (token.isCancellationRequested()) {
        discard();
        return Result<std::vector<GridBucket>>::failure(cancelledError());
    }

    std::error_code errorCode;
    const std::uintmax_t nativeSize = std::filesystem::file_size(m_impl->completedPath, errorCode);
    if (errorCode || nativeSize > std::numeric_limits<std::uint64_t>::max()) {
        discard();
        return Result<std::vector<GridBucket>>::failure(readError(
            "Grid run file size could not be determined."));
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(nativeSize);
    if (fileSize < kHeaderBytes + kFooterBytes) {
        discard();
        return Result<std::vector<GridBucket>>::failure(corruptDataError("Grid run file is too short."));
    }

    std::ifstream stream(m_impl->completedPath, std::ios::binary | std::ios::in);
    if (!stream) {
        discard();
        return Result<std::vector<GridBucket>>::failure(readError("Grid run file could not be opened."));
    }

    BinaryReader reader(stream, fileSize);
    std::array<char, kMagic.size()> magic{};
    const Result<void> magicResult = reader.readBytes(magic.data(), magic.size());
    const auto version = reader.readValue<std::uint32_t>();
    const auto bucketCountResult = reader.readValue<std::uint64_t>();
    if (!magicResult.hasValue()) { stream.close(); discard(); return Result<std::vector<GridBucket>>::failure(magicResult.error()); }
    if (!version.hasValue()) { stream.close(); discard(); return Result<std::vector<GridBucket>>::failure(version.error()); }
    if (!bucketCountResult.hasValue()) { stream.close(); discard(); return Result<std::vector<GridBucket>>::failure(bucketCountResult.error()); }
    if (magic != kMagic || version.value() != kFormatVersion) {
        stream.close();
        discard();
        return Result<std::vector<GridBucket>>::failure(corruptDataError(
            "Grid run file magic or format version is unsupported."));
    }

    const std::uint64_t bucketCount = bucketCountResult.value();
    if (reader.remainingBytes() < kFooterBytes ||
        bucketCount > static_cast<std::uint64_t>(std::vector<GridBucket>().max_size()) ||
        bucketCount > (reader.remainingBytes() - kFooterBytes) / kBucketFixedBytes) {
        stream.close();
        discard();
        return Result<std::vector<GridBucket>>::failure(corruptDataError(
            "Grid run file bucket count is invalid."));
    }

    std::vector<GridBucket> buckets;
    try {
        buckets.reserve(static_cast<std::size_t>(bucketCount));
    } catch (const std::bad_alloc&) {
        stream.close();
        discard();
        return Result<std::vector<GridBucket>>::failure(resourceError(
            "Grid run file requires more memory than is available."));
    }

    for (std::uint64_t index = 0U; index < bucketCount; ++index) {
        if (token.isCancellationRequested()) {
            stream.close();
            discard();
            return Result<std::vector<GridBucket>>::failure(cancelledError());
        }
        const Result<GridBucket> bucket = readBucket(reader, token);
        if (!bucket.hasValue()) {
            stream.close();
            discard();
            return Result<std::vector<GridBucket>>::failure(bucket.error());
        }
        if (!buckets.empty() && !(buckets.back().key < bucket.value().key)) {
            stream.close();
            discard();
            return Result<std::vector<GridBucket>>::failure(corruptDataError(
                "Grid run file buckets are not in strictly increasing GridCellKey order."));
        }
        buckets.push_back(bucket.value());
    }

    const auto endMarker = reader.readValue<std::uint64_t>();
    if (!endMarker.hasValue() || endMarker.value() != kEndMarker || reader.remainingBytes() != 0U) {
        stream.close();
        discard();
        return Result<std::vector<GridBucket>>::failure(corruptDataError(
            "Grid run file end marker or trailing data is invalid."));
    }
    stream.close();
    if (!stream) {
        discard();
        return Result<std::vector<GridBucket>>::failure(readError("Grid run file could not be closed after reading."));
    }
    return Result<std::vector<GridBucket>>::success(std::move(buckets));
}

const std::filesystem::path& GridRunFile::path() const noexcept {
    static const std::filesystem::path emptyPath;
    if (m_impl == nullptr) {
        return emptyPath;
    }
    return m_impl->completed ? m_impl->completedPath : m_impl->temporaryPath;
}

bool GridRunFile::isComplete() const noexcept {
    return m_impl != nullptr && m_impl->completed && !m_impl->discarded;
}

void GridRunFile::discard() const noexcept {
    if (m_impl == nullptr || m_impl->discarded) {
        return;
    }

    std::error_code errorCode;
    if (m_impl->completed) {
        std::filesystem::remove(m_impl->completedPath, errorCode);
    } else {
        std::filesystem::remove(m_impl->temporaryPath, errorCode);
    }
    m_impl->completed = false;
    m_impl->discarded = true;
}

} // namespace dzc
