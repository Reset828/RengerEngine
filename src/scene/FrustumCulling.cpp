#include "scene/FrustumCulling.h"

#include <dzc/Error.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace dzc {
namespace {

constexpr std::uint32_t kDataFormatCode = 2U;
constexpr std::size_t kPlaneCount = 6U;

Error dataFormatError(const char* diagnosticMessage) noexcept {
    return Error{
        ErrorDomain::DataFormat,
        kDataFormatCode,
        "Frustum culling input is invalid.",
        diagnosticMessage,
        "FrustumCulling"};
}

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool isValidPlaneIndex(ViewFrustum::PlaneIndex index) noexcept {
    return static_cast<std::size_t>(index) < kPlaneCount;
}

bool isValidPlane(const FrustumPlane& plane) noexcept {
    if (!std::isfinite(plane.equation.x) ||
        !std::isfinite(plane.equation.y) ||
        !std::isfinite(plane.equation.z) ||
        !std::isfinite(plane.equation.w)) {
        return false;
    }
    return plane.equation.x != 0.0 ||
        plane.equation.y != 0.0 ||
        plane.equation.z != 0.0;
}

bool checkedMultiply(double left, double right, double& result) noexcept {
    result = left * right;
    return std::isfinite(result);
}

bool checkedAdd(double left, double right, double& result) noexcept {
    result = left + right;
    return std::isfinite(result);
}

bool checkedPlaneDistance(
    const FrustumPlane& plane,
    const glm::dvec3& vertex,
    double& distance) noexcept {
    double xTerm = 0.0;
    double yTerm = 0.0;
    double zTerm = 0.0;
    double partial = 0.0;
    if (!checkedMultiply(plane.equation.x, vertex.x, xTerm) ||
        !checkedMultiply(plane.equation.y, vertex.y, yTerm) ||
        !checkedMultiply(plane.equation.z, vertex.z, zTerm) ||
        !checkedAdd(xTerm, yTerm, partial) ||
        !checkedAdd(partial, zTerm, partial) ||
        !checkedAdd(partial, plane.equation.w, distance)) {
        return false;
    }
    return std::isfinite(distance);
}

struct PlaneEvaluation final {
    bool outside{false};
    bool intersects{false};
};

bool evaluatePlane(
    const FrustumPlane& plane,
    const Bounds3d& bounds,
    PlaneEvaluation& evaluation) noexcept {
    const glm::dvec3 normal{
        plane.equation.x,
        plane.equation.y,
        plane.equation.z};
    const glm::dvec3 positiveVertex{
        normal.x >= 0.0 ? bounds.maximum.x : bounds.minimum.x,
        normal.y >= 0.0 ? bounds.maximum.y : bounds.minimum.y,
        normal.z >= 0.0 ? bounds.maximum.z : bounds.minimum.z};
    const glm::dvec3 negativeVertex{
        normal.x >= 0.0 ? bounds.minimum.x : bounds.maximum.x,
        normal.y >= 0.0 ? bounds.minimum.y : bounds.maximum.y,
        normal.z >= 0.0 ? bounds.minimum.z : bounds.maximum.z};

    double positiveDistance = 0.0;
    double negativeDistance = 0.0;
    if (!checkedPlaneDistance(plane, positiveVertex, positiveDistance) ||
        !checkedPlaneDistance(plane, negativeVertex, negativeDistance)) {
        return false;
    }

    evaluation.outside = positiveDistance < 0.0;
    evaluation.intersects = !evaluation.outside && negativeDistance < 0.0;
    return true;
}

} // namespace

Result<FrustumCullingResult> FrustumCulling::classify(
    const ViewFrustum& frustum,
    const Bounds3d& bounds,
    std::optional<ViewFrustum::PlaneIndex> previousSeparatingPlane) noexcept {
    if (!bounds.isValid()) {
        return Result<FrustumCullingResult>::failure(dataFormatError(
            "Frustum culling requires finite, axis-ordered Bounds3d coordinates."));
    }

    if (previousSeparatingPlane.has_value() &&
        !isValidPlaneIndex(previousSeparatingPlane.value())) {
        return Result<FrustumCullingResult>::failure(dataFormatError(
            "The previous separating plane index is invalid."));
    }

    for (const FrustumPlane& plane : frustum.planes) {
        if (!isValidPlane(plane)) {
            return Result<FrustumCullingResult>::failure(dataFormatError(
                "Every frustum plane must be finite and have a non-zero normal."));
        }
    }

    std::array<bool, kPlaneCount> evaluated{};
    bool hasIntersection = false;

    const auto evaluateIndex = [&](std::size_t index)
        -> Result<std::optional<ViewFrustum::PlaneIndex>> {
        PlaneEvaluation evaluation;
        if (!evaluatePlane(frustum.planes[index], bounds, evaluation)) {
            return Result<std::optional<ViewFrustum::PlaneIndex>>::failure(dataFormatError(
                "Frustum plane evaluation produced a non-finite intermediate result."));
        }
        evaluated[index] = true;
        if (evaluation.outside) {
            return Result<std::optional<ViewFrustum::PlaneIndex>>::success(
                static_cast<ViewFrustum::PlaneIndex>(index));
        }
        hasIntersection = hasIntersection || evaluation.intersects;
        return Result<std::optional<ViewFrustum::PlaneIndex>>::success(std::nullopt);
    };

    if (previousSeparatingPlane.has_value()) {
        const std::size_t hintedIndex = static_cast<std::size_t>(previousSeparatingPlane.value());
        const auto hintedResult = evaluateIndex(hintedIndex);
        if (!hintedResult.hasValue()) {
            return Result<FrustumCullingResult>::failure(hintedResult.error());
        }
        if (hintedResult.value().has_value()) {
            return Result<FrustumCullingResult>::success(FrustumCullingResult{
                FrustumClassification::Outside,
                hintedResult.value()});
        }
    }

    for (std::size_t index = 0U; index < kPlaneCount; ++index) {
        if (evaluated[index]) {
            continue;
        }
        const auto evaluationResult = evaluateIndex(index);
        if (!evaluationResult.hasValue()) {
            return Result<FrustumCullingResult>::failure(evaluationResult.error());
        }
        if (evaluationResult.value().has_value()) {
            return Result<FrustumCullingResult>::success(FrustumCullingResult{
                FrustumClassification::Outside,
                evaluationResult.value()});
        }
    }

    return Result<FrustumCullingResult>::success(FrustumCullingResult{
        hasIntersection ? FrustumClassification::Intersecting : FrustumClassification::Inside,
        std::nullopt});
}

} // namespace dzc