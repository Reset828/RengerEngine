#include "data/chunk/CoordinateLocalizer.h"

#include <dzc/Error.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cmath>
#include <cfloat>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

using dzc::CoordinateLocalizationResult;
using dzc::CoordinateLocalizer;

void assertCorruptData(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

void assertFinite(const glm::vec3& value) {
    assert(std::isfinite(value.x));
    assert(std::isfinite(value.y));
    assert(std::isfinite(value.z));
}

double halfUlpTolerance(float value) {
    const float towardPositive = std::nextafter(value, std::numeric_limits<float>::infinity());
    const float towardNegative = std::nextafter(value, -std::numeric_limits<float>::infinity());
    const double positiveSpacing = std::abs(static_cast<double>(towardPositive) - value);
    const double negativeSpacing = std::abs(static_cast<double>(value) - towardNegative);
    return std::max(positiveSpacing, negativeSpacing) / 2.0;
}

void assertReconstructed(const std::vector<glm::dvec3>& sourcePositions,
                         const CoordinateLocalizationResult& result) {
    assert(sourcePositions.size() == result.localPositions.size());
    for (std::size_t index = 0U; index < sourcePositions.size(); ++index) {
        const glm::dvec3 reconstructed = result.origin + glm::dvec3{result.localPositions[index]};
        const glm::dvec3 error = reconstructed - sourcePositions[index];
        assert(std::abs(error.x) <= halfUlpTolerance(result.localPositions[index].x));
        assert(std::abs(error.y) <= halfUlpTolerance(result.localPositions[index].y));
        assert(std::abs(error.z) <= halfUlpTolerance(result.localPositions[index].z));
    }
}

void testNormalPositionsAndOrdering() {
    const std::vector<glm::dvec3> sourcePositions{
        {10.0, -4.0, 3.0},
        {14.0, 2.0, 9.0},
        {12.0, -1.0, 5.0}};
    const auto localized = CoordinateLocalizer::localize(sourcePositions);
    assert(localized.hasValue());

    const auto& result = localized.value();
    assert((result.bounds.minimum == glm::dvec3{10.0, -4.0, 3.0}));
    assert((result.bounds.maximum == glm::dvec3{14.0, 2.0, 9.0}));
    assert((result.origin == glm::dvec3{12.0, -1.0, 6.0}));
    assert(result.localPositions.size() == sourcePositions.size());
    assert((result.localPositions[0] == glm::vec3{-2.0F, -3.0F, -3.0F}));
    assert((result.localPositions[1] == glm::vec3{2.0F, 3.0F, 3.0F}));
    assert((result.localPositions[2] == glm::vec3{0.0F, 0.0F, -1.0F}));
    for (const glm::vec3& localPosition : result.localPositions) {
        assertFinite(localPosition);
    }
    assertReconstructed(sourcePositions, result);
}

void testSinglePointAndDegenerateBounds() {
    const std::vector<glm::dvec3> sourcePositions{{100000000.125, -200000000.25, 0.5}};
    const auto localized = CoordinateLocalizer::localize(sourcePositions);
    assert(localized.hasValue());

    const auto& result = localized.value();
    assert(result.bounds.isDegenerate());
    assert(result.origin == sourcePositions.front());
    assert((result.localPositions.front() == glm::vec3{0.0F}));
    assertReconstructed(sourcePositions, result);
}

void testLargeCoordinatesKeepSmallLocalValues() {
    const std::vector<glm::dvec3> sourcePositions{
        {1000000000.125, -1000000000.75, 500000000.5},
        {1000000120.125, -999999900.75, 500000060.5}};
    const auto localized = CoordinateLocalizer::localize(sourcePositions);
    assert(localized.hasValue());

    const auto& result = localized.value();
    assert((result.origin == glm::dvec3{1000000060.125, -999999950.75, 500000030.5}));
    assert((result.localPositions[0] == glm::vec3{-60.0F, -50.0F, -30.0F}));
    assert((result.localPositions[1] == glm::vec3{60.0F, 50.0F, 30.0F}));
    for (const glm::vec3& localPosition : result.localPositions) {
        assertFinite(localPosition);
    }
    assertReconstructed(sourcePositions, result);
}

void testSmallLocalRangeSurvivesLargeAbsoluteCoordinates() {
    const std::vector<glm::dvec3> sourcePositions{
        {1000000000.125, -1000000000.375, 300000000.625},
        {1000000000.375, -1000000000.125, 300000000.875}};
    assert(static_cast<float>(sourcePositions[0].x) == static_cast<float>(sourcePositions[1].x));

    const auto localized = CoordinateLocalizer::localize(sourcePositions);
    assert(localized.hasValue());

    const auto& result = localized.value();
    assert(std::abs(result.localPositions[0].x) < 1.0F);
    assert(std::abs(result.localPositions[1].x) < 1.0F);
    assert(result.localPositions[0].x < 0.0F);
    assert(result.localPositions[1].x > 0.0F);
    for (const glm::vec3& localPosition : result.localPositions) {
        assertFinite(localPosition);
    }
    assertReconstructed(sourcePositions, result);
}
void testEmptyInputFails() {
    const auto localized = CoordinateLocalizer::localize({});
    assert(!localized.hasValue());
    assertCorruptData(localized.error());
}

void testNonFiniteSourceFails() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    for (const glm::dvec3 point : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{0.0, positiveInfinity, 0.0},
             glm::dvec3{0.0, 0.0, negativeInfinity}}) {
        const auto localized = CoordinateLocalizer::localize({glm::dvec3{0.0}, point});
        assert(!localized.hasValue());
        assertCorruptData(localized.error());
    }
}

void testOriginOverflowFails() {
    const double maximum = std::numeric_limits<double>::max();
    const auto localized = CoordinateLocalizer::localize({
        glm::dvec3{-maximum, 0.0, 0.0},
        glm::dvec3{maximum, 0.0, 0.0}});
    assert(!localized.hasValue());
    assertCorruptData(localized.error());
}

void testFloatConversionOverflowFails() {
    const double largeFiniteValue = static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
    const auto localized = CoordinateLocalizer::localize({
        glm::dvec3{-largeFiniteValue, 0.0, 0.0},
        glm::dvec3{largeFiniteValue, 0.0, 0.0}});
    assert(!localized.hasValue());
    assertCorruptData(localized.error());
}

void testTypeProperties() {
    static_assert(std::is_copy_constructible_v<CoordinateLocalizationResult>);
    static_assert(std::is_copy_assignable_v<CoordinateLocalizationResult>);
    static_assert(std::is_move_constructible_v<CoordinateLocalizationResult>);
    static_assert(std::is_move_assignable_v<CoordinateLocalizationResult>);
}

} // namespace

int main() {
    testNormalPositionsAndOrdering();
    testSinglePointAndDegenerateBounds();
    testLargeCoordinatesKeepSmallLocalValues();
    testSmallLocalRangeSurvivesLargeAbsoluteCoordinates();
    testEmptyInputFails();
    testNonFiniteSourceFails();
    testOriginOverflowFails();
    testFloatConversionOverflowFails();
    testTypeProperties();
    return 0;
}