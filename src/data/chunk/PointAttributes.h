#pragma once

#include <cstdint>

namespace dzc {

enum class PointAttribute : std::uint32_t {
    Position = 1U << 0U,
    Color = 1U << 1U,
    Intensity = 1U << 2U
};

struct AttributeSchema final {
    std::uint32_t mask{0U};

    bool hasPosition() const noexcept {
        return (mask & static_cast<std::uint32_t>(PointAttribute::Position)) != 0U;
    }

    bool hasColor() const noexcept {
        return (mask & static_cast<std::uint32_t>(PointAttribute::Color)) != 0U;
    }

    bool hasIntensity() const noexcept {
        return (mask & static_cast<std::uint32_t>(PointAttribute::Intensity)) != 0U;
    }
};

struct IntensityMetadata final {
    bool available{false};
    double sourceMinimum{0.0};
    double sourceMaximum{0.0};
    double validMinimum{0.0};
    double validMaximum{0.0};
};

} // namespace dzc
