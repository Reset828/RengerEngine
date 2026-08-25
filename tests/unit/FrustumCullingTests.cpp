#include "scene/FrustumCulling.h"

#include <dzc/Error.h>
#include <dzc/ViewFrustum.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace {

void assertDataFormat(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

dzc::Bounds3d bounds(const glm::dvec3& minimum, const glm::dvec3& maximum) {
    return dzc::Bounds3d{minimum, maximum};
}

dzc::ViewFrustum identityFrustum() {
    const auto result = dzc::ViewFrustum::fromViewProjection(
        glm::mat4{1.0F},
        dzc::ClipDepthRange::NegativeOneToOne);
    assert(result.hasValue());
    return result.value();
}

void assertClassification(
    const dzc::Result<dzc::FrustumCullingResult>& result,
    dzc::FrustumClassification classification,
    std::optional<dzc::ViewFrustum::PlaneIndex> separatingPlane) {
    assert(result.hasValue());
    assert(result.value().classification == classification);
    assert(result.value().separatingPlane == separatingPlane);
}

void testTypesAndValueSemantics() {
    static_assert(std::is_same_v<
        std::underlying_type_t<dzc::FrustumClassification>,
        std::uint8_t>);
    static_assert(std::is_copy_constructible_v<dzc::FrustumCullingResult>);
    static_assert(std::is_copy_assignable_v<dzc::FrustumCullingResult>);

    const dzc::FrustumCullingResult result{
        dzc::FrustumClassification::Inside,
        std::nullopt};
    assert(result.classification == dzc::FrustumClassification::Inside);
    assert(!result.separatingPlane.has_value());
}

void testAxisAlignedClassifications() {
    const dzc::ViewFrustum frustum = identityFrustum();

    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{-0.5}, glm::dvec3{0.5})),
        dzc::FrustumClassification::Inside,
        std::nullopt);
    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{0.9, -0.1, -0.1}, glm::dvec3{1.1, 0.1, 0.1})),
        dzc::FrustumClassification::Intersecting,
        std::nullopt);
    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{1.01, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1})),
        dzc::FrustumClassification::Outside,
        dzc::ViewFrustum::Right);

    const auto touching = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{1.0, -0.1, -0.1}, glm::dvec3{1.0, 0.1, 0.1}));
    assertClassification(touching, dzc::FrustumClassification::Inside, std::nullopt);

    const auto degenerateInside = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{0.0}, glm::dvec3{0.0}));
    assertClassification(degenerateInside, dzc::FrustumClassification::Inside, std::nullopt);
}

void testAllOutsidePlanes() {
    const dzc::ViewFrustum frustum = identityFrustum();
    const glm::dvec3 center{0.0, 0.0, 0.0};
    const glm::dvec3 halfExtent{0.1, 0.1, 0.1};
    const glm::dvec3 offsets[] = {
        {2.0, 0.0, 0.0}, {-2.0, 0.0, 0.0},
        {0.0, 2.0, 0.0}, {0.0, -2.0, 0.0},
        {0.0, 0.0, 2.0}, {0.0, 0.0, -2.0}};
    for (std::size_t index = 0U; index < 6U; ++index) {
        const glm::dvec3 point = center + offsets[index];
        const auto result = dzc::FrustumCulling::classify(
            frustum,
            bounds(point - halfExtent, point + halfExtent));
        assert(result.hasValue());
        assert(result.value().classification == dzc::FrustumClassification::Outside);
        assert(result.value().separatingPlane.has_value());
        assert(static_cast<std::size_t>(result.value().separatingPlane.value()) == index);
    }
}

void testRotatedPlanes() {
    dzc::ViewFrustum frustum;
    const double diagonal = std::sqrt(0.5);
    for (auto& plane : frustum.planes) {
        plane = dzc::FrustumPlane{glm::dvec4{0.0, 0.0, 1.0, 100.0}};
    }
    frustum.planes[dzc::ViewFrustum::Left] = dzc::FrustumPlane{
        glm::dvec4{diagonal, diagonal, 0.0, 1.0}};

    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{-0.4, -0.4, -1.0}, glm::dvec3{0.0, 0.0, 1.0})),
        dzc::FrustumClassification::Inside,
        std::nullopt);
    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{-1.0, -1.0, -1.0}, glm::dvec3{0.0, 0.0, 1.0})),
        dzc::FrustumClassification::Intersecting,
        std::nullopt);
    assertClassification(
        dzc::FrustumCulling::classify(
            frustum,
            bounds(glm::dvec3{-1.0, -1.0, -1.0}, glm::dvec3{-0.8, -0.8, 1.0})),
        dzc::FrustumClassification::Outside,
        dzc::ViewFrustum::Left);
}

