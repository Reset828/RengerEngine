#include "data/chunk/GridRunMerger.h"

#include <dzc/Error.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
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

Error dataFormatError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kDataFormatCode,
        "Grid run merge input is invalid.",
        diagnosticMessage,
        "GridRunMerger"};
}

Error resourceError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::Resource,
        kResourceCode,
        "Grid run merge requires more memory than is available.",
        diagnosticMessage,
        "GridRunMerger"};
}

Error cancelledError() {
    return Error{
        ErrorDomain::Task,
        kCancelledCode,
        "Grid run merge operation cancelled.",
        "GridRunMerger observed a requested cancellation.",
        "GridRunMerger"};
}

bool hasValidSchema(const AttributeSchema& schema) noexcept {
    return schema.hasPosition() &&
        (schema.mask & ~kSupportedSchemaMask) == 0U;
}

bool hasStrictlyIncreasingSourceIndices(
    const std::vector<std::uint64_t>& sourceIndices) noexcept {
    for (std::size_t index = 1U; index < sourceIndices.size(); ++index) {
        if (sourceIndices[index - 1U] >= sourceIndices[index]) {
            return false;
        }
    }
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

bool hasVectorCapacityFor(std::uint64_t count) noexcept {
    return count <= static_cast<std::uint64_t>(std::vector<glm::dvec3>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint32_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint16_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint64_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

struct PointRecord final {
    glm::dvec3 position;
    std::uint32_t color{0U};
    std::uint16_t intensity{0U};
    std::uint64_t sourceIndex{0U};
};

using CellRecords = std::map<GridCellKey, std::vector<PointRecord>>;

bool sortCellBySourceIndex(
    std::vector<PointRecord>& cell,
    const tasks::CancellationToken& token) {
    if (cell.size() < 2U) {
        return !token.isCancellationRequested();
    }

    std::vector<PointRecord> scratch(cell.size());
    for (std::size_t width = 1U;;) {
        if (token.isCancellationRequested()) {
            return false;
        }
        for (std::size_t left = 0U; left < cell.size();) {
            if (token.isCancellationRequested()) {
                return false;
            }
            const std::size_t middle =
                left + std::min(width, cell.size() - left);
            const std::size_t right =
                middle + std::min(width, cell.size() - middle);
            std::size_t first = left;
            std::size_t second = middle;
            std::size_t output = left;
            while (first < middle || second < right) {
                if (token.isCancellationRequested()) {
                    return false;
                }
                if (second == right ||
                    (first < middle &&
                     cell[first].sourceIndex <= cell[second].sourceIndex)) {
                    scratch[output++] = cell[first++];
                } else {
                    scratch[output++] = cell[second++];
                }
            }
            if (right == cell.size()) {
                break;
            }
            left = right;
        }
        cell.swap(scratch);
        if (width > cell.size() / 2U) {
            break;
        }
        width *= 2U;
    }
    return !token.isCancellationRequested();
}

Result<void> validateBucket(
    const GridBucket& bucket,
    const AttributeSchema* expectedSchema,
    bool& hasSchema,
    AttributeSchema& schema) {
    if (!hasValidSchema(bucket.points.schema)) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket declares an unsupported attribute schema."));
    }

    const Result<void> pointValidation = bucket.points.validate();
    if (!pointValidation.hasValue()) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket point streams do not match its declared schema."));
    }

    if (bucket.points.positions.size() != bucket.sourceIndices.size()) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket source index stream length does not match the point count."));
    }
    if (!hasStrictlyIncreasingSourceIndices(bucket.sourceIndices)) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket source indices must be strictly increasing."));
    }

    for (const glm::dvec3& position : bucket.points.positions) {
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z)) {
            return Result<void>::failure(dataFormatError(
                "Grid bucket contains a non-finite point position."));
        }
    }

    if (bucket.points.positions.empty()) {
        return Result<void>::success();
    }

    if (!hasSchema) {
        schema = bucket.points.schema;
        hasSchema = true;
    } else if (expectedSchema == nullptr || schema.mask != expectedSchema->mask ||
               schema.mask != bucket.points.schema.mask) {
        return Result<void>::failure(dataFormatError(
            "Non-empty grid buckets do not have the same attribute schema."));
    }
    return Result<void>::success();
}

} // namespace

