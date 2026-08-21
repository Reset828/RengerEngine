#include <dzc/OrbitCameraController.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidArgumentCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr double kVerticalFieldOfViewRadians = 0.78539816339744830962;
constexpr double kDefaultNearPlane = 0.001;
constexpr double kDefaultFarPlane = 1000.0;
constexpr double kDefaultDistance = 3.0;
constexpr double kMinimumInteractiveDistance = 0.1;
constexpr double kMaximumInteractiveDistance = 1000.0;
constexpr double kTrackballThreshold = 0.70710678118654752440;
constexpr double kRotationSensitivity = 4.71238898038;
constexpr double kMinimumWorldZUp = -1.0e-6;
constexpr int kConstraintSearchIterations = 16;

Error inputError(const char* userMessage, const char* diagnosticMessage, const char* context) {
    return Error{ErrorDomain::General, kInvalidArgumentCode, userMessage, diagnosticMessage, context};
}

Error dataError(const char* userMessage, const char* diagnosticMessage, const char* context) {
    return Error{ErrorDomain::DataFormat, kCorruptDataCode, userMessage, diagnosticMessage, context};
}

bool isFinite(double value) noexcept {
    return std::isfinite(value);
}

bool isFinite(const glm::dvec2& value) noexcept {
    return isFinite(value.x) && isFinite(value.y);
}

