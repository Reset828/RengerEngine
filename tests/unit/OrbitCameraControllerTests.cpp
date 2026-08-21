#include <dzc/OrbitCameraController.h>

#include <glm/gtc/constants.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

using dzc::Bounds3d;
using dzc::CameraMatrices;
using dzc::CameraState;
using dzc::Error;
using dzc::ErrorDomain;
using dzc::InputEvent;
using dzc::InputEventType;
using dzc::OrbitCameraController;
using dzc::RenderSize;
using dzc::Result;
using dzc::ViewFrustum;

constexpr double kTolerance = 1.0e-10;
constexpr double kFov = 0.78539816339744830962;

static_assert(std::is_base_of_v<dzc::ICameraController, OrbitCameraController>);
static_assert(std::is_final_v<OrbitCameraController>);
static_assert(!std::is_copy_constructible_v<OrbitCameraController>);
static_assert(!std::is_copy_assignable_v<OrbitCameraController>);
static_assert(std::is_move_constructible_v<OrbitCameraController>);
static_assert(std::is_move_assignable_v<OrbitCameraController>);

bool nearlyEqual(double lhs, double rhs, double tolerance = kTolerance) {
    return std::abs(lhs - rhs) <= tolerance;
}

bool sameVec3(const glm::dvec3& lhs, const glm::dvec3& rhs, double tolerance = kTolerance) {
    return nearlyEqual(lhs.x, rhs.x, tolerance) &&
           nearlyEqual(lhs.y, rhs.y, tolerance) &&
           nearlyEqual(lhs.z, rhs.z, tolerance);
}

bool sameQuat(const glm::dquat& lhs, const glm::dquat& rhs, double tolerance = kTolerance) {
    return nearlyEqual(lhs.w, rhs.w, tolerance) && nearlyEqual(lhs.x, rhs.x, tolerance) &&
           nearlyEqual(lhs.y, rhs.y, tolerance) && nearlyEqual(lhs.z, rhs.z, tolerance);
}

bool sameState(const CameraState& lhs, const CameraState& rhs) {
    return sameVec3(lhs.position, rhs.position) && sameQuat(lhs.orientation, rhs.orientation) &&
           nearlyEqual(lhs.verticalFieldOfViewRadians, rhs.verticalFieldOfViewRadians) &&
           nearlyEqual(lhs.nearPlane, rhs.nearPlane) && nearlyEqual(lhs.farPlane, rhs.farPlane);
}

void assertSuccess(const Result<void>& result) {
    assert(result.hasValue());
}

