#include "data/chunk/GridChunkBuilder.h"

#include <dzc/Error.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint32_t kDataFormatCode = 2U;
constexpr std::uint32_t kResourceCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kSupportedSchemaMask =
    static_cast<std::uint32_t>(PointAttribute::Position) |
    static_cast<std::uint32_t>(PointAttribute::Color) |
    static_cast<std::uint32_t>(PointAttribute::Intensity);
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

Error dataFormatError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kDataFormatCode,
        "Grid Chunk input is invalid.",
        diagnosticMessage,
        "GridChunkBuilder"};
}

Error resourceError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::Resource,
        kResourceCode,
        "Grid Chunk construction requires more memory than is available.",
        diagnosticMessage,
        "GridChunkBuilder"};
}

Error cancelledError() {
    return Error{
        ErrorDomain::Task,
        kCancelledCode,
        "Grid Chunk construction was cancelled.",
        "GridChunkBuilder observed a requested cancellation.",
        "GridChunkBuilder"};
}

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool hasValidSchema(const AttributeSchema& schema) noexcept {
    return schema.hasPosition() &&
        (schema.mask & ~kSupportedSchemaMask) == 0U;
}

bool checkedSize(std::size_t value, std::uint64_t& result) noexcept {
    if (value > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    result = static_cast<std::uint64_t>(value);
    return true;
}

bool checkedAdd(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

bool hasOutputCapacity(std::uint64_t count) noexcept {
    std::uint64_t positionsMaxSize = 0U;
    std::uint64_t colorsMaxSize = 0U;
    std::uint64_t intensitiesMaxSize = 0U;
    std::uint64_t chunksMaxSize = 0U;
    std::uint64_t sizeMax = 0U;
    return checkedSize(std::vector<glm::vec3>().max_size(), positionsMaxSize) &&
        checkedSize(std::vector<std::uint32_t>().max_size(), colorsMaxSize) &&
        checkedSize(std::vector<std::uint16_t>().max_size(), intensitiesMaxSize) &&
        checkedSize(std::vector<Chunk>().max_size(), chunksMaxSize) &&
        checkedSize(std::numeric_limits<std::size_t>::max(), sizeMax) &&
        count <= positionsMaxSize &&
        count <= colorsMaxSize &&
        count <= intensitiesMaxSize &&
        count <= chunksMaxSize &&
        count <= sizeMax;
}

struct PreparedGroup final {
    GridCellKey key;
    AttributeSchema schema;
    std::vector<const GridBucket*> buckets;
};

void fnvMixByte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= kFnvPrime;
}

void fnvMixUint64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        fnvMixByte(hash, static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
}

std::uint64_t makeChunkId(
    const GridCellKey& key,
    std::uint64_t subchunkIndex) noexcept {
    std::uint64_t hash = kFnvOffsetBasis;
    fnvMixUint64(hash, static_cast<std::uint64_t>(key.x));
    fnvMixUint64(hash, static_cast<std::uint64_t>(key.y));
    fnvMixUint64(hash, static_cast<std::uint64_t>(key.z));
    fnvMixUint64(hash, subchunkIndex);
    return hash;
}

bool sameSchema(
    const std::optional<AttributeSchema>& expected,
    const AttributeSchema& actual) noexcept {
    return !expected.has_value() || expected->mask == actual.mask;
}

Result<void> validateBucket(
    const GridBucket& bucket,
    const std::optional<AttributeSchema>& expectedSchema,
    std::set<std::uint64_t>& sourceIndices,
    const tasks::CancellationToken& token) {
    if (token.isCancellationRequested()) {
        return Result<void>::failure(cancelledError());
    }

    if (!hasValidSchema(bucket.points.schema)) {
        return Result<void>::failure(dataFormatError(
            "GridBucket schema must declare Position and contain only supported attributes."));
    }
    if (!sameSchema(expectedSchema, bucket.points.schema)) {
        return Result<void>::failure(dataFormatError(
            "All non-empty GridBuckets must use the same AttributeSchema."));
    }

    const Result<void> pointBatchValidation = bucket.points.validate();
    if (!pointBatchValidation.hasValue()) {
        return Result<void>::failure(dataFormatError(
            "GridBucket PointBatch streams do not match the declared schema."));
    }

    std::uint64_t pointCount = 0U;
    if (!checkedSize(bucket.points.positions.size(), pointCount) ||
        !hasOutputCapacity(pointCount)) {
        return Result<void>::failure(resourceError(
            "GridBucket point count cannot be represented by Chunk CPU vectors."));
    }
    if (bucket.sourceIndices.size() != bucket.points.positions.size()) {
        return Result<void>::failure(dataFormatError(
            "GridBucket sourceIndices length must equal the Position point count."));
    }

    for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
        if ((index & 0x3ffU) == 0U && token.isCancellationRequested()) {
            return Result<void>::failure(cancelledError());
        }
        if (!isFinite(bucket.points.positions[index])) {
            return Result<void>::failure(dataFormatError(
                "GridBucket contains a non-finite Position."));
        }
        if (index > 0U && bucket.sourceIndices[index - 1U] >= bucket.sourceIndices[index]) {
            return Result<void>::failure(dataFormatError(
                "GridBucket sourceIndices must be strictly increasing."));
        }
        if (!sourceIndices.insert(bucket.sourceIndices[index]).second) {
            return Result<void>::failure(dataFormatError(
                "A Cell group contains duplicate sourceIndices across subchunks."));
        }
    }
    return Result<void>::success();
}