Result<std::vector<GridBucket>> GridRunMerger::merge(
    const std::vector<std::vector<GridBucket>>& inputs,
    tasks::CancellationToken token) {
    try {
        AttributeSchema schema{};
        bool hasSchema = false;

        // Validate every input before allocating the merged output. Each input
        // group must retain the strict CellKey ordering used by GridRunFile.
        for (const std::vector<GridBucket>& input : inputs) {
            GridCellKey previousKey{};
            bool hasPreviousKey = false;
            for (const GridBucket& bucket : input) {
                if (token.isCancellationRequested()) {
                    return Result<std::vector<GridBucket>>::failure(cancelledError());
                }
                if (hasPreviousKey && !(previousKey < bucket.key)) {
                    return Result<std::vector<GridBucket>>::failure(dataFormatError(
                        "Grid bucket input is not in strictly increasing CellKey order."));
                }
                const Result<void> validation = validateBucket(
                    bucket,
                    hasSchema ? &schema : nullptr,
                    hasSchema,
                    schema);
                if (!validation.hasValue()) {
                    return Result<std::vector<GridBucket>>::failure(validation.error());
                }
                previousKey = bucket.key;
                hasPreviousKey = true;
            }
        }

        CellRecords records;
        std::uint64_t totalPointCount = 0U;
        for (const std::vector<GridBucket>& input : inputs) {
            for (const GridBucket& bucket : input) {
                if (token.isCancellationRequested()) {
                    return Result<std::vector<GridBucket>>::failure(cancelledError());
                }
                std::uint64_t nextTotal = 0U;
                const auto pointCount = static_cast<std::uint64_t>(bucket.points.positions.size());
                if (!checkedAdd(totalPointCount, pointCount, nextTotal)) {
                    return Result<std::vector<GridBucket>>::failure(dataFormatError(
                        "Grid run merge point count overflows uint64."));
                }
                totalPointCount = nextTotal;
                if (pointCount == 0U) {
                    continue;
                }

                std::vector<PointRecord>& cell = records[bucket.key];
                std::uint64_t cellSize = 0U;
                if (!checkedAdd(
                        static_cast<std::uint64_t>(cell.size()),
                        pointCount,
                        cellSize) ||
                    !hasVectorCapacityFor(cellSize)) {
                    return Result<std::vector<GridBucket>>::failure(resourceError(
                        "Merged Cell point count cannot be represented by local vectors."));
                }
                cell.reserve(static_cast<std::size_t>(cellSize));
                for (std::size_t index = 0U; index < bucket.points.positions.size(); ++index) {
                    if (token.isCancellationRequested()) {
                        return Result<std::vector<GridBucket>>::failure(cancelledError());
                    }
                    PointRecord record;
                    record.position = bucket.points.positions[index];
                    if (schema.hasColor()) {
                        record.color = bucket.points.colorsRgba8[index];
                    }
                    if (schema.hasIntensity()) {
                        record.intensity = bucket.points.intensities[index];
                    }
                    record.sourceIndex = bucket.sourceIndices[index];
                    cell.push_back(record);
                }
            }
        }

        std::vector<GridBucket> result;
        if (records.size() > static_cast<std::size_t>(result.max_size())) {
            return Result<std::vector<GridBucket>>::failure(resourceError(
                "Merged Cell count cannot be represented by the result vector."));
        }
        result.reserve(records.size());

        for (auto& entry : records) {
            if (token.isCancellationRequested()) {
                return Result<std::vector<GridBucket>>::failure(cancelledError());
            }
            std::vector<PointRecord>& cell = entry.second;
            if (!sortCellBySourceIndex(cell, token)) {
                return Result<std::vector<GridBucket>>::failure(cancelledError());
            }

            for (std::size_t index = 1U; index < cell.size(); ++index) {
                if (cell[index - 1U].sourceIndex == cell[index].sourceIndex) {
                    return Result<std::vector<GridBucket>>::failure(dataFormatError(
                        "Merged Cell contains duplicate source indices."));
                }
            }

            GridBucket bucket;
            bucket.key = entry.first;
            bucket.points.schema = schema;
            const std::size_t count = cell.size();
            if (!hasVectorCapacityFor(static_cast<std::uint64_t>(count))) {
                return Result<std::vector<GridBucket>>::failure(resourceError(
                    "Merged Cell point count cannot be represented by local vectors."));
            }
            bucket.points.positions.reserve(count);
            if (schema.hasColor()) {
                bucket.points.colorsRgba8.reserve(count);
            }
            if (schema.hasIntensity()) {
                bucket.points.intensities.reserve(count);
            }
            bucket.sourceIndices.reserve(count);
            for (const PointRecord& record : cell) {
                if (token.isCancellationRequested()) {
                    return Result<std::vector<GridBucket>>::failure(cancelledError());
                }
                bucket.points.positions.push_back(record.position);
                if (schema.hasColor()) {
                    bucket.points.colorsRgba8.push_back(record.color);
                }
                if (schema.hasIntensity()) {
                    bucket.points.intensities.push_back(record.intensity);
                }
                bucket.sourceIndices.push_back(record.sourceIndex);
            }
            result.push_back(std::move(bucket));
        }

        return Result<std::vector<GridBucket>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::vector<GridBucket>>::failure(resourceError(
            "Grid run merge requires more memory than is available."));
    } catch (const std::length_error&) {
        return Result<std::vector<GridBucket>>::failure(resourceError(
            "Grid run merge container size is not representable."));
    }
}

} // namespace dzc