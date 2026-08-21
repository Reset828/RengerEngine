#pragma once

#include <dzc/ICameraController.h>

#include <memory>

namespace dzc {

class OrbitCameraController final : public ICameraController {
public:
    OrbitCameraController();
    ~OrbitCameraController() override;

    OrbitCameraController(const OrbitCameraController&) = delete;
    OrbitCameraController& operator=(const OrbitCameraController&) = delete;
    OrbitCameraController(OrbitCameraController&&) noexcept;
    OrbitCameraController& operator=(OrbitCameraController&&) noexcept;

    Result<void> submitInput(const InputEvent& event) override;
    Result<void> update(double deltaSeconds, const Bounds3d& sceneBounds) override;
    const CameraState& state() const noexcept override;
    CameraMatrices matrices(const RenderSize& size) const override;
    ViewFrustum frustum(const RenderSize& size) const override;
    Result<void> reset(const Bounds3d& sceneBounds) override;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace dzc
