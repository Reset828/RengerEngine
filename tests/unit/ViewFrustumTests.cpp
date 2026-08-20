#include <dzc/Bounds3d.h>
#include <dzc/Error.h>
#include <dzc/ViewFrustum.h>

#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>

namespace {

void assertCorrupt(const dzc::Error& error) {
    assert(error.domain == dzc::ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

dzc::Bounds3d makeBounds(const glm::dvec3& minimum, const glm::dvec3& maximum) {
    return dzc::Bounds3d{minimum, maximum};
}

dzc::ViewFrustum identityFrustum(dzc::ClipDepthRange range) {
    const auto result = dzc::ViewFrustum::fromViewProjection(glm::mat4{1.0F}, range);
    assert(result.hasValue());
    return result.value();
}

void testTypesAndValueSemantics() {
    static_assert(std::is_same_v<decltype(dzc::FrustumPlane::equation), glm::dvec4>);
    static_assert(std::is_same_v<decltype(dzc::ViewFrustum::planes), std::array<dzc::FrustumPlane, 6>>);
    static_assert(std::is_same_v<std::underlying_type_t<dzc::ClipDepthRange>, std::uint8_t>);
    static_assert(std::is_copy_constructible_v<dzc::FrustumPlane>);
    static_assert(std::is_copy_assignable_v<dzc::ViewFrustum>);

    const dzc::FrustumPlane plane;
    assert((plane.equation == glm::dvec4{0.0}));
    const dzc::ViewFrustum frustum;
    assert(frustum.planes.size() == 6U);
    assert((frustum.planes[dzc::ViewFrustum::Left].equation == glm::dvec4{0.0}));
}

void testPlaneNormalization() {
    const dzc::FrustumPlane source{glm::dvec4{3.0, 4.0, 0.0, 10.0}};
    const auto normalized = source.normalized();
    assert(normalized.hasValue());
    assert(std::abs(normalized.value().equation.x - 0.6) < 1.0e-12);
    assert(std::abs(normalized.value().equation.y - 0.8) < 1.0e-12);
    assert(std::abs(normalized.value().equation.w - 2.0) < 1.0e-12);
    assert((source.equation == glm::dvec4{3.0, 4.0, 0.0, 10.0}));
    const dzc::FrustumPlane tinyNormal{glm::dvec4{1.0e-300, 0.0, 0.0, 0.0}};
    const auto tinyNormalized = tinyNormal.normalized();
    assert(tinyNormalized.hasValue());
    assert((tinyNormalized.value().equation == glm::dvec4{1.0, 0.0, 0.0, 0.0}));

    dzc::ViewFrustum sourceFrustum;
    for (auto& plane : sourceFrustum.planes) {
        plane = dzc::FrustumPlane{glm::dvec4{2.0, 0.0, 0.0, 2.0}};
    }
    const auto normalizedFrustum = sourceFrustum.normalized();
    assert(normalizedFrustum.hasValue());
    assert((normalizedFrustum.value().planes[0].equation == glm::dvec4{1.0, 0.0, 0.0, 1.0}));
    assert((sourceFrustum.planes[0].equation == glm::dvec4{2.0, 0.0, 0.0, 2.0}));
}

void testPlaneNormalizationFailures() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const dzc::FrustumPlane invalid : {
             dzc::FrustumPlane{glm::dvec4{0.0}},
             dzc::FrustumPlane{glm::dvec4{nan, 0.0, 0.0, 1.0}}}) {
        const auto result = invalid.normalized();
        assert(!result.hasValue());
        assertCorrupt(result.error());
    }

    dzc::ViewFrustum frustum;
    for (auto& plane : frustum.planes) {
        plane = dzc::FrustumPlane{glm::dvec4{1.0, 0.0, 0.0, 1.0}};
    }
    frustum.planes[dzc::ViewFrustum::Near] = dzc::FrustumPlane{glm::dvec4{0.0}};
    const auto result = frustum.normalized();
    assert(!result.hasValue());
    assertCorrupt(result.error());
}

void testMatrixExtractionAndPlaneOrder() {
    const auto openGl = identityFrustum(dzc::ClipDepthRange::NegativeOneToOne);
    const glm::dvec4 expectedOpenGl[] = {
        {1.0, 0.0, 0.0, 1.0}, {-1.0, 0.0, 0.0, 1.0},
        {0.0, 1.0, 0.0, 1.0}, {0.0, -1.0, 0.0, 1.0},
        {0.0, 0.0, 1.0, 1.0}, {0.0, 0.0, -1.0, 1.0}};
    for (std::size_t index = 0; index < 6; ++index) {
        assert(openGl.planes[index].equation == expectedOpenGl[index]);
    }

    const auto zeroToOne = identityFrustum(dzc::ClipDepthRange::ZeroToOne);
    assert((zeroToOne.planes[dzc::ViewFrustum::Near].equation == glm::dvec4{0.0, 0.0, 1.0, 0.0}));
    assert((zeroToOne.planes[dzc::ViewFrustum::Far].equation == glm::dvec4{0.0, 0.0, -1.0, 1.0}));

    const auto invalidRange = dzc::ViewFrustum::fromViewProjection(
        glm::mat4{1.0F}, static_cast<dzc::ClipDepthRange>(255U));
    assert(!invalidRange.hasValue());
    assertCorrupt(invalidRange.error());
}

void testMatrixFailures() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    glm::mat4 nonFinite{1.0F};
    nonFinite[0][0] = static_cast<float>(nan);
    const auto nonFiniteResult = dzc::ViewFrustum::fromViewProjection(
        nonFinite, dzc::ClipDepthRange::NegativeOneToOne);
    assert(!nonFiniteResult.hasValue());
    assertCorrupt(nonFiniteResult.error());

