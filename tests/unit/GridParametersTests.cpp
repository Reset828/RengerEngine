#include "data/chunk/GridParameters.h"

#include <dzc/Error.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace {

dzc::Bounds3d makeBounds(const glm::dvec3& minimum, const glm::dvec3& maximum) {
    return dzc::Bounds3d{minimum, maximum};
}

void assertCorruptDataError(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
}

void assertFinitePositive(const dzc::Result<double>& result) {
    assert(result.hasValue());
    assert(std::isfinite(result.value()));
    assert(result.value() > 0.0);
}

void testRegularAndDeterministicEstimate() {
    const auto bounds = makeBounds({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0});
    const auto first = dzc::GridParameters::estimateCellSize(bounds, 1000U);
    const auto second = dzc::GridParameters::estimateCellSize(bounds, 1000U);

    assertFinitePositive(first);
    assertFinitePositive(second);
    assert(std::abs(first.value() - 64.0) < 1.0e-12);
    assert(first.value() == second.value());
}

void testPointDensityChangesCellSize() {
    const auto bounds = makeBounds({0.0, 0.0, 0.0}, {10.0, 10.0, 10.0});
    const auto sparse = dzc::GridParameters::estimateCellSize(bounds, 1000U);
    const auto dense = dzc::GridParameters::estimateCellSize(bounds, 8000U);

    assertFinitePositive(sparse);
    assertFinitePositive(dense);
    assert(sparse.value() > dense.value());
    assert(std::abs(dense.value() - 32.0) < 1.0e-12);
}

void testUnknownAndZeroPointCountsUseLongestExtent() {
    const auto bounds = makeBounds({-4.0, 2.0, 8.0}, {6.0, 6.0, 10.0});
    const auto unknown = dzc::GridParameters::estimateCellSize(bounds, std::nullopt);
    const auto zero = dzc::GridParameters::estimateCellSize(bounds, 0U);

    assertFinitePositive(unknown);
    assertFinitePositive(zero);
    assert(unknown.value() == 10.0);
    assert(zero.value() == 10.0);
}

void testDegenerateBoundsReplaceZeroAxes() {
    const auto bounds = makeBounds({0.0, 2.0, -4.0}, {8.0, 2.0, 0.0});
    const auto result = dzc::GridParameters::estimateCellSize(bounds, 1000U);
    const double expected = std::cbrt(8.0 * 8.0 * 4.0 * 262144.0 / 1000.0);

    assertFinitePositive(result);
    assert(std::abs(result.value() - expected) < 1.0e-9);
}

void testAllZeroBoundsUseUnitFallback() {
    const auto bounds = makeBounds({12.5, -8.0, 3.25}, {12.5, -8.0, 3.25});
    const auto known = dzc::GridParameters::estimateCellSize(bounds, 1U);
    const auto unknown = dzc::GridParameters::estimateCellSize(bounds, std::nullopt);

    assertFinitePositive(known);
    assertFinitePositive(unknown);
    assert(known.value() == 1.0);
    assert(unknown.value() == 1.0);
}

void testLargeCoordinates() {
    const auto bounds = makeBounds(
        {1000000000.125, -1000000000.75, 500000000.5},
        {1000000120.125, -999999900.75, 500000060.5});
    const auto result = dzc::GridParameters::estimateCellSize(bounds, 720000U);

    assertFinitePositive(result);
}

void testInvalidBoundsFail() {
    const dzc::Bounds3d emptyBounds;
    const auto emptyResult = dzc::GridParameters::estimateCellSize(emptyBounds, 1U);
    assert(!emptyResult.hasValue());
    assertCorruptDataError(emptyResult.error());

    const auto reversedBounds = makeBounds({1.0, 0.0, 0.0}, {0.0, 1.0, 1.0});
    const auto reversedResult = dzc::GridParameters::estimateCellSize(reversedBounds, 1U);
    assert(!reversedResult.hasValue());
    assertCorruptDataError(reversedResult.error());

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto nanBounds = makeBounds({0.0, 0.0, 0.0}, {nan, 1.0, 1.0});
    const auto nanResult = dzc::GridParameters::estimateCellSize(nanBounds, 1U);
    assert(!nanResult.hasValue());
    assertCorruptDataError(nanResult.error());

    const double infinity = std::numeric_limits<double>::infinity();
    const auto infinityBounds = makeBounds({0.0, 0.0, 0.0}, {infinity, 1.0, 1.0});
    const auto infinityResult = dzc::GridParameters::estimateCellSize(infinityBounds, 1U);
    assert(!infinityResult.hasValue());
    assertCorruptDataError(infinityResult.error());
}

void testUnrepresentableExtentOrVolumeFails() {
    const double maximum = std::numeric_limits<double>::max();
    const auto overflowingExtent = makeBounds({-maximum, 0.0, 0.0}, {maximum, 1.0, 1.0});
    const auto extentResult = dzc::GridParameters::estimateCellSize(overflowingExtent, 1U);
    assert(!extentResult.hasValue());
    assertCorruptDataError(extentResult.error());

    const auto overflowingVolume = makeBounds({0.0, 0.0, 0.0}, {maximum, maximum, maximum});
    const auto volumeResult = dzc::GridParameters::estimateCellSize(overflowingVolume, 1U);
    assert(!volumeResult.hasValue());
    assertCorruptDataError(volumeResult.error());
}

} // namespace

int main() {
    testRegularAndDeterministicEstimate();
    testPointDensityChangesCellSize();
    testUnknownAndZeroPointCountsUseLongestExtent();
    testDegenerateBoundsReplaceZeroAxes();
    testAllZeroBoundsUseUnitFallback();
    testLargeCoordinates();
    testInvalidBoundsFail();
    testUnrepresentableExtentOrVolumeFails();
    return 0;
}