void testHintsAndDeterminism() {
    const dzc::ViewFrustum frustum = identityFrustum();
    const auto withoutHint = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{1.01, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1}));
    const auto withMatchingHint = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{1.01, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1}),
        dzc::ViewFrustum::Right);
    const auto withNonMatchingHint = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{1.01, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1}),
        dzc::ViewFrustum::Left);
    assertClassification(withoutHint, dzc::FrustumClassification::Outside, dzc::ViewFrustum::Right);
    assertClassification(withMatchingHint, dzc::FrustumClassification::Outside, dzc::ViewFrustum::Right);
    assertClassification(withNonMatchingHint, dzc::FrustumClassification::Outside, dzc::ViewFrustum::Right);

    const auto first = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{0.9, -0.1, -0.1}, glm::dvec3{1.1, 0.1, 0.1}));
    const auto second = dzc::FrustumCulling::classify(
        frustum,
        bounds(glm::dvec3{0.9, -0.1, -0.1}, glm::dvec3{1.1, 0.1, 0.1}),
        dzc::ViewFrustum::Right);
    assertClassification(first, dzc::FrustumClassification::Intersecting, std::nullopt);
    assertClassification(second, dzc::FrustumClassification::Intersecting, std::nullopt);
}

void testInvalidInputsAndLargeCoordinates() {
    const dzc::ViewFrustum validFrustum = identityFrustum();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    for (const dzc::Bounds3d invalidBounds : {
             bounds(glm::dvec3{1.0, 0.0, 0.0}, glm::dvec3{0.0}),
             bounds(glm::dvec3{nan, 0.0, 0.0}, glm::dvec3{1.0}),
             bounds(glm::dvec3{0.0}, glm::dvec3{inf, 1.0, 1.0})}) {
        const auto result = dzc::FrustumCulling::classify(validFrustum, invalidBounds);
        assert(!result.hasValue());
        assertDataFormat(result.error());
    }

    dzc::ViewFrustum invalidPlane = validFrustum;
    invalidPlane.planes[dzc::ViewFrustum::Top] = dzc::FrustumPlane{glm::dvec4{0.0}};
    auto result = dzc::FrustumCulling::classify(validFrustum, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}));
    assert(result.hasValue());
    result = dzc::FrustumCulling::classify(invalidPlane, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}));
    assert(!result.hasValue());
    assertDataFormat(result.error());

    dzc::ViewFrustum nonFinitePlane = validFrustum;
    nonFinitePlane.planes[dzc::ViewFrustum::Near] = dzc::FrustumPlane{
        glm::dvec4{nan, 0.0, 0.0, 1.0}};
    result = dzc::FrustumCulling::classify(nonFinitePlane, bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}));
    assert(!result.hasValue());
    assertDataFormat(result.error());

    result = dzc::FrustumCulling::classify(
        validFrustum,
        bounds(glm::dvec3{-0.5}, glm::dvec3{0.5}),
        static_cast<dzc::ViewFrustum::PlaneIndex>(99U));
    assert(!result.hasValue());
    assertDataFormat(result.error());

    dzc::ViewFrustum overflowPlane = validFrustum;
    overflowPlane.planes[dzc::ViewFrustum::Left] = dzc::FrustumPlane{
        glm::dvec4{1.0e308, 0.0, 0.0, 0.0}};
    result = dzc::FrustumCulling::classify(
        overflowPlane,
        bounds(glm::dvec3{2.0, -0.5, -0.5}, glm::dvec3{3.0, 0.5, 0.5}));
    assert(!result.hasValue());
    assertDataFormat(result.error());

    const auto large = dzc::FrustumCulling::classify(
        validFrustum,
        bounds(glm::dvec3{999999999.0, -1.0, -1.0}, glm::dvec3{1000000001.0, 1.0, 1.0}));
    assert(large.hasValue());
    assert(large.value().classification == dzc::FrustumClassification::Outside);
}

void testInputsUnchanged() {
    dzc::ViewFrustum frustum = identityFrustum();
    const dzc::ViewFrustum originalFrustum = frustum;
    const dzc::Bounds3d originalBounds = bounds(glm::dvec3{-0.5}, glm::dvec3{0.5});
    const auto result = dzc::FrustumCulling::classify(frustum, originalBounds);
    assert(result.hasValue());
    for (std::size_t index = 0U; index < frustum.planes.size(); ++index) {
        assert(frustum.planes[index].equation == originalFrustum.planes[index].equation);
    }
    assert(originalBounds.minimum == glm::dvec3{-0.5});
    assert(originalBounds.maximum == glm::dvec3{0.5});
}

} // namespace

int main() {
    testTypesAndValueSemantics();
    testAxisAlignedClassifications();
    testAllOutsidePlanes();
    testRotatedPlanes();
    testHintsAndDeterminism();
    testInvalidInputsAndLargeCoordinates();
    testInputsUnchanged();
    return 0;
}