bool isFinite(const glm::dvec3& value) noexcept {
    return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

bool isFinite(const glm::dquat& value) noexcept {
    return isFinite(value.w) && isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

bool isFinite(const glm::vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

bool isValidPointerPosition(const InputEvent& event) noexcept {
    return isFinite(event.valueX) && isFinite(event.valueY) &&
           event.valueX >= 0.0 && event.valueX <= 1.0 &&
           event.valueY >= 0.0 && event.valueY <= 1.0;
}

bool isValidRenderSize(const RenderSize& size) noexcept {
    return size.width > 0U && size.height > 0U &&
           std::isfinite(size.devicePixelRatio) && size.devicePixelRatio > 0.0F;
}

bool safeLength(const glm::dvec3& value, double& result) noexcept {
    if (!isFinite(value)) {
        return false;
    }

    const double scale = std::max(std::abs(value.x), std::max(std::abs(value.y), std::abs(value.z)));
    if (scale == 0.0) {
        result = 0.0;
        return true;
    }

    const glm::dvec3 scaled = value / scale;
    const double length = scale * std::sqrt(glm::dot(scaled, scaled));
    if (!isFinite(length)) {
        return false;
    }

    result = length;
    return true;
}

bool boundsCenterAndRadius(const Bounds3d& bounds, glm::dvec3& center, double& radius) noexcept {
    if (!bounds.isValid()) {
        return false;
    }

    center = bounds.minimum * 0.5 + bounds.maximum * 0.5;
    const glm::dvec3 halfExtent = bounds.maximum * 0.5 - bounds.minimum * 0.5;
    return isFinite(center) && safeLength(halfExtent, radius);
}

glm::dvec3 positionFor(const glm::dvec3& target, const glm::dquat& orientation, double distance) noexcept {
    return target + orientation * glm::dvec3{0.0, 0.0, distance};
}

bool isOrientationAllowed(const glm::dquat& orientation) noexcept {
    const glm::dvec3 worldLocalPositiveZ = orientation * glm::dvec3{0.0, 0.0, 1.0};
    return isFinite(worldLocalPositiveZ) && worldLocalPositiveZ.y >= kMinimumWorldZUp;
}

glm::dquat localRotation(const glm::dquat& orientation, const glm::dvec3& localAxis, double angle) noexcept {
    return glm::normalize(orientation * glm::angleAxis(angle, glm::normalize(localAxis)));
}

bool makeMatrices(const CameraState& state, const RenderSize& size, CameraMatrices& matrices) noexcept {
    if (!isValidRenderSize(size) || !isFinite(state.position) || !isFinite(state.orientation) ||
        !isFinite(state.verticalFieldOfViewRadians) || !isFinite(state.nearPlane) ||
        !isFinite(state.farPlane) || state.verticalFieldOfViewRadians <= 0.0 ||
        state.nearPlane <= 0.0 || state.farPlane <= state.nearPlane) {
        return false;
    }

    const double aspectDouble = static_cast<double>(size.width) / static_cast<double>(size.height);
    if (!isFinite(aspectDouble) || aspectDouble <= 0.0 ||
        aspectDouble > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }

    const glm::dmat4 inverseOrientation = glm::mat4_cast(glm::conjugate(state.orientation));
    const glm::mat4 view{inverseOrientation};
    const glm::mat4 projection = glm::perspectiveRH_NO(
        static_cast<float>(state.verticalFieldOfViewRadians),
        static_cast<float>(aspectDouble),
        static_cast<float>(state.nearPlane),
        static_cast<float>(state.farPlane));
    if (!isFinite(view) || !isFinite(projection)) {
        return false;
    }

    matrices = CameraMatrices{view, projection, state.position};
    return true;
}

bool makeWorldFrustum(const CameraMatrices& matrices, ViewFrustum& frustum) noexcept {
    const auto relativeFrustum = ViewFrustum::fromViewProjection(
        matrices.projection * matrices.view,
        ClipDepthRange::NegativeOneToOne);
    if (!relativeFrustum.hasValue()) {
        return false;
    }

    ViewFrustum worldFrustum = relativeFrustum.value();
    for (FrustumPlane& plane : worldFrustum.planes) {
        const glm::dvec3 normal{plane.equation.x, plane.equation.y, plane.equation.z};
        const double translatedD = plane.equation.w - glm::dot(normal, matrices.cameraOrigin);
        if (!isFinite(normal) || !isFinite(translatedD)) {
            return false;
        }
        plane.equation.w = translatedD;
    }

    frustum = worldFrustum;
    return true;
}

} // namespace

class OrbitCameraController::Impl final {
public:
    enum class DragMode : std::uint8_t {
        None,
        Rotate,
        Pan
    };

    glm::dvec3 target{0.0};
    double distance{kDefaultDistance};
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 lastVerticalLocalAxis{1.0, 0.0, 0.0};
    DragMode dragMode{DragMode::None};
    glm::dvec2 lastPointer{0.0};
    bool hasPointer{false};
    bool pendingReset{false};
    std::optional<RenderSize> lastValidSize;
    std::optional<Bounds3d> lastSceneBounds;
    mutable std::optional<CameraMatrices> lastMatrices;
    mutable std::optional<ViewFrustum> lastFrustum;
    CameraState stateValue{};

    Impl() {
        CameraState initialState;
        if (makeState(target, orientation, distance, kDefaultNearPlane, kDefaultFarPlane, initialState)) {
            stateValue = initialState;
        }
    }

    static bool makeState(const glm::dvec3& candidateTarget,
                          const glm::dquat& candidateOrientation,
                          double candidateDistance,
                          double nearPlane,
                          double farPlane,
                          CameraState& result) noexcept {
        const glm::dvec3 position = positionFor(candidateTarget, candidateOrientation, candidateDistance);
        if (!isFinite(candidateTarget) || !isFinite(position) || !isFinite(candidateOrientation) ||
            !isFinite(candidateDistance) || candidateDistance < 0.0 || !isFinite(nearPlane) ||
            !isFinite(farPlane) || nearPlane <= 0.0 || farPlane <= nearPlane) {
            return false;
        }

        result.position = position;
        result.orientation = candidateOrientation;
        result.verticalFieldOfViewRadians = kVerticalFieldOfViewRadians;
        result.nearPlane = nearPlane;
        result.farPlane = farPlane;
        return true;
    }

    void clearDrag() noexcept {
        dragMode = DragMode::None;
        hasPointer = false;
    }

    bool calculateReset(const Bounds3d& bounds, glm::dvec3& resetTarget, double& resetDistance) const noexcept {
        if (!lastValidSize.has_value()) {
            return false;
        }

        double radius = 0.0;
        if (!boundsCenterAndRadius(bounds, resetTarget, radius)) {
            return false;
        }

        const double aspect = static_cast<double>(lastValidSize->width) /
            static_cast<double>(lastValidSize->height);
        const double verticalHalfAngle = kVerticalFieldOfViewRadians * 0.5;
        const double verticalTangent = std::tan(verticalHalfAngle);
        const double horizontalHalfAngle = std::atan(verticalTangent * aspect);
        const double horizontalTangent = std::tan(horizontalHalfAngle);
        const double limitingTangent = std::min(verticalTangent, horizontalTangent);
        const double paddedRadius = radius * 1.05;
        if (!isFinite(aspect) || !isFinite(verticalTangent) || !isFinite(horizontalTangent) ||
            !isFinite(limitingTangent) || limitingTangent <= 0.0 || !isFinite(paddedRadius)) {
            return false;
        }

        resetDistance = paddedRadius / limitingTangent;
        return isFinite(resetDistance) && resetDistance >= 0.0 &&
               isFinite(positionFor(resetTarget, glm::dquat{1.0, 0.0, 0.0, 0.0}, resetDistance));
    }

    bool calculateDynamicPlanes(const glm::dvec3& candidateTarget,
                                const glm::dquat& candidateOrientation,
                                double candidateDistance,
                                const Bounds3d& bounds,
                                double& nearPlane,
                                double& farPlane) const noexcept {
        glm::dvec3 center{0.0};
        double radius = 0.0;
        if (!boundsCenterAndRadius(bounds, center, radius)) {
            return false;
        }

        const glm::dvec3 position = positionFor(candidateTarget, candidateOrientation, candidateDistance);
        double sceneDistance = 0.0;
        if (!isFinite(position) || !safeLength(position - center, sceneDistance)) {
            return false;
        }

        nearPlane = std::max(kDefaultNearPlane, 0.9 * std::max(sceneDistance - radius, 0.0));
        farPlane = std::max(nearPlane * 2.0, 1.1 * (sceneDistance + radius));
        return isFinite(nearPlane) && isFinite(farPlane) && nearPlane > 0.0 && farPlane > nearPlane;
    }
};

OrbitCameraController::OrbitCameraController()
    : mImpl(std::make_unique<Impl>()) {}

OrbitCameraController::~OrbitCameraController() = default;
OrbitCameraController::OrbitCameraController(OrbitCameraController&&) noexcept = default;
OrbitCameraController& OrbitCameraController::operator=(OrbitCameraController&&) noexcept = default;

Result<void> OrbitCameraController::submitInput(const InputEvent& event) {
    if (mImpl == nullptr) {
        return Result<void>::failure(dataError(
            "Orbit camera is unavailable",
            "The moved-from orbit camera controller has no implementation state.",
            "OrbitCameraController::submitInput"));
    }

    switch (event.type) {
    case InputEventType::PointerMove: {
        if (!isValidPointerPosition(event)) {
            return Result<void>::failure(inputError(
                "Pointer position is invalid",
                "PointerMove requires finite normalized coordinates in the inclusive range [0, 1].",
                "OrbitCameraController::submitInput(PointerMove)"));
        }

        const glm::dvec2 current{event.valueX, event.valueY};
        if (mImpl->dragMode == Impl::DragMode::None) {
            mImpl->lastPointer = current;
            mImpl->hasPointer = true;
            return Result<void>::success();
        }
        if (!mImpl->lastValidSize.has_value()) {
            return Result<void>::failure(dataError(
                "Camera viewport is unavailable",
                "Pointer dragging requires a previously supplied valid RenderSize.",
                "OrbitCameraController::submitInput(PointerMove)"));
        }

        const glm::dvec2 previous = mImpl->lastPointer;
        const RenderSize size = *mImpl->lastValidSize;
        const double width = static_cast<double>(size.width);
        const double height = static_cast<double>(size.height);
        const double minExtent = std::min(width, height);
        if (minExtent <= 0.0 || !isFinite(minExtent)) {
            return Result<void>::failure(dataError(
                "Camera viewport is invalid",
                "The cached RenderSize cannot produce a finite trackball mapping.",
                "OrbitCameraController::submitInput(PointerMove)"));
        }

        glm::dvec3 candidateTarget = mImpl->target;
        glm::dquat candidateOrientation = mImpl->orientation;
        glm::dvec3 candidateVerticalAxis = mImpl->lastVerticalLocalAxis;
        if (mImpl->dragMode == Impl::DragMode::Rotate) {
            const auto project = [width, height, minExtent](const glm::dvec2& pointer, glm::dvec3& sphere) noexcept {
                const double x = (2.0 * pointer.x - 1.0) * width / minExtent;
                const double y = (1.0 - 2.0 * pointer.y) * height / minExtent;
                const double d = std::sqrt(x * x + y * y);
                const double z = d <= kTrackballThreshold ? std::sqrt(std::max(0.0, 1.0 - d * d)) : 0.5 / d;
                sphere = glm::normalize(glm::dvec3{x, y, z});
                return isFinite(sphere);
            };

            glm::dvec3 sphereFrom{0.0};
            glm::dvec3 sphereTo{0.0};
            if (!project(previous, sphereFrom) || !project(current, sphereTo)) {
                return Result<void>::failure(dataError(
                    "Trackball projection failed",
                    "Normalized pointer coordinates could not produce a finite virtual-sphere point.",
                    "OrbitCameraController::submitInput(PointerMove)"));
            }

            const glm::dvec3 sphereCross = glm::cross(sphereFrom, sphereTo);
            double sineAngle = 0.0;
            if (!safeLength(sphereCross, sineAngle)) {
                return Result<void>::failure(dataError(
                    "Trackball rotation failed",
                    "The virtual-sphere rotation axis is not finite.",
                    "OrbitCameraController::submitInput(PointerMove)"));
            }

            if (sineAngle > std::numeric_limits<double>::epsilon()) {
                const glm::dvec2 screenDelta{
                    (current.x - previous.x) * width / minExtent,
                    (current.y - previous.y) * height / minExtent};
                double uniformAngle = 0.0;
                if (!safeLength(glm::dvec3{screenDelta.x, screenDelta.y, 0.0}, uniformAngle)) {
                    return Result<void>::failure(dataError(
                        "Trackball rotation failed",
                        "The normalized pointer delta is not finite.",
                        "OrbitCameraController::submitInput(PointerMove)"));
                }
                uniformAngle *= kRotationSensitivity;

                glm::dvec2 rotationAxisXY{sphereCross.x, sphereCross.y};
                const double axisLength = std::sqrt(glm::dot(rotationAxisXY, rotationAxisXY));
                if (axisLength <= 1.0e-6) {
                    rotationAxisXY = glm::dvec2{screenDelta.y, screenDelta.x};
                }
                const double normalizedAxisLength = std::sqrt(glm::dot(rotationAxisXY, rotationAxisXY));
                if (normalizedAxisLength > std::numeric_limits<double>::epsilon()) {
                    rotationAxisXY /= normalizedAxisLength;
                    const glm::dvec3 sphereRotation{
                        rotationAxisXY.x * uniformAngle,
                        rotationAxisXY.y * uniformAngle,
                        0.0};

                    const auto applyConstrained = [&candidateOrientation](const glm::dvec3& axis, double angle) noexcept {
                        if (std::abs(angle) <= std::numeric_limits<double>::epsilon()) {
                            return true;
                        }
                        const glm::dquat fullCandidate = localRotation(candidateOrientation, axis, angle);
                        if (!isFinite(fullCandidate)) {
                            return false;
                        }
                        if (isOrientationAllowed(fullCandidate)) {
                            candidateOrientation = fullCandidate;
                            return true;
                        }

                        double lower = 0.0;
                        double upper = 1.0;
                        for (int iteration = 0; iteration < kConstraintSearchIterations; ++iteration) {
                            const double middle = (lower + upper) * 0.5;
                            const glm::dquat partial = localRotation(candidateOrientation, axis, angle * middle);
                            if (!isFinite(partial)) {
                                return false;
                            }
                            if (isOrientationAllowed(partial)) {
                                lower = middle;
                            } else {
                                upper = middle;
                            }
                        }
                        candidateOrientation = localRotation(candidateOrientation, axis, angle * lower);
                        return isFinite(candidateOrientation) && isOrientationAllowed(candidateOrientation);
                    };

                    if (!applyConstrained(glm::dvec3{0.0, 0.0, 1.0}, sphereRotation.y)) {
                        return Result<void>::failure(dataError(
                            "Camera rotation failed",
                            "The constrained trackball rotation produced a non-finite orientation.",
                            "OrbitCameraController::submitInput(PointerMove)"));
                    }

                    if (std::abs(sphereRotation.x) > std::numeric_limits<double>::epsilon()) {
                        const glm::dvec3 worldLocalX = candidateOrientation * glm::dvec3{1.0, 0.0, 0.0};
                        const glm::dvec3 worldLocalY = candidateOrientation * glm::dvec3{0.0, 1.0, 0.0};
                        const glm::dvec2 localWeights{worldLocalX.x, worldLocalY.x};
                        const double localWeightLength = std::sqrt(glm::dot(localWeights, localWeights));
                        glm::dvec3 verticalAxis = candidateVerticalAxis;
                        if (isFinite(worldLocalX) && isFinite(worldLocalY) && localWeightLength > 1.0e-4) {
                            verticalAxis = glm::normalize(glm::dvec3{localWeights.x, localWeights.y, 0.0});
                            candidateVerticalAxis = verticalAxis;
                        }
                        if (!applyConstrained(verticalAxis, sphereRotation.x)) {
                            return Result<void>::failure(dataError(
                                "Camera rotation failed",
                                "The constrained trackball rotation produced a non-finite orientation.",
                                "OrbitCameraController::submitInput(PointerMove)"));
                        }
                    }
                }
            }
        } else {
            const double aspect = width / height;
            const double visibleHeight = 2.0 * mImpl->distance * std::tan(kVerticalFieldOfViewRadians * 0.5);
            const double visibleWidth = visibleHeight * aspect;
            const glm::dvec2 delta = current - previous;
            const glm::dvec3 right = mImpl->orientation * glm::dvec3{1.0, 0.0, 0.0};
            const glm::dvec3 up = mImpl->orientation * glm::dvec3{0.0, 1.0, 0.0};
            candidateTarget += right * (delta.x * visibleWidth) - up * (delta.y * visibleHeight);
        }

        const glm::dvec3 candidatePosition = positionFor(candidateTarget, candidateOrientation, mImpl->distance);
        if (!isFinite(candidateTarget) || !isFinite(candidateOrientation) || !isFinite(candidatePosition)) {
            return Result<void>::failure(dataError(
                "Camera interaction produced invalid coordinates",
                "The requested camera rotation or pan cannot be represented as finite double-precision values.",
                "OrbitCameraController::submitInput(PointerMove)"));
        }

        CameraState candidateState;
        if (!Impl::makeState(candidateTarget, candidateOrientation, mImpl->distance,
                             mImpl->stateValue.nearPlane, mImpl->stateValue.farPlane, candidateState)) {
            return Result<void>::failure(dataError(
                "Camera state update failed",
                "The requested interaction cannot produce a finite camera state.",
                "OrbitCameraController::submitInput(PointerMove)"));
        }
        mImpl->target = candidateTarget;
        mImpl->orientation = candidateOrientation;
        mImpl->lastVerticalLocalAxis = candidateVerticalAxis;
        mImpl->lastPointer = current;
        mImpl->hasPointer = true;
        mImpl->stateValue = candidateState;
        return Result<void>::success();
    }

    case InputEventType::PointerButton:
        if (!event.pressed) {
            mImpl->clearDrag();
            return Result<void>::success();
        }
        if (!isValidPointerPosition(event)) {
            return Result<void>::failure(inputError(
                "Pointer position is invalid",
                "PointerButton press requires finite normalized coordinates in the inclusive range [0, 1].",
                "OrbitCameraController::submitInput(PointerButton)"));
        }
        if (event.code != 0U && event.code != 2U) {
            return Result<void>::failure(inputError(
                "Pointer button is unsupported",
                "Only abstract pointer button codes 0 (rotate) and 2 (pan) are supported.",
                "OrbitCameraController::submitInput(PointerButton)"));
        }
        mImpl->dragMode = event.code == 0U ? Impl::DragMode::Rotate : Impl::DragMode::Pan;
        mImpl->lastPointer = {event.valueX, event.valueY};
        mImpl->hasPointer = true;
        return Result<void>::success();

    case InputEventType::Wheel: {
        if (!isFinite(event.valueY)) {
            return Result<void>::failure(inputError(
                "Wheel input is invalid",
                "Wheel requires a finite valueY.",
                "OrbitCameraController::submitInput(Wheel)"));
        }
        double candidateDistance = mImpl->distance;
        if (event.valueY > 0.0) {
            candidateDistance *= 0.9;
        } else if (event.valueY < 0.0) {
            candidateDistance *= 1.1;
        }
        candidateDistance = std::max(kMinimumInteractiveDistance,
            std::min(kMaximumInteractiveDistance, candidateDistance));
        const glm::dvec3 candidatePosition = positionFor(mImpl->target, mImpl->orientation, candidateDistance);
        if (!isFinite(candidateDistance) || !isFinite(candidatePosition)) {
            return Result<void>::failure(dataError(
                "Camera zoom failed",
                "The requested wheel interaction cannot produce a finite camera distance and position.",
                "OrbitCameraController::submitInput(Wheel)"));
        }
        CameraState candidateState;
        if (!Impl::makeState(mImpl->target, mImpl->orientation, candidateDistance,
                             mImpl->stateValue.nearPlane, mImpl->stateValue.farPlane, candidateState)) {
            return Result<void>::failure(dataError(
                "Camera state update failed",
                "The requested wheel interaction cannot produce a finite camera state.",
                "OrbitCameraController::submitInput(Wheel)"));
        }
        mImpl->distance = candidateDistance;
        mImpl->stateValue = candidateState;
        return Result<void>::success();
    }

    case InputEventType::Focus:
        if (!event.pressed) {
            mImpl->clearDrag();
        }
        return Result<void>::success();

    case InputEventType::ResetRequest:
        mImpl->pendingReset = true;
        return Result<void>::success();

    case InputEventType::Key:
        return Result<void>::success();
    }

    return Result<void>::failure(inputError(
        "Input event type is invalid",
        "The InputEvent type is outside the defined InputEventType enumeration.",
        "OrbitCameraController::submitInput"));
}

Result<void> OrbitCameraController::update(double deltaSeconds, const Bounds3d& sceneBounds) {
    if (mImpl == nullptr) {
        return Result<void>::failure(dataError(
            "Orbit camera is unavailable",
            "The moved-from orbit camera controller has no implementation state.",
            "OrbitCameraController::update"));
    }
    if (!isFinite(deltaSeconds) || deltaSeconds < 0.0) {
        return Result<void>::failure(dataError(
            "Camera update delta is invalid",
            "Camera update requires a finite deltaSeconds value that is greater than or equal to zero.",
            "OrbitCameraController::update"));
    }
    if (!sceneBounds.isValid()) {
        return Result<void>::failure(dataError(
            "Scene bounds are invalid",
            "Camera update requires finite, axis-ordered scene bounds.",
            "OrbitCameraController::update"));
    }

    glm::dvec3 candidateTarget = mImpl->target;
    double candidateDistance = mImpl->distance;
    glm::dquat candidateOrientation = mImpl->orientation;
    bool clearInteraction = false;
    bool clearPendingReset = false;
    if (mImpl->pendingReset) {
        if (!mImpl->calculateReset(sceneBounds, candidateTarget, candidateDistance)) {
            return Result<void>::failure(dataError(
                "Camera reset cannot be completed",
                "A pending reset requires a valid cached RenderSize and finite frameable scene bounds.",
                "OrbitCameraController::update"));
        }
        candidateOrientation = glm::dquat{1.0, 0.0, 0.0, 0.0};
        clearInteraction = true;
        clearPendingReset = true;
    }

    double nearPlane = 0.0;
    double farPlane = 0.0;
    if (!mImpl->calculateDynamicPlanes(
            candidateTarget, candidateOrientation, candidateDistance, sceneBounds, nearPlane, farPlane)) {
        return Result<void>::failure(dataError(
            "Camera clipping range is invalid",
            "The scene bounds and camera position cannot produce finite dynamic near and far planes.",
            "OrbitCameraController::update"));
    }

    const glm::dvec3 candidatePosition = positionFor(candidateTarget, candidateOrientation, candidateDistance);
    if (!isFinite(candidatePosition)) {
        return Result<void>::failure(dataError(
            "Camera position is invalid",
            "The update cannot produce a finite camera position.",
            "OrbitCameraController::update"));
    }

    CameraState candidateState;
    if (!Impl::makeState(candidateTarget, candidateOrientation, candidateDistance,
                         nearPlane, farPlane, candidateState)) {
        return Result<void>::failure(dataError(
            "Camera state update failed",
            "The update cannot produce a finite camera state.",
            "OrbitCameraController::update"));
    }
    mImpl->target = candidateTarget;
    mImpl->distance = candidateDistance;
    mImpl->orientation = candidateOrientation;
    mImpl->lastSceneBounds = sceneBounds;
    if (clearInteraction) {
        mImpl->lastVerticalLocalAxis = {1.0, 0.0, 0.0};
        mImpl->clearDrag();
    }
    if (clearPendingReset) {
        mImpl->pendingReset = false;
    }
    mImpl->stateValue = candidateState;
    return Result<void>::success();
}

const CameraState& OrbitCameraController::state() const noexcept {
    static const CameraState unavailableState{};
    return mImpl != nullptr ? mImpl->stateValue : unavailableState;
}

CameraMatrices OrbitCameraController::matrices(const RenderSize& size) const {
    if (mImpl == nullptr || !isValidRenderSize(size)) {
        return mImpl != nullptr && mImpl->lastMatrices.has_value()
            ? *mImpl->lastMatrices
            : CameraMatrices{};
    }

    CameraMatrices generated;
    if (!makeMatrices(mImpl->stateValue, size, generated)) {
        return mImpl->lastMatrices.has_value() ? *mImpl->lastMatrices : CameraMatrices{};
    }

    mImpl->lastValidSize = size;
    mImpl->lastMatrices = generated;
    return generated;
}

ViewFrustum OrbitCameraController::frustum(const RenderSize& size) const {
    if (mImpl == nullptr || !isValidRenderSize(size)) {
        return mImpl != nullptr && mImpl->lastFrustum.has_value()
            ? *mImpl->lastFrustum
            : ViewFrustum{};
    }

    CameraMatrices generatedMatrices;
    ViewFrustum generatedFrustum;
    if (!makeMatrices(mImpl->stateValue, size, generatedMatrices) ||
        !makeWorldFrustum(generatedMatrices, generatedFrustum)) {
        return mImpl->lastFrustum.has_value() ? *mImpl->lastFrustum : ViewFrustum{};
    }

    mImpl->lastValidSize = size;
    mImpl->lastMatrices = generatedMatrices;
    mImpl->lastFrustum = generatedFrustum;
    return generatedFrustum;
}

Result<void> OrbitCameraController::reset(const Bounds3d& sceneBounds) {
    if (mImpl == nullptr) {
        return Result<void>::failure(dataError(
            "Orbit camera is unavailable",
            "The moved-from orbit camera controller has no implementation state.",
            "OrbitCameraController::reset"));
    }

    glm::dvec3 resetTarget{0.0};
    double resetDistance = 0.0;
    if (!sceneBounds.isValid()) {
        return Result<void>::failure(dataError(
            "Scene bounds are invalid",
            "Camera reset requires finite, axis-ordered scene bounds.",
            "OrbitCameraController::reset"));
    }
    if (!mImpl->lastValidSize.has_value()) {
        return Result<void>::failure(dataError(
            "Camera viewport is unavailable",
            "Camera reset requires a previously supplied valid RenderSize.",
            "OrbitCameraController::reset"));
    }
    if (!mImpl->calculateReset(sceneBounds, resetTarget, resetDistance)) {
        return Result<void>::failure(dataError(
            "Camera reset cannot be completed",
            "The scene bounds cannot produce a finite framing distance.",
            "OrbitCameraController::reset"));
    }

    double nearPlane = 0.0;
    double farPlane = 0.0;
    const glm::dquat resetOrientation{1.0, 0.0, 0.0, 0.0};
    if (!mImpl->calculateDynamicPlanes(
            resetTarget, resetOrientation, resetDistance, sceneBounds, nearPlane, farPlane)) {
        return Result<void>::failure(dataError(
            "Camera reset cannot be completed",
            "The reset camera cannot produce finite dynamic clipping planes.",
            "OrbitCameraController::reset"));
    }

    CameraState candidateState;
    if (!Impl::makeState(resetTarget, resetOrientation, resetDistance,
                         nearPlane, farPlane, candidateState)) {
        return Result<void>::failure(dataError(
            "Camera state update failed",
            "The reset cannot produce a finite camera state.",
            "OrbitCameraController::reset"));
    }
    mImpl->target = resetTarget;
    mImpl->distance = resetDistance;
    mImpl->orientation = resetOrientation;
    mImpl->lastVerticalLocalAxis = {1.0, 0.0, 0.0};
    mImpl->clearDrag();
    mImpl->pendingReset = false;
    mImpl->lastSceneBounds = sceneBounds;
    mImpl->stateValue = candidateState;
    return Result<void>::success();
}

} // namespace dzc
