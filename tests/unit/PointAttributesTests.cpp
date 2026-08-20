#include "data/chunk/PointAttributes.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

constexpr std::uint32_t attributeMask(dzc::PointAttribute attribute) noexcept {
    return static_cast<std::uint32_t>(attribute);
}

void testPointAttributeDefinition() {
    static_assert(std::is_same_v<std::underlying_type_t<dzc::PointAttribute>, std::uint32_t>);
    static_assert(attributeMask(dzc::PointAttribute::Position) == (1U << 0U));
    static_assert(attributeMask(dzc::PointAttribute::Color) == (1U << 1U));
    static_assert(attributeMask(dzc::PointAttribute::Intensity) == (1U << 2U));
}

void testDefaultSchema() {
    const dzc::AttributeSchema schema{};
    assert(schema.mask == 0U);
    assert(!schema.hasPosition());
    assert(!schema.hasColor());
    assert(!schema.hasIntensity());
}

void testIndividualAttributes() {
    const dzc::AttributeSchema position{attributeMask(dzc::PointAttribute::Position)};
    assert(position.hasPosition());
    assert(!position.hasColor());
    assert(!position.hasIntensity());

    const dzc::AttributeSchema color{attributeMask(dzc::PointAttribute::Color)};
    assert(!color.hasPosition());
    assert(color.hasColor());
    assert(!color.hasIntensity());

    const dzc::AttributeSchema intensity{attributeMask(dzc::PointAttribute::Intensity)};
    assert(!intensity.hasPosition());
    assert(!intensity.hasColor());
    assert(intensity.hasIntensity());
}

void testCombinationsAndUnknownBits() {
    const auto positionColor = attributeMask(dzc::PointAttribute::Position) |
                               attributeMask(dzc::PointAttribute::Color);
    const dzc::AttributeSchema first{positionColor};
    assert(first.hasPosition());
    assert(first.hasColor());
    assert(!first.hasIntensity());

    const auto positionIntensity = attributeMask(dzc::PointAttribute::Position) |
                                   attributeMask(dzc::PointAttribute::Intensity);
    const dzc::AttributeSchema second{positionIntensity};
    assert(second.hasPosition());
    assert(!second.hasColor());
    assert(second.hasIntensity());

    const auto allAttributes = attributeMask(dzc::PointAttribute::Position) |
                               attributeMask(dzc::PointAttribute::Color) |
                               attributeMask(dzc::PointAttribute::Intensity);
    const dzc::AttributeSchema all{allAttributes};
    assert(all.hasPosition());
    assert(all.hasColor());
    assert(all.hasIntensity());

    const dzc::AttributeSchema unknown{1U << 31U};
    assert(!unknown.hasPosition());
    assert(!unknown.hasColor());
    assert(!unknown.hasIntensity());
}

void testIntensityMetadata() {
    const dzc::IntensityMetadata defaults{};
    assert(!defaults.available);
    assert(defaults.sourceMinimum == 0.0);
    assert(defaults.sourceMaximum == 0.0);
    assert(defaults.validMinimum == 0.0);
    assert(defaults.validMaximum == 0.0);

    const dzc::IntensityMetadata original{true, -10.5, 42.25, -2.0, 40.0};
    const dzc::IntensityMetadata copied = original;
    assert(copied.available);
    assert(copied.sourceMinimum == -10.5);
    assert(copied.sourceMaximum == 42.25);
    assert(copied.validMinimum == -2.0);
    assert(copied.validMaximum == 40.0);

    const dzc::IntensityMetadata moved = std::move(copied);
    assert(moved.available);
    assert(moved.sourceMinimum == -10.5);
    assert(moved.sourceMaximum == 42.25);
    assert(moved.validMinimum == -2.0);
    assert(moved.validMaximum == 40.0);
}

} // namespace

int main() {
    testPointAttributeDefinition();
    testDefaultSchema();
    testIndividualAttributes();
    testCombinationsAndUnknownBits();
    testIntensityMetadata();
    return 0;
}
