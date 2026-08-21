#pragma once

#include <dzc/ICameraController.h>

#include <cstddef>
#include <optional>

namespace dzc::tests {

class FakeCameraController final : public ICameraController {
public:
    CameraState stateValue{};
    CameraMatrices matricesValue{};
    ViewFrustum frustumValue{};

    Result<void> submitInputResult{Result<void>::success()};
    Result<void> updateResult{Result<void>::success()};
    Result<void> resetResult{Result<void>::success()};

    std::size_t submitInputCallCount{0};
    std::size_t updateCallCount{0};
    mutable std::size_t stateCallCount{0};
    mutable std::size_t matricesCallCount{0};
    mutable std::size_t frustumCallCount{0};
    std::size_t resetCallCount{0};

    std::optional<InputEvent> lastInputEvent;
    std::optional<double> lastDeltaSeconds;
    std::optional<Bounds3d> lastUpdateSceneBounds;
    mutable std::optional<RenderSize> lastMatricesSize;
    mutable std::optional<RenderSize> lastFrustumSize;
    std::optional<Bounds3d> lastResetSceneBounds;

    Result<void> submitInput(const InputEvent& event) override {
        ++submitInputCallCount;
        lastInputEvent = event;
        return submitInputResult;
    }

    Result<void> update(double deltaSeconds, const Bounds3d& sceneBounds) override {
        ++updateCallCount;
        lastDeltaSeconds = deltaSeconds;
        lastUpdateSceneBounds = sceneBounds;
        return updateResult;
    }

    const CameraState& state() const noexcept override {
        ++stateCallCount;
        return stateValue;
    }

    CameraMatrices matrices(const RenderSize& size) const override {
        ++matricesCallCount;
        lastMatricesSize = size;
        return matricesValue;
    }

    ViewFrustum frustum(const RenderSize& size) const override {
        ++frustumCallCount;
        lastFrustumSize = size;
        return frustumValue;
    }

    Result<void> reset(const Bounds3d& sceneBounds) override {
        ++resetCallCount;
        lastResetSceneBounds = sceneBounds;
        return resetResult;
    }
};

} // namespace dzc::tests