    const auto degenerateResult = dzc::ViewFrustum::fromViewProjection(
        glm::mat4{0.0F}, dzc::ClipDepthRange::NegativeOneToOne);
    assert(!degenerateResult.hasValue());
    assertCorrupt(degenerateResult.error());
}

void testIntersections() {
    const auto frustum = identityFrustum(dzc::ClipDepthRange::NegativeOneToOne);
    assert(frustum.intersects(makeBounds(glm::dvec3{-0.5}, glm::dvec3{0.5})).value());
    assert(frustum.intersects(makeBounds(glm::dvec3{1.0, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1})).value());
    assert(!frustum.intersects(makeBounds(glm::dvec3{1.01, -0.1, -0.1}, glm::dvec3{1.5, 0.1, 0.1})).value());
    assert(frustum.intersects(makeBounds(glm::dvec3{0.9, -0.1, -0.1}, glm::dvec3{1.1, 0.1, 0.1})).value());

    dzc::ViewFrustum invalid = frustum;
    invalid.planes[dzc::ViewFrustum::Left] = dzc::FrustumPlane{glm::dvec4{0.0}};
    const auto invalidPlane = invalid.intersects(makeBounds(glm::dvec3{-0.5}, glm::dvec3{0.5}));
    assert(!invalidPlane.hasValue());
    assertCorrupt(invalidPlane.error());

    const double nan = std::numeric_limits<double>::quiet_NaN();
    dzc::ViewFrustum nonFinite = frustum;
    nonFinite.planes[dzc::ViewFrustum::Right] = dzc::FrustumPlane{glm::dvec4{nan, 0.0, 0.0, 1.0}};
    const auto nonFinitePlane = nonFinite.intersects(makeBounds(glm::dvec3{-0.5}, glm::dvec3{0.5}));
    assert(!nonFinitePlane.hasValue());
    assertCorrupt(nonFinitePlane.error());
    const dzc::Bounds3d invalidBounds{glm::dvec3{1.0, 0.0, 0.0}, glm::dvec3{0.0}};
    const auto invalidBoundsResult = frustum.intersects(invalidBounds);
    assert(!invalidBoundsResult.hasValue());
    assertCorrupt(invalidBoundsResult.error());
}

} // namespace

int main() {
    testTypesAndValueSemantics();
    testPlaneNormalization();
    testPlaneNormalizationFailures();
    testMatrixExtractionAndPlaneOrder();
    testMatrixFailures();
    testIntersections();
    return 0;
}
