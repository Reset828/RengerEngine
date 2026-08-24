#include "data/chunk/GridCellSplitter.h"

#include <dzc/Error.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint64_t kTargetPointCount = 262144U;
constexpr std::uint64_t kMaxPointCount = 524288U;
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
        "Grid Cell split input is invalid.",
        diagnosticMessage,
        "GridCellSplitter"};
}

Error resourceError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::Resource,
        kResourceCode,
        "Grid Cell split requires more memory than is available.",
        diagnosticMessage,
        "GridCellSplitter"};
}

Error cancelledError() {
    return Error{
        ErrorDomain::Task,
        kCancelledCode,
        "Grid Cell split operation cancelled.",
        "GridCellSplitter observed a requested cancellation.",
        "GridCellSplitter"};
}

bool hasValidSchema(const AttributeSchema& schema) noexcept {
    return schema.hasPosition() && (schema.mask & ~kSupportedSchemaMask) == 0U;
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

bool checkedMultiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool hasVectorCapacityFor(std::uint64_t count) noexcept {
    return count <= static_cast<std::uint64_t>(std::vector<glm::dvec3>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint32_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint16_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::vector<std::uint64_t>().max_size()) &&
        count <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

Result<void> validateBucket(const GridBucket& bucket) {
    if (!hasValidSchema(bucket.points.schema)) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket declares an unsupported schema or does not declare Position."));
    }

    if (!bucket.points.validate().hasValue()) {
        return Result<void>::failure(dataFormatError(
            "Grid bucket point streams do not match the declared schema."));
    }

    const std::size_t pointCount = bucket.points.positions.size();
    if (pointCount != bucket.sourceIndices.size()) {
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

    if (static_cast<std::uint64_t>(pointCount) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        !hasVectorCapacityFor(static_cast<std::uint64_t>(pointCount))) {
        return Result<void>::failure(resourceError(
            "Grid bucket point count cannot be represented by output vectors."));
    }
    return Result<void>::success();
}

struct AxisSpan final {
    int exponent{std::numeric_limits<int>::min()};
    double mantissa{0.0};
};

bool checkedPositiveDifference(double maximum, double minimum, double& result) noexcept {
    result = maximum - minimum;
    return std::isfinite(result) && result >= 0.0;
}

bool checkedPositiveSum(double first, double second, double& result) noexcept {
    if (!std::isfinite(first) || !std::isfinite(second) || first < 0.0 || second < 0.0) {
        return false;
    }
    result = first + second;
    return std::isfinite(result) && result >= 0.0;
}

AxisSpan positiveValue(double value) noexcept {
    if (value == 0.0) {
        return AxisSpan{};
    }
    int exponent = 0;
    const double mantissa = std::frexp(value, &exponent);
    return AxisSpan{exponent, mantissa};
}

bool checkedAxisSpan(
    const std::vector<std::size_t>& indices,
    const std::vector<glm::dvec3>& positions,
    int axis,
    AxisSpan& result) noexcept {
    double minimum = positions[indices.front()][axis];
    double maximum = minimum;
    for (const std::size_t index : indices) {
        const double coordinate = positions[index][axis];
        minimum = std::min(minimum, coordinate);
        maximum = std::max(maximum, coordinate);
    }

    double span = 0.0;
    if (minimum >= 0.0) {
        if (!checkedPositiveDifference(maximum, minimum, span)) {
            return false;
        }
    } else if (maximum <= 0.0) {
        double minimumMagnitude = 0.0;
        double maximumMagnitude = 0.0;
        if (!checkedPositiveDifference(-minimum, -maximum, minimumMagnitude) ||
            !std::isfinite(minimumMagnitude)) {
            return false;
        }
        span = minimumMagnitude;
    } else {
        double negativeMagnitude = 0.0;
        if (!std::isfinite(-minimum) || !checkedPositiveSum(-minimum, maximum, negativeMagnitude)) {
            return false;
        }
        span = negativeMagnitude;
    }

    if (!std::isfinite(span) || span < 0.0) {
        return false;
    }
    result = positiveValue(span);
    return true;
}

bool spanLess(const AxisSpan& left, const AxisSpan& right) noexcept {
    if (left.exponent != right.exponent) {
        return left.exponent < right.exponent;
    }
    return left.mantissa < right.mantissa;
}

bool longestAxis(
    const std::vector<std::size_t>& indices,
    const std::vector<glm::dvec3>& positions,
    int& axis) noexcept {
    AxisSpan spans[3];
    if (!checkedAxisSpan(indices, positions, 0, spans[0]) ||
        !checkedAxisSpan(indices, positions, 1, spans[1]) ||
        !checkedAxisSpan(indices, positions, 2, spans[2])) {
        return false;
    }
    axis = 0;
    if (spanLess(spans[axis], spans[1])) {
        axis = 1;
    }
    if (spanLess(spans[axis], spans[2])) {
        axis = 2;
    }
    return true;
}
bool spatialLess(
    std::size_t left,
    std::size_t right,
    int axis,
    const std::vector<glm::dvec3>& positions,
    const std::vector<std::uint64_t>& sourceIndices) noexcept {
    const double leftCoordinate = positions[left][axis];
    const double rightCoordinate = positions[right][axis];
    if (leftCoordinate != rightCoordinate) {
        return leftCoordinate < rightCoordinate;
    }
    return sourceIndices[left] < sourceIndices[right];
}

bool sortSpatial(
    std::vector<std::size_t>& indices,
    int axis,
    const std::vector<glm::dvec3>& positions,
    const std::vector<std::uint64_t>& sourceIndices,
    const tasks::CancellationToken& token) {
    if (indices.size() < 2U) {
        return !token.isCancellationRequested();
    }

    std::vector<std::size_t> scratch(indices.size());
    for (std::size_t width = 1U;;) {
        if (token.isCancellationRequested()) {
            return false;
        }
        for (std::size_t left = 0U; left < indices.size();) {
            if (token.isCancellationRequested()) {
                return false;
            }
            const std::size_t middle = left + std::min(width, indices.size() - left);
            const std::size_t right = middle + std::min(width, indices.size() - middle);
            std::size_t first = left;
            std::size_t second = middle;
            std::size_t output = left;
            while (first < middle || second < right) {
                if (token.isCancellationRequested()) {
                    return false;
                }
                if (second == right ||
                    (first < middle && !spatialLess(
                        indices[second], indices[first], axis, positions, sourceIndices))) {
                    scratch[output++] = indices[first++];
                } else {
                    scratch[output++] = indices[second++];
                }
            }
            if (right == indices.size()) {
                break;
            }
            left = right;
        }
        indices.swap(scratch);
        if (width > indices.size() / 2U) {
            break;
        }
        width *= 2U;
    }
    return !token.isCancellationRequested();
}

bool sortBySourceIndex(
    std::vector<std::size_t>& indices,
    const tasks::CancellationToken& token) {
    if (token.isCancellationRequested()) {
        return false;
    }
    std::sort(indices.begin(), indices.end());
    return !token.isCancellationRequested();
}

bool checkedPartitionCount(
    std::uint64_t pointCount,
    std::uint64_t& partitionCount) noexcept {
    const std::uint64_t quotient = pointCount / kTargetPointCount;
    const std::uint64_t remainder = pointCount % kTargetPointCount;
    if (remainder == 0U) {
        partitionCount = quotient;
        return true;
    }
    if (quotient == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    partitionCount = quotient + 1U;
    return true;
}

Result<GridBucket> copyBucketPart(
    const GridBucket& source,
    const std::vector<std::size_t>& indices,
    const tasks::CancellationToken& token) {
    if (token.isCancellationRequested()) {
        return Result<GridBucket>::failure(cancelledError());
    }
    const std::uint64_t count = static_cast<std::uint64_t>(indices.size());
    if (!hasVectorCapacityFor(count)) {
        return Result<GridBucket>::failure(resourceError(
            "Grid Cell split output point count cannot be represented by vectors."));
    }

    GridBucket result;
    result.key = source.key;
    result.points.schema = source.points.schema;
    result.points.positions.reserve(indices.size());
    if (source.points.schema.hasColor()) {
        result.points.colorsRgba8.reserve(indices.size());
    }
    if (source.points.schema.hasIntensity()) {
        result.points.intensities.reserve(indices.size());
    }
    result.sourceIndices.reserve(indices.size());
    for (const std::size_t index : indices) {
        if (token.isCancellationRequested()) {
            return Result<GridBucket>::failure(cancelledError());
        }
        result.points.positions.push_back(source.points.positions[index]);
        if (source.points.schema.hasColor()) {
            result.points.colorsRgba8.push_back(source.points.colorsRgba8[index]);
        }
        if (source.points.schema.hasIntensity()) {
            result.points.intensities.push_back(source.points.intensities[index]);
        }
        result.sourceIndices.push_back(source.sourceIndices[index]);
    }
    return Result<GridBucket>::success(std::move(result));
}

} // namespace

Result<std::vector<GridBucket>> GridCellSplitter::split(
    const GridBucket& bucket,
    tasks::CancellationToken token) {
    try {
        if (token.isCancellationRequested()) {
            return Result<std::vector<GridBucket>>::failure(cancelledError());
        }

        const Result<void> validation = validateBucket(bucket);
        if (!validation.hasValue()) {
            return Result<std::vector<GridBucket>>::failure(validation.error());
        }

        const std::uint64_t pointCount = static_cast<std::uint64_t>(bucket.points.positions.size());
        if (pointCount == 0U) {
            return Result<std::vector<GridBucket>>::success({});
        }

        if (pointCount <= kMaxPointCount) {
            std::vector<std::size_t> allIndices(bucket.points.positions.size());
            for (std::size_t index = 0U; index < allIndices.size(); ++index) {
                allIndices[index] = index;
            }
            const auto copy = copyBucketPart(bucket, allIndices, token);
            if (!copy.hasValue()) {
                return Result<std::vector<GridBucket>>::failure(copy.error());
            }
            std::vector<GridBucket> result;
            result.reserve(1U);
            result.push_back(copy.value());
            return Result<std::vector<GridBucket>>::success(std::move(result));
        }

        std::uint64_t partitionCount64 = 0U;
        if (!checkedPartitionCount(pointCount, partitionCount64) || partitionCount64 == 0U) {
            return Result<std::vector<GridBucket>>::failure(dataFormatError(
                "Grid Cell partition count arithmetic is invalid."));
        }
        if (partitionCount64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            partitionCount64 > static_cast<std::uint64_t>(std::vector<std::vector<std::size_t>>().max_size())) {
            return Result<std::vector<GridBucket>>::failure(resourceError(
                "Grid Cell partition count cannot be represented by output containers."));
        }
        const std::size_t partitionCount = static_cast<std::size_t>(partitionCount64);
        std::vector<std::vector<std::size_t>> leaves;
        leaves.reserve(partitionCount);

        const std::vector<glm::dvec3>& positions = bucket.points.positions;
        const std::vector<std::uint64_t>& sourceIndices = bucket.sourceIndices;
        const auto buildPartitions = [&](auto&& self,
                                         std::vector<std::size_t> indices,
                                         std::size_t desiredLeaves) -> Result<void> {
            if (token.isCancellationRequested()) {
                return Result<void>::failure(cancelledError());
            }
            if (desiredLeaves == 1U) {
                if (!sortBySourceIndex(indices, token)) {
                    return Result<void>::failure(cancelledError());
                }
                leaves.push_back(std::move(indices));
                return Result<void>::success();
            }

            int axis = 0;
            if (!longestAxis(indices, positions, axis)) {
                return Result<void>::failure(dataFormatError(
                    "Grid Cell coordinate range is not representable as a finite double."));
            }
            if (!sortSpatial(indices, axis, positions, sourceIndices, token)) {
                return Result<void>::failure(cancelledError());
            }

            const std::size_t leftLeaves = desiredLeaves / 2U;
            const std::size_t rightLeaves = desiredLeaves - leftLeaves;
            std::uint64_t product = 0U;
            if (!checkedMultiply(
                    static_cast<std::uint64_t>(indices.size()),
                    static_cast<std::uint64_t>(leftLeaves),
                    product)) {
                return Result<void>::failure(dataFormatError(
                    "Grid Cell partition arithmetic overflows uint64."));
            }
            const std::size_t leftCount = static_cast<std::size_t>(
                product / static_cast<std::uint64_t>(desiredLeaves));
            if (leftCount == 0U || leftCount >= indices.size()) {
                return Result<void>::failure(dataFormatError(
                    "Grid Cell partition would produce an empty child."));
            }

            std::vector<std::size_t> left(indices.begin(), indices.begin() + leftCount);
            std::vector<std::size_t> right(indices.begin() + leftCount, indices.end());
            const Result<void> leftResult = self(self, std::move(left), leftLeaves);
            if (!leftResult.hasValue()) {
                return leftResult;
            }
            return self(self, std::move(right), rightLeaves);
        };

        std::vector<std::size_t> allIndices(bucket.points.positions.size());
        for (std::size_t index = 0U; index < allIndices.size(); ++index) {
            if (token.isCancellationRequested()) {
                return Result<std::vector<GridBucket>>::failure(cancelledError());
            }
            allIndices[index] = index;
        }
        const Result<void> partitionResult = buildPartitions(
            buildPartitions,
            std::move(allIndices),
            partitionCount);
        if (!partitionResult.hasValue()) {
            return Result<std::vector<GridBucket>>::failure(partitionResult.error());
        }
        if (leaves.size() != partitionCount) {
            return Result<std::vector<GridBucket>>::failure(dataFormatError(
                "Grid Cell partition produced an unexpected number of children."));
        }

        std::vector<GridBucket> result;
        if (leaves.size() > result.max_size()) {
            return Result<std::vector<GridBucket>>::failure(resourceError(
                "Grid Cell output count cannot be represented by the result vector."));
        }
        result.reserve(leaves.size());
        for (const std::vector<std::size_t>& leaf : leaves) {
            if (token.isCancellationRequested()) {
                return Result<std::vector<GridBucket>>::failure(cancelledError());
            }
            if (leaf.empty() || leaf.size() > static_cast<std::size_t>(kTargetPointCount)) {
                return Result<std::vector<GridBucket>>::failure(dataFormatError(
                    "Grid Cell partition produced an invalid child size."));
            }
            const auto child = copyBucketPart(bucket, leaf, token);
            if (!child.hasValue()) {
                return Result<std::vector<GridBucket>>::failure(child.error());
            }
            result.push_back(child.value());
        }
        return Result<std::vector<GridBucket>>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<std::vector<GridBucket>>::failure(resourceError(
            "Grid Cell split requires more memory than is available."));
    } catch (const std::length_error&) {
        return Result<std::vector<GridBucket>>::failure(resourceError(
            "Grid Cell split container size is not representable."));
    }
}

} // namespace dzc