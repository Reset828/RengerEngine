#include "data/chunk/GridCellKey.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {

using dzc::GridCellKey;

void assertCorruptData(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

void assertFailure(const dzc::Result<GridCellKey>& result) {
    assert(!result.hasValue());
    assertCorruptData(result.error());
}

void testNormalPositionAndOrigin() {
    const auto result = GridCellKey::fromPosition(
        glm::dvec3{2.5, 7.9, 10.0},
        glm::dvec3{0.0},
        2.0);
    assert(result.hasValue());
    assert((result.value() == GridCellKey{1, 3, 5}));
}

void testCellBoundariesAndMaximumBoundary() {
    const auto result = GridCellKey::fromPosition(
        glm::dvec3{10.0, 20.0, 30.0},
        glm::dvec3{0.0},
        10.0);
    assert(result.hasValue());
    assert((result.value() == GridCellKey{1, 2, 3}));
}

void testNegativeCoordinatesUseMathematicalFloor() {
    const auto result = GridCellKey::fromPosition(
        glm::dvec3{-0.1, -10.0, -10.1},
        glm::dvec3{0.0},
        10.0);
    assert(result.hasValue());
    assert((result.value() == GridCellKey{-1, -1, -2}));
}

void testNegativeDatasetMinimum() {
    const auto result = GridCellKey::fromPosition(
        glm::dvec3{-5.0, 5.0, 15.0},
        glm::dvec3{-15.0, -5.0, 5.0},
        10.0);
    assert(result.hasValue());
    assert((result.value() == GridCellKey{1, 1, 1}));
}

void testLargeFiniteCoordinates() {
    const auto result = GridCellKey::fromPosition(
        glm::dvec3{1000000000000.0 + 4095.0, -1000000000000.0 + 2049.0, 800000000000.0},
        glm::dvec3{1000000000000.0, -1000000000000.0, 799999999999.0},
        1.0);
    assert(result.hasValue());
    assert((result.value() == GridCellKey{4095, 2049, 1}));
}

void testNonFiniteInputsFail() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();

    for (const glm::dvec3 position : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{0.0, positiveInfinity, 0.0},
             glm::dvec3{0.0, 0.0, negativeInfinity}}) {
        assertFailure(GridCellKey::fromPosition(position, glm::dvec3{0.0}, 1.0));
    }

    for (const glm::dvec3 minimum : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{0.0, positiveInfinity, 0.0},
             glm::dvec3{0.0, 0.0, negativeInfinity}}) {
        assertFailure(GridCellKey::fromPosition(glm::dvec3{0.0}, minimum, 1.0));
    }
}

void testInvalidCellSizesFail() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    for (const double cellSize : {0.0, -1.0, nan, positiveInfinity}) {
        assertFailure(GridCellKey::fromPosition(glm::dvec3{0.0}, glm::dvec3{0.0}, cellSize));
    }
}

void testSubtractionOverflowFails() {
    const double maximum = std::numeric_limits<double>::max();
    assertFailure(GridCellKey::fromPosition(
        glm::dvec3{maximum, 0.0, 0.0},
        glm::dvec3{-maximum, 0.0, 0.0},
        1.0));
}

void testIndexOverflowFails() {
    const double int64Limit = 0x1p63;
    assertFailure(GridCellKey::fromPosition(
        glm::dvec3{int64Limit, 0.0, 0.0},
        glm::dvec3{0.0},
        1.0));
    assertFailure(GridCellKey::fromPosition(
        glm::dvec3{-int64Limit * 2.0, 0.0, 0.0},
        glm::dvec3{0.0},
        1.0));
}

void testDeterminismAndOrdering() {
    const auto first = GridCellKey::fromPosition(
        glm::dvec3{1.25, 2.25, 3.25}, glm::dvec3{0.0}, 1.0);
    const auto second = GridCellKey::fromPosition(
        glm::dvec3{1.25, 2.25, 3.25}, glm::dvec3{0.0}, 1.0);
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value() == second.value());

    const GridCellKey lower{1, 2, 9};
    const GridCellKey upper{1, 3, -100};
    assert(lower < upper);
    assert(!(upper < lower));
    assert(lower != upper);
}

void testTypeProperties() {
    static_assert(std::is_copy_constructible_v<GridCellKey>);
    static_assert(std::is_copy_assignable_v<GridCellKey>);
    static_assert(std::is_move_constructible_v<GridCellKey>);
    static_assert(std::is_move_assignable_v<GridCellKey>);
}

} // namespace

int main() {
    testNormalPositionAndOrigin();
    testCellBoundariesAndMaximumBoundary();
    testNegativeCoordinatesUseMathematicalFloor();
    testNegativeDatasetMinimum();
    testLargeFiniteCoordinates();
    testNonFiniteInputsFail();
    testInvalidCellSizesFail();
    testSubtractionOverflowFails();
    testIndexOverflowFails();
    testDeterminismAndOrdering();
    testTypeProperties();
    return 0;
}