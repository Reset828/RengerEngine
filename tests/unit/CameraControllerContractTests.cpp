#include <dzc/ICameraController.h>

#include <fakes/FakeCameraController.h>

#include <cassert>
#include <cmath>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

using dzc::Bounds3d;
using dzc::CameraMatrices;
using dzc::CameraState;
using dzc::Error;
using dzc::ErrorDomain;
using dzc::FrustumPlane;
using dzc::ICameraController;
using dzc::InputEvent;
using dzc::InputEventType;
using dzc::RenderSize;
using dzc::Result;
using dzc::ViewFrustum;
using dzc::tests::FakeCameraController;

static_assert(std::is_abstract_v<ICameraController>);
static_assert(std::has_virtual_destructor_v<ICameraController>);
static_assert(std::is_base_of_v<ICameraController, FakeCameraController>);
static_assert(std::is_final_v<FakeCameraController>);
static_assert(std::is_same_v<
    decltype(&ICameraController::submitInput),
    Result<void> (ICameraController::*)(const InputEvent&)>);
static_assert(std::is_same_v<
    decltype(&ICameraController::update),
    Result<void> (ICameraController::*)(double, const Bounds3d&)>);
static_assert(std::is_same_v<
    decltype(&ICameraController::state),
    const CameraState& (ICameraController::*)() const noexcept>);
static_assert(std::is_same_v<
    decltype(&ICameraController::matrices),
    CameraMatrices (ICameraController::*)(const RenderSize&) const>);
static_assert(std::is_same_v<
    decltype(&ICameraController::frustum),
    ViewFrustum (ICameraController::*)(const RenderSize&) const>);
static_assert(std::is_same_v<
    decltype(&ICameraController::reset),
    Result<void> (ICameraController::*)(const Bounds3d&)>);

Bounds3d makeBounds(double offset) {
    Bounds3d bounds;
    bounds.minimum = {offset, offset + 1.0, offset + 2.0};
    bounds.maximum = {offset + 3.0, offset + 4.0, offset + 5.0};
    return bounds;
}

void assertBoundsEqual(const Bounds3d& actual, const Bounds3d& expected) {
    assert(actual.minimum.x == expected.minimum.x);
    assert(actual.minimum.y == expected.minimum.y);
    assert(actual.minimum.z == expected.minimum.z);
    assert(actual.maximum.x == expected.maximum.x);
    assert(actual.maximum.y == expected.maximum.y);
    assert(actual.maximum.z == expected.maximum.z);
}

void assertErrorEqual(const Error& actual, const Error& expected) {
    assert(actual.domain == expected.domain);
    assert(actual.code == expected.code);
    assert(actual.userMessage == expected.userMessage);
    assert(actual.diagnosticMessage == expected.diagnosticMessage);
    assert(actual.context == expected.context);
}

void assertMatricesEqual(const CameraMatrices& actual, const CameraMatrices& expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            assert(actual.view[column][row] == expected.view[column][row]);
            assert(actual.projection[column][row] == expected.projection[column][row]);
        }
    }

    assert(actual.cameraOrigin.x == expected.cameraOrigin.x);
    assert(actual.cameraOrigin.y == expected.cameraOrigin.y);
    assert(actual.cameraOrigin.z == expected.cameraOrigin.z);
}

void assertFrustumEqual(const ViewFrustum& actual, const ViewFrustum& expected) {
    for (std::size_t index = 0; index < actual.planes.size(); ++index) {
        assert(actual.planes[index].equation.x == expected.planes[index].equation.x);
        assert(actual.planes[index].equation.y == expected.planes[index].equation.y);
        assert(actual.planes[index].equation.z == expected.planes[index].equation.z);
        assert(actual.planes[index].equation.w == expected.planes[index].equation.w);
    }
}

