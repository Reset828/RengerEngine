#include <dzc/ViewFrustum.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

Error frustumError(const char* userMessage, const char* diagnosticMessage, const char* context) {
    return Error{ErrorDomain::DataFormat, kCorruptDataCode, userMessage, diagnosticMessage, context};
}

bool isFinite(const glm::dvec4& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

bool isFinite(const glm::mat4& value) noexcept {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(value[column][row])) {
                return false;
            }
        }
    }
    return true;
}

Result<FrustumPlane> normalizePlane(const FrustumPlane& plane, const char* context) noexcept {
    if (!isFinite(plane.equation)) {
        return Result<FrustumPlane>::failure(frustumError(
            "Frustum plane is not finite",
            "A frustum plane equation must contain only finite components.",
            context));
    }

    const glm::dvec3 normal{plane.equation.x, plane.equation.y, plane.equation.z};
    const double normalScale = std::max(std::abs(normal.x), std::max(std::abs(normal.y), std::abs(normal.z)));
    if (normalScale == 0.0) {
        return Result<FrustumPlane>::failure(frustumError(
            "Frustum plane is degenerate",
            "A frustum plane normal must have a finite, non-zero length.",
            context));
    }

    const glm::dvec3 scaledNormal = normal / normalScale;
    const double scaledLength = std::sqrt(glm::dot(scaledNormal, scaledNormal));
    if (!std::isfinite(scaledLength) || scaledLength == 0.0) {
        return Result<FrustumPlane>::failure(frustumError(
            "Frustum plane cannot be normalized",
            "The frustum plane normal length is not finite or is zero.",
            context));
    }

    const glm::dvec4 normalizedEquation = (plane.equation / normalScale) / scaledLength;
    if (!isFinite(normalizedEquation)) {
        return Result<FrustumPlane>::failure(frustumError(
            "Normalized frustum plane is not finite",
            "Normalizing the frustum plane produced a non-finite equation.",
            context));
    }

    return Result<FrustumPlane>::success(FrustumPlane{normalizedEquation});
}

bool isValidPlaneForEvaluation(const FrustumPlane& plane) noexcept {
    if (!isFinite(plane.equation)) {
        return false;
    }
    return plane.equation.x != 0.0 || plane.equation.y != 0.0 || plane.equation.z != 0.0;
}

glm::dvec4 row(const glm::mat4& matrix, int rowIndex) noexcept {
    return glm::dvec4{
        static_cast<double>(matrix[0][rowIndex]),
        static_cast<double>(matrix[1][rowIndex]),
        static_cast<double>(matrix[2][rowIndex]),
        static_cast<double>(matrix[3][rowIndex])};
}

} // namespace

Result<FrustumPlane> FrustumPlane::normalized() const noexcept {
    return normalizePlane(*this, "FrustumPlane::normalized");
}

Result<ViewFrustum> ViewFrustum::normalized() const noexcept {
    ViewFrustum result;
    for (std::size_t index = 0; index < planes.size(); ++index) {
        const auto normalizedPlane = planes[index].normalized();
        if (!normalizedPlane.hasValue()) {
            return Result<ViewFrustum>::failure(normalizedPlane.error());
        }
        result.planes[index] = normalizedPlane.value();
    }
    return Result<ViewFrustum>::success(result);
}

Result<ViewFrustum> ViewFrustum::fromViewProjection(
    const glm::mat4& viewProjection,
    ClipDepthRange depthRange) noexcept {
    if (!isFinite(viewProjection)) {
        return Result<ViewFrustum>::failure(frustumError(
            "View-projection matrix is not finite",
            "ViewFrustum plane extraction requires finite matrix components.",
            "ViewFrustum::fromViewProjection(matrix)"));
    }

    const glm::dvec4 row0 = row(viewProjection, 0);
    const glm::dvec4 row1 = row(viewProjection, 1);
    const glm::dvec4 row2 = row(viewProjection, 2);
    const glm::dvec4 row3 = row(viewProjection, 3);

    ViewFrustum extracted;
    extracted.planes[Left] = FrustumPlane{row3 + row0};
    extracted.planes[Right] = FrustumPlane{row3 - row0};
    extracted.planes[Bottom] = FrustumPlane{row3 + row1};
    extracted.planes[Top] = FrustumPlane{row3 - row1};
    if (depthRange == ClipDepthRange::NegativeOneToOne) {
        extracted.planes[Near] = FrustumPlane{row3 + row2};
    } else if (depthRange == ClipDepthRange::ZeroToOne) {
        extracted.planes[Near] = FrustumPlane{row2};
    } else {
        return Result<ViewFrustum>::failure(frustumError(
            "Clip depth range is invalid",
            "ViewFrustum extraction received an enum value outside the supported depth ranges.",
            "ViewFrustum::fromViewProjection(depthRange)"));
    }
    extracted.planes[Far] = FrustumPlane{row3 - row2};

    return extracted.normalized();
}

Result<bool> ViewFrustum::intersects(const Bounds3d& bounds) const noexcept {
    if (!bounds.isValid()) {
        return Result<bool>::failure(frustumError(
            "Bounds are invalid",
            "ViewFrustum intersection requires finite, axis-ordered Bounds3d coordinates.",
            "ViewFrustum::intersects(bounds)"));
    }

    for (std::size_t index = 0; index < planes.size(); ++index) {
        const FrustumPlane& plane = planes[index];
        if (!isValidPlaneForEvaluation(plane)) {
            return Result<bool>::failure(frustumError(
                "Frustum plane is invalid",
                "ViewFrustum intersection requires finite planes with non-zero normals.",
                "ViewFrustum::intersects(plane)"));
        }

        const glm::dvec3 normal{plane.equation.x, plane.equation.y, plane.equation.z};
        const glm::dvec3 positiveVertex{
            normal.x >= 0.0 ? bounds.maximum.x : bounds.minimum.x,
            normal.y >= 0.0 ? bounds.maximum.y : bounds.minimum.y,
            normal.z >= 0.0 ? bounds.maximum.z : bounds.minimum.z};
        const double distance = glm::dot(normal, positiveVertex) + plane.equation.w;
        if (!std::isfinite(distance)) {
            return Result<bool>::failure(frustumError(
                "Frustum intersection calculation is not finite",
                "Evaluating a finite plane against Bounds3d produced a non-finite distance.",
                "ViewFrustum::intersects(distance)"));
        }
        if (distance < 0.0) {
            return Result<bool>::success(false);
        }
    }

    return Result<bool>::success(true);
}

} // namespace dzc
