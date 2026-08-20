#include "data/chunk/PointBatch.h"

#include <cstdint>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

Error invalidPointBatchError(const char* diagnosticMessage) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Point batch structure is invalid",
        diagnosticMessage,
        "PointBatch::validate"};
}

} // namespace

Result<void> PointBatch::validate() const noexcept {
    if (!schema.hasPosition()) {
        return Result<void>::failure(invalidPointBatchError(
            "PointBatch requires the Position attribute."));
    }

    const std::size_t pointCount = positions.size();

    if (schema.hasColor()) {
        if (colorsRgba8.size() != pointCount) {
            return Result<void>::failure(invalidPointBatchError(
                "PointBatch Color stream length does not match the point count."));
        }
    } else if (!colorsRgba8.empty()) {
        return Result<void>::failure(invalidPointBatchError(
            "PointBatch contains Color data without declaring the Color attribute."));
    }

    if (schema.hasIntensity()) {
        if (intensities.size() != pointCount) {
            return Result<void>::failure(invalidPointBatchError(
                "PointBatch Intensity stream length does not match the point count."));
        }
    } else if (!intensities.empty()) {
        return Result<void>::failure(invalidPointBatchError(
            "PointBatch contains Intensity data without declaring the Intensity attribute."));
    }

    return Result<void>::success();
}

} // namespace dzc
