#include <dzc/CameraTypes.h>

#include <cassert>
#include <type_traits>
#include <utility>

namespace {

void testDefaultValues() {
    const dzc::CameraState state;
    const dzc::CameraMatrices matrices;

    const glm::dvec3 zeroPosition{0.0};
    const glm::dquat identityOrientation{1.0, 0.0, 0.0, 0.0};
    assert(state.position == zeroPosition);
    assert(state.orientation == identityOrientation);
    assert(state.verticalFieldOfViewRadians == 0.0);
    assert(state.nearPlane == 0.0);
    assert(state.farPlane == 0.0);

    const glm::mat4 identityMatrix{1.0F};
    assert(matrices.view == identityMatrix);
    assert(matrices.projection == identityMatrix);
    assert(matrices.cameraOrigin == zeroPosition);
}

void testMemberTypes() {
    static_assert(std::is_same_v<decltype(dzc::CameraState::position), glm::dvec3>);
    static_assert(std::is_same_v<decltype(dzc::CameraState::orientation), glm::dquat>);
    static_assert(std::is_same_v<decltype(dzc::CameraState::verticalFieldOfViewRadians), double>);
    static_assert(std::is_same_v<decltype(dzc::CameraState::nearPlane), double>);
    static_assert(std::is_same_v<decltype(dzc::CameraState::farPlane), double>);
    static_assert(std::is_same_v<decltype(dzc::CameraMatrices::view), glm::mat4>);
    static_assert(std::is_same_v<decltype(dzc::CameraMatrices::projection), glm::mat4>);
    static_assert(std::is_same_v<decltype(dzc::CameraMatrices::cameraOrigin), glm::dvec3>);

    static_assert(std::is_default_constructible_v<dzc::CameraState>);
    static_assert(std::is_copy_constructible_v<dzc::CameraState>);
    static_assert(std::is_copy_assignable_v<dzc::CameraState>);
    static_assert(std::is_move_constructible_v<dzc::CameraState>);
    static_assert(std::is_move_assignable_v<dzc::CameraState>);
    static_assert(std::is_default_constructible_v<dzc::CameraMatrices>);
    static_assert(std::is_copy_constructible_v<dzc::CameraMatrices>);
    static_assert(std::is_copy_assignable_v<dzc::CameraMatrices>);
    static_assert(std::is_move_constructible_v<dzc::CameraMatrices>);
    static_assert(std::is_move_assignable_v<dzc::CameraMatrices>);
}

void testCopyMovePreservesDoublePrecisionData() {
    dzc::CameraState source;
    source.position = glm::dvec3{1000000000.125, -2000000000.25, 3000000000.5};
    const glm::dquat expectedOrientation{0.5, -0.5, 0.5, -0.5};
    source.orientation = expectedOrientation;
    source.verticalFieldOfViewRadians = 1.234567890123;
    source.nearPlane = 0.125;
    source.farPlane = 9876543210.5;

    const glm::dvec3 expectedPosition{1000000000.125, -2000000000.25, 3000000000.5};

    const dzc::CameraState copied(source);
    dzc::CameraState assigned;
    assigned = source;
    dzc::CameraState moved(std::move(source));
    dzc::CameraState moveAssigned;
    moveAssigned = std::move(copied);

    for (const dzc::CameraState* value : {&assigned, &moved, &moveAssigned}) {
        assert(value->position == expectedPosition);
        assert(value->orientation == expectedOrientation);
        assert(value->verticalFieldOfViewRadians == 1.234567890123);
        assert(value->nearPlane == 0.125);
        assert(value->farPlane == 9876543210.5);
    }

    dzc::CameraMatrices matrices;
    matrices.view[0][0] = 1.25F;
    matrices.view[3][2] = -4.5F;
    matrices.projection[1][1] = 2.5F;
    matrices.projection[2][3] = 0.75F;
    matrices.cameraOrigin = glm::dvec3{4000000000.25, -5000000000.5, 6000000000.75};

    const glm::mat4 expectedView = matrices.view;
    const glm::mat4 expectedProjection = matrices.projection;
    const glm::dvec3 expectedOrigin = matrices.cameraOrigin;

    const dzc::CameraMatrices matrixCopy(matrices);
    dzc::CameraMatrices matrixAssigned;
    matrixAssigned = matrices;
    dzc::CameraMatrices matrixMoved(std::move(matrices));
    dzc::CameraMatrices matrixMoveAssigned;
    matrixMoveAssigned = std::move(matrixCopy);

    for (const dzc::CameraMatrices* value : {&matrixAssigned, &matrixMoved, &matrixMoveAssigned}) {
        assert(value->view == expectedView);
        assert(value->projection == expectedProjection);
        assert(value->cameraOrigin == expectedOrigin);
    }
}

} // namespace

int main() {
    testDefaultValues();
    testMemberTypes();
    testCopyMovePreservesDoublePrecisionData();
    return 0;
}