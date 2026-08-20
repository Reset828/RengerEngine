#pragma once

#include "data/chunk/PointAttributes.h"
#include <dzc/Result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dzc {

struct IntensitySourceRange final {
    double minimum{0.0};
    double maximum{0.0};
};

enum class IntensityQuantizationStatus : std::uint8_t {
    Normal,
    DegenerateRange,
    NoValidValues
};

struct IntensityQuantizationResult final {
    std::vector<std::uint16_t> values;
    IntensityMetadata metadata;
    IntensityQuantizationStatus status{IntensityQuantizationStatus::NoValidValues};
    std::size_t invalidCount{0U};
};

class IntensityQuantizer final {
public:
    // Quantizes finite source values using their computed valid range.
    static Result<IntensityQuantizationResult> quantize(
        const std::vector<double>& sourceValues,
        std::optional<IntensitySourceRange> sourceRange = std::nullopt) noexcept;
};

} // namespace dzc
