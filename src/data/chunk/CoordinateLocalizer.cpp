#include "data/chunk/CoordinateLocalizer.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Error localizationError(
    const char* userMessage,
    const char* diagnosticMessage,
    const char* context) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        userMessage,
        diagnosticMessage,
        context};
}

} // namespace

Result<CoordinateLocalizationResult> CoordinateLocalizer::localize(
    const std::vector<glm::dvec3>& sourcePositions) noexcept {
    if (sourcePositions.empty()) {
        return Result<CoordinateLocalizationResult>::failure(localizationError(
            "Source positions are empty",
            "CoordinateLocalizer requires at least one finite source position to determine a Chunk origin.",
            "CoordinateLocalizer::localize"));
    }

    Bounds3d bounds;
    for (const glm::dvec3& sourcePosition : sourcePositions) {
        if (!isFinite(sourcePosition)) {
            return Result<CoordinateLocalizationResult>::failure(localizationError(
                "Source position is not finite",
                "CoordinateLocalizer cannot localize a Position containing NaN or positive/negative infinity.",
                "CoordinateLocalizer::localize(sourcePositions)"));
        }

        const Result<void> extendResult = bounds.extend(sourcePosition);
        if (!extendResult.hasValue()) {
            return Result<CoordinateLocalizationResult>::failure(extendResult.error());
        }
    }

    const Result<glm::dvec3> centerResult = bounds.center();
    if (!centerResult.hasValue() || !isFinite(centerResult.value())) {
        return Result<CoordinateLocalizationResult>::failure(localizationError(
            "Chunk origin is not finite",
            "The source bounds center overflowed or otherwise could not be represented as a finite double origin.",
            "CoordinateLocalizer::localize(bounds.center)"));
    }

    const glm::dvec3 origin = centerResult.value();
    std::vector<glm::vec3> localPositions;
    localPositions.reserve(sourcePositions.size());

    for (const glm::dvec3& sourcePosition : sourcePositions) {
        const glm::dvec3 localDouble = sourcePosition - origin;
        if (!isFinite(localDouble)) {
            return Result<CoordinateLocalizationResult>::failure(localizationError(
                "Local position is not finite",
                "Subtracting the Chunk origin from a source position overflowed or produced a non-finite double offset.",
                "CoordinateLocalizer::localize(sourcePosition - origin)"));
        }

        const glm::vec3 localPosition{
            static_cast<float>(localDouble.x),
            static_cast<float>(localDouble.y),
            static_cast<float>(localDouble.z)};
        if (!std::isfinite(localPosition.x) ||
            !std::isfinite(localPosition.y) ||
            !std::isfinite(localPosition.z)) {
            return Result<CoordinateLocalizationResult>::failure(localizationError(
                "Local position exceeds float range",
                "A finite double local offset could not be represented as a finite float3.",
                "CoordinateLocalizer::localize(float conversion)"));
        }

        localPositions.push_back(localPosition);
    }

    CoordinateLocalizationResult result;
    result.bounds = bounds;
    result.origin = origin;
    result.localPositions = std::move(localPositions);
    return Result<CoordinateLocalizationResult>::success(std::move(result));
}

} // namespace dzc