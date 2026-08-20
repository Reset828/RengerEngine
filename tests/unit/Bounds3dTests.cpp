#include <dzc/Bounds3d.h>

#include <dzc/Error.h>

#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

void assertCorruptDataError(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
}

void testDefaultEmptyBounds() {
    const dzc::Bounds3d bounds;
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();

    assert(bounds.minimum == glm::dvec3{positiveInfinity});
    assert(bounds.maximum == glm::dvec3{negativeInfinity});
    assert(!bounds.isValid());
    assert(!bounds.isDegenerate());

    const auto center = bounds.center();
    const auto size = bounds.size();
    assert(!center.hasValue());
    assert(!size.hasValue());
    assertCorruptDataError(center.error());
    assertCorruptDataError(size.error());
}

void testSinglePointAndValueSemantics() {
    dzc::Bounds3d bounds;
    const glm::dvec3 point{12.5, -8.0, 3.25};

    assert(bounds.extend(point).hasValue());
    assert(bounds.isValid());
    assert(bounds.minimum == point);
    assert(bounds.maximum == point);
    assert(bounds.isDegenerate());

    const auto center = bounds.center();
    const auto size = bounds.size();
    assert(center.hasValue());
    assert(size.hasValue());
    assert(center.value() == point);
    assert(size.value() == glm::dvec3{0.0});

    const dzc::Bounds3d copied = bounds;
    dzc::Bounds3d moved = std::move(bounds);
    assert(copied.minimum == point);
    assert(copied.maximum == point);
    assert(moved.minimum == point);
    assert(moved.maximum == point);
}

void testMultiplePointsAndDegeneracy() {
    dzc::Bounds3d bounds;
    assert(bounds.extend(glm::dvec3{4.0, 8.0, 1.0}).hasValue());
    assert(bounds.extend(glm::dvec3{-2.0, 10.0, 7.0}).hasValue());
    assert(bounds.extend(glm::dvec3{3.0, -5.0, 4.0}).hasValue());

    assert((bounds.minimum == glm::dvec3{-2.0, -5.0, 1.0}));
    assert((bounds.maximum == glm::dvec3{4.0, 10.0, 7.0}));
    assert(!bounds.isDegenerate());

    const auto center = bounds.center();
    const auto size = bounds.size();
    assert(center.hasValue());
    assert(size.hasValue());
    assert((center.value() == glm::dvec3{1.0, 2.5, 4.0}));
    assert((size.value() == glm::dvec3{6.0, 15.0, 6.0}));

    dzc::Bounds3d flatBounds;
    assert(flatBounds.extend(glm::dvec3{1.0, 2.0, 3.0}).hasValue());
    assert(flatBounds.extend(glm::dvec3{4.0, 2.0, 8.0}).hasValue());
    assert(flatBounds.isDegenerate());
}

void testLargeCoordinates() {
    dzc::Bounds3d bounds;
    assert(bounds.extend(glm::dvec3{1000000000.125, -1000000000.75, 500000000.5}).hasValue());
    assert(bounds.extend(glm::dvec3{1000000120.125, -999999900.75, 500000060.5}).hasValue());

    const auto center = bounds.center();
    const auto size = bounds.size();
    assert(center.hasValue());
    assert(size.hasValue());
    assert(std::abs(center.value().x - 1000000060.125) < 1.0e-9);
    assert(std::abs(center.value().y - -999999950.75) < 1.0e-9);
    assert(std::abs(center.value().z - 500000030.5) < 1.0e-9);
    assert((size.value() == glm::dvec3{120.0, 100.0, 60.0}));
}

void testNonFinitePointsDoNotMutateBounds() {
    dzc::Bounds3d bounds;
    assert(bounds.extend(glm::dvec3{1.0, 2.0, 3.0}).hasValue());
    const dzc::Bounds3d original = bounds;

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double positiveInfinity = std::numeric_limits<double>::infinity();
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    for (const glm::dvec3 point : {
             glm::dvec3{nan, 0.0, 0.0},
             glm::dvec3{positiveInfinity, 0.0, 0.0},
             glm::dvec3{negativeInfinity, 0.0, 0.0}}) {
        const auto result = bounds.extend(point);
        assert(!result.hasValue());
        assertCorruptDataError(result.error());
        assert(bounds.minimum == original.minimum);
        assert(bounds.maximum == original.maximum);
    }
}

void testBoundsMergingAndInvalidBounds() {
    dzc::Bounds3d first;
    assert(first.extend(glm::dvec3{0.0, 1.0, 2.0}).hasValue());
    assert(first.extend(glm::dvec3{4.0, 5.0, 6.0}).hasValue());

    dzc::Bounds3d second;
    assert(second.extend(glm::dvec3{-3.0, 3.0, -4.0}).hasValue());
    assert(second.extend(glm::dvec3{2.0, 8.0, 12.0}).hasValue());

    assert(first.extend(second).hasValue());
    assert((first.minimum == glm::dvec3{-3.0, 1.0, -4.0}));
    assert((first.maximum == glm::dvec3{4.0, 8.0, 12.0}));

    const dzc::Bounds3d original = first;
    const dzc::Bounds3d invalid{
        glm::dvec3{2.0, 0.0, 0.0},
        glm::dvec3{1.0, 1.0, 1.0}};
    assert(!invalid.isValid());
    const auto invalidResult = first.extend(invalid);
    assert(!invalidResult.hasValue());
    assertCorruptDataError(invalidResult.error());
    assert(first.minimum == original.minimum);
    assert(first.maximum == original.maximum);

    dzc::Bounds3d malformedTarget{
        glm::dvec3{2.0, 0.0, 0.0},
        glm::dvec3{1.0, 1.0, 1.0}};
    const dzc::Bounds3d malformedOriginal = malformedTarget;
    const auto malformedResult = malformedTarget.extend(glm::dvec3{0.0, 0.0, 0.0});
    assert(!malformedResult.hasValue());
    assertCorruptDataError(malformedResult.error());
    assert(malformedTarget.minimum == malformedOriginal.minimum);
    assert(malformedTarget.maximum == malformedOriginal.maximum);
}

void testTypeProperties() {
    static_assert(std::is_copy_constructible_v<dzc::Bounds3d>);
    static_assert(std::is_copy_assignable_v<dzc::Bounds3d>);
    static_assert(std::is_move_constructible_v<dzc::Bounds3d>);
    static_assert(std::is_move_assignable_v<dzc::Bounds3d>);
}

} // namespace

int main() {
    testDefaultEmptyBounds();
    testSinglePointAndValueSemantics();
    testMultiplePointsAndDegeneracy();
    testLargeCoordinates();
    testNonFinitePointsDoNotMutateBounds();
    testBoundsMergingAndInvalidBounds();
    testTypeProperties();
    return 0;
}