Result<Chunk> buildChunk(
    const GridBucket& bucket,
    ChunkId id,
    const tasks::CancellationToken& token) {
    if (token.isCancellationRequested()) {
        return Result<Chunk>::failure(cancelledError());
    }

    Bounds3d bounds;
    for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
        if ((index & 0x3ffU) == 0U && token.isCancellationRequested()) {
            return Result<Chunk>::failure(cancelledError());
        }
        const Result<void> extendResult = bounds.extend(bucket.points.positions[index]);
        if (!extendResult.hasValue()) {
            return Result<Chunk>::failure(dataFormatError(
                "Chunk bounds could not be built from the input Position stream."));
        }
    }

    const Result<glm::dvec3> centerResult = bounds.center();
    if (!centerResult.hasValue() || !isFinite(centerResult.value())) {
        return Result<Chunk>::failure(dataFormatError(
            "Chunk bounds center is not finite."));
    }
    const glm::dvec3 origin = centerResult.value();

    ChunkCpuData cpuData;
    const std::uint64_t pointCount = static_cast<std::uint64_t>(bucket.points.positions.size());
    if (!hasOutputCapacity(pointCount)) {
        return Result<Chunk>::failure(resourceError(
            "Chunk CPU data point count cannot be represented by output vectors."));
    }
    cpuData.positions.reserve(bucket.points.positions.size());
    if (bucket.points.schema.hasColor()) {
        cpuData.colorsRgba8.reserve(bucket.points.colorsRgba8.size());
    }
    if (bucket.points.schema.hasIntensity()) {
        cpuData.intensities.reserve(bucket.points.intensities.size());
    }

    for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
        if ((index & 0x3ffU) == 0U && token.isCancellationRequested()) {
            return Result<Chunk>::failure(cancelledError());
        }
        const glm::dvec3 localDouble = bucket.points.positions[index] - origin;
        if (!isFinite(localDouble)) {
            return Result<Chunk>::failure(dataFormatError(
                "Chunk local Position calculation is not finite."));
        }
        const glm::vec3 localPosition{
            static_cast<float>(localDouble.x),
            static_cast<float>(localDouble.y),
            static_cast<float>(localDouble.z)};
        if (!std::isfinite(localPosition.x) ||
            !std::isfinite(localPosition.y) ||
            !std::isfinite(localPosition.z)) {
            return Result<Chunk>::failure(dataFormatError(
                "Chunk local Position cannot be represented as finite float values."));
        }
        cpuData.positions.push_back(localPosition);
        if (bucket.points.schema.hasColor()) {
            cpuData.colorsRgba8.push_back(bucket.points.colorsRgba8[index]);
        }
        if (bucket.points.schema.hasIntensity()) {
            cpuData.intensities.push_back(bucket.points.intensities[index]);
        }
    }

    ChunkMetadata metadata;
    metadata.id = id;
    metadata.pointCount = pointCount;
    metadata.bounds = bounds;
    metadata.origin = origin;
    metadata.schema = bucket.points.schema;

    const Result<Chunk> created = Chunk::create(std::move(metadata));
    if (!created.hasValue()) {
        return Result<Chunk>::failure(created.error());
    }
    Chunk chunk = created.value();
    const Result<void> beginResult = chunk.beginCpuLoad();
    if (!beginResult.hasValue()) {
        return Result<Chunk>::failure(beginResult.error());
    }
    const Result<void> completeResult = chunk.completeCpuLoad(std::move(cpuData));
    if (!completeResult.hasValue()) {
        return Result<Chunk>::failure(completeResult.error());
    }
    return Result<Chunk>::success(std::move(chunk));
}

} // namespace