void assertDataFormat(const Result<void>& result) {
    assert(!result.hasValue());
    const Error& error = result.error();
    assert(error.domain == ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

void assertInvalidArgument(const Result<void>& result) {
    assert(!result.hasValue());
    const Error& error = result.error();
    assert(error.domain == ErrorDomain::General);
    assert(error.code == 1U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

Bounds3d bounds(const glm::dvec3& minimum, const glm::dvec3& maximum) {
    Bounds3d result;
    result.minimum = minimum;
    result.maximum = maximum;
    return result;
}

InputEvent pointerButton(std::uint32_t code, bool pressed, double x, double y) {
    InputEvent event;
    event.type = InputEventType::PointerButton;
    event.code = code;
    event.pressed = pressed;
    event.valueX = x;
    event.valueY = y;
    return event;
}

InputEvent pointerMove(double x, double y) {
    InputEvent event;
    event.type = InputEventType::PointerMove;
    event.valueX = x;
    event.valueY = y;
    return event;
}

InputEvent wheel(double valueY) {
    InputEvent event;
    event.type = InputEventType::Wheel;
    event.valueY = valueY;
    return event;
}

void testDefaultStateMatricesAndWorldFrustum() {
    OrbitCameraController controller;
    const CameraState& state = controller.state();
    assert(sameVec3(state.position, {0.0, 0.0, 3.0}));
    assert(sameQuat(state.orientation, {1.0, 0.0, 0.0, 0.0}));
    assert(nearlyEqual(state.verticalFieldOfViewRadians, kFov));
    assert(nearlyEqual(state.nearPlane, 0.001));
    assert(nearlyEqual(state.farPlane, 1000.0));

    const CameraMatrices noSize = controller.matrices(RenderSize{});
    assert((noSize.view == glm::mat4{1.0F}));
    assert((noSize.projection == glm::mat4{1.0F}));
    assert((noSize.cameraOrigin == glm::dvec3{0.0}));
    const ViewFrustum noSizeFrustum = controller.frustum(RenderSize{});
    for (const auto& plane : noSizeFrustum.planes) {
        assert((plane.equation == glm::dvec4{0.0}));
    }

    const CameraMatrices matrices = controller.matrices(RenderSize{800U, 600U, 1.0F});
    assert((matrices.cameraOrigin == glm::dvec3{0.0, 0.0, 3.0}));
    assert((matrices.view == glm::mat4{1.0F}));
    assert(matrices.projection[2][3] == -1.0F);
    assert(matrices.projection[3][2] < 0.0F);

    const ViewFrustum frustum = controller.frustum(RenderSize{800U, 600U, 1.0F});
    assert(frustum.intersects(bounds({-0.5, -0.5, -0.5}, {0.5, 0.5, 0.5})).value());
    assert(!frustum.intersects(bounds({-0.1, -0.1, 3.1}, {0.1, 0.1, 3.5})).value());
}

void testRotationPanReleaseAndFocus() {
    OrbitCameraController controller;
    const RenderSize size{800U, 600U, 1.0F};
    controller.matrices(size);

    assertSuccess(controller.submitInput(pointerButton(0U, true, 0.5, 0.5)));
    assertSuccess(controller.submitInput(pointerMove(0.7, 0.35)));
    const CameraState rotated = controller.state();
    assert(!sameQuat(rotated.orientation, glm::dquat{1.0, 0.0, 0.0, 0.0}));
    const glm::dvec3 worldPositiveZ = rotated.orientation * glm::dvec3{0.0, 0.0, 1.0};
    assert(worldPositiveZ.y >= -1.0e-6 - kTolerance);

    for (int index = 0; index < 10; ++index) {
        assertSuccess(controller.submitInput(pointerMove(0.7, 1.0)));
        assertSuccess(controller.submitInput(pointerMove(0.7, 0.0)));
    }
    const glm::dvec3 constrainedPositiveZ =
        controller.state().orientation * glm::dvec3{0.0, 0.0, 1.0};
    assert(constrainedPositiveZ.y >= -1.0e-6 - kTolerance);

    assertSuccess(controller.submitInput(pointerButton(99U, false, -10.0, 10.0)));
    const CameraState afterRelease = controller.state();
    assertSuccess(controller.submitInput(pointerMove(0.2, 0.2)));
    assert(sameState(controller.state(), afterRelease));

    assertSuccess(controller.submitInput(pointerButton(2U, true, 0.5, 0.5)));
    assertSuccess(controller.submitInput(pointerMove(0.6, 0.5)));
    const CameraState panned = controller.state();
    assert(!sameVec3(panned.position, afterRelease.position));

    InputEvent focusLost;
    focusLost.type = InputEventType::Focus;
    focusLost.pressed = false;
    assertSuccess(controller.submitInput(focusLost));
    const CameraState afterFocus = controller.state();
    assertSuccess(controller.submitInput(pointerMove(0.7, 0.6)));
    assert(sameState(controller.state(), afterFocus));
}

void testWheelAndInvalidInputAreAtomic() {
    OrbitCameraController controller;
    const CameraState before = controller.state();

    assertSuccess(controller.submitInput(wheel(1.0)));
    assert(nearlyEqual(controller.state().position.z, 2.7));
    assertSuccess(controller.submitInput(wheel(-123.0)));
    assert(nearlyEqual(controller.state().position.z, 2.97));
    assertSuccess(controller.submitInput(wheel(0.0)));
    assert(nearlyEqual(controller.state().position.z, 2.97));

    for (int index = 0; index < 100; ++index) {
        assertSuccess(controller.submitInput(wheel(1.0)));
    }
    assert(nearlyEqual(controller.state().position.z, 0.1));
    for (int index = 0; index < 100; ++index) {
        assertSuccess(controller.submitInput(wheel(-1.0)));
    }
    assert(nearlyEqual(controller.state().position.z, 1000.0));

    const CameraState stable = controller.state();
    assertInvalidArgument(controller.submitInput(pointerMove(-0.01, 0.5)));
    assert(sameState(controller.state(), stable));
    assertInvalidArgument(controller.submitInput(pointerButton(0U, true,
        std::numeric_limits<double>::quiet_NaN(), 0.5)));
    assert(sameState(controller.state(), stable));
    assertInvalidArgument(controller.submitInput(pointerButton(9U, true, 0.5, 0.5)));
    assert(sameState(controller.state(), stable));
    assertInvalidArgument(controller.submitInput(wheel(std::numeric_limits<double>::infinity())));
    assert(sameState(controller.state(), stable));
    assert(!sameState(before, stable));
}

void testResetPendingUpdateAndDynamicPlanes() {
    const RenderSize size{1600U, 800U, 1.0F};
    const Bounds3d scene = bounds({-1.0, -2.0, -3.0}, {1.0, 2.0, 3.0});

    OrbitCameraController controller;
    const CameraState beforeReset = controller.state();
    assertDataFormat(controller.reset(scene));
    assert(sameState(controller.state(), beforeReset));

    controller.matrices(size);
    assertSuccess(controller.reset(scene));
    const CameraState reset = controller.state();
    const double radius = std::sqrt(14.0);
    const double verticalTangent = std::tan(kFov * 0.5);
    const double horizontalTangent = std::tan(std::atan(verticalTangent * 2.0));
    const double expectedDistance = radius * 1.05 / std::min(verticalTangent, horizontalTangent);
    assert(sameVec3(reset.position, {0.0, 0.0, expectedDistance}));
    assert(nearlyEqual(reset.nearPlane, 0.9 * (expectedDistance - radius)));
    assert(nearlyEqual(reset.farPlane, 1.1 * (expectedDistance + radius)));

    InputEvent resetRequest;
    resetRequest.type = InputEventType::ResetRequest;
    assertSuccess(controller.submitInput(pointerButton(2U, true, 0.5, 0.5)));
    assertSuccess(controller.submitInput(resetRequest));
    assertSuccess(controller.submitInput(pointerMove(0.6, 0.5)));
    const CameraState beforePendingUpdate = controller.state();
    assertSuccess(controller.update(0.0, scene));
    assert(sameVec3(controller.state().position, reset.position));
    assert(sameQuat(controller.state().orientation, glm::dquat{1.0, 0.0, 0.0, 0.0}));
    assert(!sameState(beforePendingUpdate, controller.state()));

    const Bounds3d unitScene = bounds({-1.0, -1.0, -1.0}, {1.0, 1.0, 1.0});
    assertSuccess(controller.update(0.1, unitScene));
    const CameraState updated = controller.state();
    const double s = std::sqrt(glm::dot(updated.position, updated.position));
    const double unitRadius = std::sqrt(3.0);
    assert(nearlyEqual(updated.nearPlane, std::max(0.001, 0.9 * std::max(s - unitRadius, 0.0))));
    assert(nearlyEqual(updated.farPlane, std::max(updated.nearPlane * 2.0, 1.1 * (s + unitRadius))));

    const CameraState stable = controller.state();
    assertDataFormat(controller.update(-0.1, unitScene));
    assert(sameState(controller.state(), stable));
    Bounds3d invalid;
    assertDataFormat(controller.update(0.0, invalid));
    assert(sameState(controller.state(), stable));
}

void testLargeResetSizeCacheAndGlobalFrustum() {
    OrbitCameraController controller;
    const RenderSize initialSize{640U, 480U, 1.0F};
    controller.matrices(initialSize);
    const Bounds3d largeScene = bounds({999999000.0, -1000.0, -1000.0},
                                       {1000001000.0, 1000.0, 1000.0});
    assertSuccess(controller.reset(largeScene));
    const CameraState resetState = controller.state();
    assert(resetState.position.z > 1000.0);
    assert(nearlyEqual(resetState.position.x, 1000000000.0));
    assertSuccess(controller.submitInput(wheel(-1.0)));
    assert(nearlyEqual(controller.state().position.z, 1000.0));

    const CameraMatrices beforeInvalidSize = controller.matrices(initialSize);
    const CameraMatrices invalidSize = controller.matrices(RenderSize{0U, 480U, 1.0F});
    assert(invalidSize.cameraOrigin == beforeInvalidSize.cameraOrigin);
    const ViewFrustum beforeInvalidFrustum = controller.frustum(initialSize);
    const ViewFrustum invalidFrustum = controller.frustum(RenderSize{640U, 0U, 1.0F});
    for (std::size_t index = 0; index < beforeInvalidFrustum.planes.size(); ++index) {
        assert(invalidFrustum.planes[index].equation == beforeInvalidFrustum.planes[index].equation);
    }
    const CameraState beforeResize = controller.state();
    controller.matrices(RenderSize{1920U, 1080U, 2.0F});
    assert(sameState(controller.state(), beforeResize));

    assertSuccess(controller.reset(largeScene));
    const ViewFrustum frustum = controller.frustum(RenderSize{1920U, 1080U, 1.0F});
    assert(frustum.intersects(bounds({999999999.0, -1.0, -1.0},
                                     {1000000001.0, 1.0, 1.0})).value());
}

} // namespace

int main() {
    testDefaultStateMatricesAndWorldFrustum();
    testRotationPanReleaseAndFocus();
    testWheelAndInvalidInputAreAtomic();
    testResetPendingUpdateAndDynamicPlanes();
    testLargeResetSizeCacheAndGlobalFrustum();
    return 0;
}
