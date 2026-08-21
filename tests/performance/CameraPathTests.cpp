#include "CameraPath.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using dzc::Bounds3d;
using dzc::ErrorDomain;
using dzc::InputEvent;
using dzc::InputEventType;
using dzc::RenderSize;
using dzc::performance::CameraPath;
using dzc::performance::CameraPathFrame;
using dzc::performance::CameraPathReplayer;
using dzc::performance::CameraPathStep;

Bounds3d sceneBounds() {
    Bounds3d bounds;
    bounds.minimum = {-1.0, -1.0, -1.0};
    bounds.maximum = {1.0, 1.0, 1.0};
    return bounds;
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

InputEvent focusLost() {
    InputEvent event;
    event.type = InputEventType::Focus;
    event.pressed = false;
    return event;
}

InputEvent resetRequest() {
    InputEvent event;
    event.type = InputEventType::ResetRequest;
    return event;
}

CameraPath makePath() {
    CameraPath path;
    path.renderSize = {1280U, 720U, 1.0F};
    path.sceneBounds = sceneBounds();
    path.steps = {
        {0.0, std::nullopt},
        {0.1, pointerButton(0U, true, 0.5, 0.5)},
        {0.2, pointerMove(0.61, 0.54)},
        {0.3, pointerButton(0U, false, 0.61, 0.54)},
        {0.4, pointerButton(2U, true, 0.5, 0.5)},
        {0.5, pointerMove(0.53, 0.47)},
        {0.6, focusLost()},
        {0.7, pointerMove(0.60, 0.40)},
        {0.8, wheel(1.0)},
        {0.9, resetRequest()},
        {1.0, std::nullopt},
        {1.1, std::nullopt}};
    return path;
}

void assertDataFormat(const dzc::Error& error) {
    assert(error.domain == ErrorDomain::DataFormat);
    assert(error.code == 2U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

void assertGeneralInvalidArgument(const dzc::Error& error) {
    assert(error.domain == ErrorDomain::General);
    assert(error.code == 1U);
    assert(!error.userMessage.empty());
    assert(!error.diagnosticMessage.empty());
    assert(!error.context.empty());
}

bool nearlyEqual(double lhs, double rhs, double absolute, double relative) {
    const double difference = std::abs(lhs - rhs);
    return difference <= absolute + relative * std::max(std::abs(lhs), std::abs(rhs));
}

bool nearlyEqual(float lhs, float rhs, float absolute, float relative) {
    const float difference = std::abs(lhs - rhs);
    return difference <= absolute + relative * std::max(std::abs(lhs), std::abs(rhs));
}

void assertSame(const CameraPathFrame& lhs, const CameraPathFrame& rhs) {
    assert(nearlyEqual(lhs.timeSeconds, rhs.timeSeconds, 1.0e-12, 1.0e-12));
    for (int axis = 0; axis < 3; ++axis) {
        assert(nearlyEqual(lhs.state.position[axis], rhs.state.position[axis], 1.0e-12, 1.0e-12));
        assert(nearlyEqual(lhs.matrices.cameraOrigin[axis], rhs.matrices.cameraOrigin[axis], 1.0e-12, 1.0e-12));
    }
    assert(nearlyEqual(lhs.state.orientation.w, rhs.state.orientation.w, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.orientation.x, rhs.state.orientation.x, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.orientation.y, rhs.state.orientation.y, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.orientation.z, rhs.state.orientation.z, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.verticalFieldOfViewRadians, rhs.state.verticalFieldOfViewRadians, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.nearPlane, rhs.state.nearPlane, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(lhs.state.farPlane, rhs.state.farPlane, 1.0e-12, 1.0e-12));
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            assert(nearlyEqual(lhs.matrices.view[column][row], rhs.matrices.view[column][row], 1.0e-5F, 1.0e-5F));
            assert(nearlyEqual(lhs.matrices.projection[column][row], rhs.matrices.projection[column][row], 1.0e-5F, 1.0e-5F));
        }
    }
}

void testBasicAndDeterministicReplay() {
    const CameraPath path = makePath();
    const auto first = CameraPathReplayer::replay(path);
    const auto second = CameraPathReplayer::replay(path);
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value().frames.size() == path.steps.size());
    assert(second.value().frames.size() == first.value().frames.size());
    assert(first.value().frames.front().timeSeconds == 0.0);
    assert(first.value().frames[9].timeSeconds == 0.9);
    assert((first.value().frames[8].state.orientation != glm::dquat{1.0, 0.0, 0.0, 0.0}));
    assert((first.value().frames[9].state.orientation == glm::dquat{1.0, 0.0, 0.0, 0.0}));
    assert(nearlyEqual(first.value().frames[10].state.position.x, 0.0, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(first.value().frames[10].state.position.y, 0.0, 1.0e-12, 1.0e-12));
    assert(first.value().frames[10].state.position.z > 3.0);
    assert(nearlyEqual(first.value().frames[10].state.orientation.w, 1.0, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(first.value().frames[10].state.orientation.x, 0.0, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(first.value().frames[10].state.orientation.y, 0.0, 1.0e-12, 1.0e-12));
    assert(nearlyEqual(first.value().frames[10].state.orientation.z, 0.0, 1.0e-12, 1.0e-12));
    for (std::size_t index = 0; index < first.value().frames.size(); ++index) {
        assertSame(first.value().frames[index], second.value().frames[index]);
        if (index != 0U) {
            assert(first.value().frames[index].timeSeconds >= first.value().frames[index - 1U].timeSeconds);
        }
    }
    assert(first.value().frames[2].state.orientation != first.value().frames[0].state.orientation);
    assert(first.value().frames[5].state.position != first.value().frames[2].state.position);
    assert(first.value().frames[7].state.position == first.value().frames[6].state.position);
    assert(first.value().frames[8].state.position != first.value().frames[7].state.position);
}

void testDataFormatValidation() {
    CameraPath path = makePath();
    path.sceneBounds = Bounds3d{};
    auto result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertDataFormat(result.error());

    path = makePath();
    path.renderSize.width = 0U;
    result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertDataFormat(result.error());

    for (float dpr : {0.0F, -1.0F, std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        path = makePath();
        path.renderSize.devicePixelRatio = dpr;
        result = CameraPathReplayer::replay(path);
        assert(!result.hasValue());
        assertDataFormat(result.error());
    }

    path = makePath();
    path.renderSize.height = 0U;
    result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertDataFormat(result.error());

    for (double time : {std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity(), -1.0}) {
        path = makePath();
        path.steps[1].timeSeconds = time;
        result = CameraPathReplayer::replay(path);
        assert(!result.hasValue());
        assertDataFormat(result.error());
    }

    path = makePath();
    path.steps[2].timeSeconds = 0.05;
    result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertDataFormat(result.error());
}

void testControllerErrorPropagation() {
    CameraPath path = makePath();
    path.steps[1].input = pointerMove(2.0, 0.5);
    auto result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertGeneralInvalidArgument(result.error());

    path = makePath();
    path.steps[1].input = wheel(std::numeric_limits<double>::quiet_NaN());
    result = CameraPathReplayer::replay(path);
    assert(!result.hasValue());
    assertGeneralInvalidArgument(result.error());
}

} // namespace

int main() {
    testBasicAndDeterministicReplay();
    testDataFormatValidation();
    testControllerErrorPropagation();
    return 0;
}