void verifyPolymorphicCallsAndRecording() {
    FakeCameraController fake;
    fake.stateValue.position = {1.0, -2.0, 3.5};
    fake.stateValue.orientation = {0.5, 0.25, -0.75, 1.0};
    fake.stateValue.verticalFieldOfViewRadians = 1.2;
    fake.stateValue.nearPlane = 0.1;
    fake.stateValue.farPlane = 1000.0;

    fake.matricesValue.view[0][1] = 7.0F;
    fake.matricesValue.projection[2][3] = -4.0F;
    fake.matricesValue.cameraOrigin = {100000000.0, -3.0, 0.125};

    fake.frustumValue.planes[ViewFrustum::Left].equation = {1.0, 2.0, 3.0, 4.0};
    fake.frustumValue.planes[ViewFrustum::Far].equation = {-5.0, 6.0, -7.0, 8.0};

    ICameraController& controller = fake;
    const ICameraController& constController = fake;

    const InputEvent event{
        InputEventType::PointerMove,
        42U,
        -12.5,
        33.25,
        true,
        0xA5A5U};
    const Bounds3d updateBounds = makeBounds(10.0);
    const Bounds3d resetBounds = makeBounds(-25.0);
    const RenderSize matrixSize{1920U, 1080U, 1.5F};
    const RenderSize frustumSize{800U, 600U, 2.0F};

    assert(controller.submitInput(event).hasValue());
    assert(controller.update(0.016, updateBounds).hasValue());
    const CameraState& state = constController.state();
    const CameraMatrices matrices = constController.matrices(matrixSize);
    const ViewFrustum frustum = constController.frustum(frustumSize);
    assert(controller.reset(resetBounds).hasValue());

    assert(fake.submitInputCallCount == 1U);
    assert(fake.updateCallCount == 1U);
    assert(fake.stateCallCount == 1U);
    assert(fake.matricesCallCount == 1U);
    assert(fake.frustumCallCount == 1U);
    assert(fake.resetCallCount == 1U);

    assert(fake.lastInputEvent.has_value());
    assert(fake.lastInputEvent->type == event.type);
    assert(fake.lastInputEvent->code == event.code);
    assert(fake.lastInputEvent->valueX == event.valueX);
    assert(fake.lastInputEvent->valueY == event.valueY);
    assert(fake.lastInputEvent->pressed == event.pressed);
    assert(fake.lastInputEvent->modifiers == event.modifiers);

    assert(fake.lastDeltaSeconds.has_value());
    assert(*fake.lastDeltaSeconds == 0.016);
    assert(fake.lastUpdateSceneBounds.has_value());
    assertBoundsEqual(*fake.lastUpdateSceneBounds, updateBounds);
    assert(fake.lastResetSceneBounds.has_value());
    assertBoundsEqual(*fake.lastResetSceneBounds, resetBounds);
    assert(fake.lastMatricesSize.has_value());
    assert(*fake.lastMatricesSize == matrixSize);
    assert(fake.lastFrustumSize.has_value());
    assert(*fake.lastFrustumSize == frustumSize);

    assert(&state == &fake.stateValue);
    assert(state.position.x == 1.0);
    assert(state.position.y == -2.0);
    assert(state.position.z == 3.5);
    assert(state.orientation.w == 0.5);
    assert(state.orientation.x == 0.25);
    assert(state.orientation.y == -0.75);
    assert(state.orientation.z == 1.0);
    assert(state.verticalFieldOfViewRadians == 1.2);
    assert(state.nearPlane == 0.1);
    assert(state.farPlane == 1000.0);
    assertMatricesEqual(matrices, fake.matricesValue);
    assertFrustumEqual(frustum, fake.frustumValue);
}

void verifyPresetFailureResults() {
    FakeCameraController fake;
    const Error submitError{
        ErrorDomain::Internal,
        101U,
        "Submit failed",
        "preset submit failure",
        "CameraControllerContractTests"};
    const Error updateError{
        ErrorDomain::DataFormat,
        102U,
        "Update failed",
        "preset update failure",
        "CameraControllerContractTests"};
    const Error resetError{
        ErrorDomain::Configuration,
        103U,
        "Reset failed",
        "preset reset failure",
        "CameraControllerContractTests"};

    fake.submitInputResult = Result<void>::failure(submitError);
    fake.updateResult = Result<void>::failure(updateError);
    fake.resetResult = Result<void>::failure(resetError);

    ICameraController& controller = fake;
    const InputEvent event{InputEventType::ResetRequest, 7U, 0.0, -0.0, false, 0U};
    const Bounds3d bounds = makeBounds(2.0);

    const Result<void> submitResult = controller.submitInput(event);
    const Result<void> updateResult = controller.update(0.5, bounds);
    const Result<void> resetResult = controller.reset(bounds);

    assert(!submitResult.hasValue());
    assertErrorEqual(submitResult.error(), submitError);
    assert(!updateResult.hasValue());
    assertErrorEqual(updateResult.error(), updateError);
    assert(!resetResult.hasValue());
    assertErrorEqual(resetResult.error(), resetError);

    assert(fake.submitInputCallCount == 1U);
    assert(fake.updateCallCount == 1U);
    assert(fake.resetCallCount == 1U);
    assert(fake.lastInputEvent.has_value());
    assert(fake.lastUpdateSceneBounds.has_value());
    assert(fake.lastResetSceneBounds.has_value());
}

void verifyVirtualDestruction() {
    std::unique_ptr<ICameraController> controller = std::make_unique<FakeCameraController>();
    assert(controller != nullptr);
}

} // namespace

int main() {
    verifyPolymorphicCallsAndRecording();
    verifyPresetFailureResults();
    verifyVirtualDestruction();
    return 0;
}