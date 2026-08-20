#include "data/chunk/IntensityQuantizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr double kQuantizationMaximum = 65535.0;

bool isValidSourceRange(const IntensitySourceRange& range) noexcept {
    return std::isfinite(range.minimum) && std::isfinite(range.maximum) &&
           range.minimum <= range.maximum;
}

Error invalidSourceRangeError() {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Intensity source range is invalid",
        "IntensitySourceRange requires finite, ordered minimum and maximum values.",
        "IntensityQuantizer::quantize"};
}

std::uint16_t quantizeValue(double value, double minimum, double maximum) noexcept {
    const double normalized = (value - minimum) / (maximum - minimum);
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<std::uint16_t>(std::round(clamped * kQuantizationMaximum));
}

} // namespace

Result<IntensityQuantizationResult> IntensityQuantizer::quantize(
    const std::vector<double>& sourceValues,
    std::optional<IntensitySourceRange> sourceRange) noexcept {
    if (sourceRange.has_value() && !isValidSourceRange(*sourceRange)) {
        return Result<IntensityQuantizationResult>::failure(invalidSourceRangeError());
    }

    IntensityQuantizationResult result{};
    result.values.resize(sourceValues.size(), 0U);

    double validMinimum = std::numeric_limits<double>::infinity();
    double validMaximum = -std::numeric_limits<double>::infinity();
    for (double value : sourceValues) {
        if (!std::isfinite(value)) {
            ++result.invalidCount;
            continue;
        }

        validMinimum = std::min(validMinimum, value);
        validMaximum = std::max(validMaximum, value);
    }

    if (validMinimum == std::numeric_limits<double>::infinity()) {
        result.status = IntensityQuantizationStatus::NoValidValues;
        if (sourceRange.has_value()) {
            result.metadata.sourceMinimum = sourceRange->minimum;
            result.metadata.sourceMaximum = sourceRange->maximum;
        }
        return Result<IntensityQuantizationResult>::success(std::move(result));
    }

    result.metadata.available = true;
    result.metadata.validMinimum = validMinimum;
    result.metadata.validMaximum = validMaximum;
    result.metadata.sourceMinimum = sourceRange.has_value() ? sourceRange->minimum : validMinimum;
    result.metadata.sourceMaximum = sourceRange.has_value() ? sourceRange->maximum : validMaximum;

    if (validMinimum == validMaximum) {
        result.status = IntensityQuantizationStatus::DegenerateRange;
        return Result<IntensityQuantizationResult>::success(std::move(result));
    }

    result.status = IntensityQuantizationStatus::Normal;
    for (std::size_t index = 0U; index < sourceValues.size(); ++index) {
        if (std::isfinite(sourceValues[index])) {
            result.values[index] = quantizeValue(sourceValues[index], validMinimum, validMaximum);
        }
    }

    return Result<IntensityQuantizationResult>::success(std::move(result));
}

} // namespace dzc