Result<std::vector<Chunk>> GridChunkBuilder::build(
    const std::vector<std::vector<GridBucket>>& groups,
    tasks::CancellationToken token) {
    try {
        if (token.isCancellationRequested()) {
            return Result<std::vector<Chunk>>::failure(cancelledError());
        }

        std::vector<PreparedGroup> preparedGroups;
        std::map<GridCellKey, std::size_t> groupByKey;
        std::optional<AttributeSchema> expectedSchema;
        std::uint64_t totalPointCount = 0U;

        for (const std::vector<GridBucket>& inputGroup : groups) {
            if (token.isCancellationRequested()) {
                return Result<std::vector<Chunk>>::failure(cancelledError());
            }

            PreparedGroup prepared;
            bool hasKey = false;
            std::set<std::uint64_t> groupSourceIndices;
            for (const GridBucket& bucket : inputGroup) {
                if (bucket.points.positions.empty()) {
                    continue;
                }
                if (!hasKey) {
                    prepared.key = bucket.key;
                    hasKey = true;
                } else if (prepared.key != bucket.key) {
                    return Result<std::vector<Chunk>>::failure(dataFormatError(
                        "Each non-empty input group must contain exactly one GridCellKey."));
                }
                if (!expectedSchema.has_value()) {
                    expectedSchema = bucket.points.schema;
                }
                const Result<void> validation = validateBucket(
                    bucket,
                    expectedSchema,
                    groupSourceIndices,
                    token);
                if (!validation.hasValue()) {
                    return Result<std::vector<Chunk>>::failure(validation.error());
                }
                std::uint64_t nextTotal = 0U;
                std::uint64_t bucketPointCount = 0U;
                if (!checkedSize(bucket.points.positions.size(), bucketPointCount) ||
                    !checkedAdd(totalPointCount, bucketPointCount, nextTotal)) {
                    return Result<std::vector<Chunk>>::failure(dataFormatError(
                        "Total Chunk point count overflows uint64."));
                }
                totalPointCount = nextTotal;
                prepared.buckets.push_back(&bucket);
            }

            if (!hasKey) {
                continue;
            }
            if (!groupByKey.emplace(prepared.key, preparedGroups.size()).second) {
                return Result<std::vector<Chunk>>::failure(dataFormatError(
                    "The same GridCellKey must not occur in multiple input groups."));
            }
            preparedGroups.push_back(std::move(prepared));
        }

        std::sort(
            preparedGroups.begin(),
            preparedGroups.end(),
            [](const PreparedGroup& left, const PreparedGroup& right) {
                return left.key < right.key;
            });

        std::uint64_t outputCount = 0U;
        for (const PreparedGroup& group : preparedGroups) {
            std::uint64_t nextOutputCount = 0U;
            std::uint64_t groupCount = 0U;
            if (!checkedSize(group.buckets.size(), groupCount)) {
                return Result<std::vector<Chunk>>::failure(dataFormatError(
                    "Chunk subchunk count cannot be represented by uint64."));
            }
            if (!checkedAdd(outputCount, groupCount, nextOutputCount)) {
                return Result<std::vector<Chunk>>::failure(dataFormatError(
                    "Total Chunk count overflows uint64."));
            }
            if (!hasOutputCapacity(nextOutputCount)) {
                return Result<std::vector<Chunk>>::failure(resourceError(
                    "Chunk output count cannot be represented by the result vector."));
            }
            outputCount = nextOutputCount;
        }

        std::map<std::uint64_t, std::pair<GridCellKey, std::uint64_t>> generatedIds;
        for (const PreparedGroup& group : preparedGroups) {
            for (std::size_t bucketIndex = 0U; bucketIndex < group.buckets.size(); ++bucketIndex) {
                if (token.isCancellationRequested()) {
                    return Result<std::vector<Chunk>>::failure(cancelledError());
                }
                std::uint64_t subchunkIndex = 0U;
                if (!checkedSize(bucketIndex, subchunkIndex)) {
                    return Result<std::vector<Chunk>>::failure(dataFormatError(
                        "Chunk subchunk index cannot be represented by uint64."));
                }
                const std::uint64_t idValue = makeChunkId(group.key, subchunkIndex);
                const auto inserted = generatedIds.emplace(
                    idValue,
                    std::make_pair(group.key, subchunkIndex));
                if (!inserted.second) {
                    return Result<std::vector<Chunk>>::failure(dataFormatError(
                        "Deterministic ChunkId generation produced a collision."));
                }
            }
        }

        std::vector<Chunk> result;
        result.reserve(static_cast<std::size_t>(outputCount));
        for (const PreparedGroup& group : preparedGroups) {
            for (std::size_t bucketIndex = 0U; bucketIndex < group.buckets.size(); ++bucketIndex) {
                if (token.isCancellationRequested()) {
                    return Result<std::vector<Chunk>>::failure(cancelledError());
                }
                std::uint64_t subchunkIndex = 0U;
                if (!checkedSize(bucketIndex, subchunkIndex)) {
                    return Result<std::vector<Chunk>>::failure(dataFormatError(
                        "Chunk subchunk index cannot be represented by uint64."));
                }
                const ChunkId id{makeChunkId(group.key, subchunkIndex)};
                const Result<Chunk> chunkResult = buildChunk(
                    *group.buckets[bucketIndex],
                    id,
                    token);
                if (!chunkResult.hasValue()) {
                    return Result<std::vector<Chunk>>::failure(chunkResult.error());
                }
                result.push_back(chunkResult.value());
            }
        }
        return Result<std::vector<Chunk>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::vector<Chunk>>::failure(resourceError(
            "Grid Chunk construction requires more memory than is available."));
    } catch (const std::length_error&) {
        return Result<std::vector<Chunk>>::failure(resourceError(
            "Grid Chunk container size is not representable."));
    }
}

} // namespace dzc
