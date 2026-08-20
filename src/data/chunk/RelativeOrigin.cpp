#include "data/chunk/RelativeOrigin.h"

#include <cmath>
#include <cstdint>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

bool isFinite(const glm::dvec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isFinite(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Error relativeOriginError(
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

Result<glm::vec3> RelativeOrigin::calculate(
    const glm::dvec3& chunkOrigin,
    const glm::dvec3& cameraOrigin) noexcept {
    if (!isFinite(chunkOrigin)) {
        return Result<glm::vec3>::failure(relativeOriginError(
            "Chunk origin is not finite",
            "RelativeOrigin requires all Chunk-origin components to be finite doubles.",
            "RelativeOrigin::calculate(chunkOrigin)"));
    }

    if (!isFinite(cameraOrigin)) {
        return Result<glm::vec3>::failure(relativeOriginError(
            "Camera origin is not finite",
            "RelativeOrigin requires all camera-origin components to be finite doubles.",
            "RelativeOrigin::calculate(cameraOrigin)"));
    }

    const glm::dvec3 relativeDouble = chunkOrigin - cameraOrigin;
    if (!isFinite(relativeDouble)) {
        return Result<glm::vec3>::failure(relativeOriginError(
            "Relative origin is not finite",
            "Subtracting the camera origin from the Chunk origin overflowed or produced a non-finite double offset.",
            "RelativeOrigin::calculate(chunkOrigin - cameraOrigin)"));
    }

    const glm::vec3 relativeOrigin{
        static_cast<float>(relativeDouble.x),
        static_cast<float>(relativeDouble.y),
        static_cast<float>(relativeDouble.z)};
    if (!isFinite(relativeOrigin)) {
        return Result<glm::vec3>::failure(relativeOriginError(
            "Relative origin exceeds float range",
            "A finite double Chunk-to-camera offset could not be represented as a finite float3.",
            "RelativeOrigin::calculate(float conversion)"));
    }

    return Result<glm::vec3>::success(relativeOrigin);
}

} // namespace dzc
