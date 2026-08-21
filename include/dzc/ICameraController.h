#pragma once

#include <dzc/Bounds3d.h>
#include <dzc/CameraTypes.h>
#include <dzc/EngineTypes.h>
#include <dzc/InputEvent.h>
#include <dzc/Result.h>
#include <dzc/ViewFrustum.h>

namespace dzc {

class ICameraController {
public:
    virtual ~ICameraController() = default;

    virtual Result<void> submitInput(const InputEvent& event) = 0;
    virtual Result<void> update(double deltaSeconds, const Bounds3d& sceneBounds) = 0;
    virtual const CameraState& state() const noexcept = 0;
    virtual CameraMatrices matrices(const RenderSize& size) const = 0;
    virtual ViewFrustum frustum(const RenderSize& size) const = 0;
    virtual Result<void> reset(const Bounds3d& sceneBounds) = 0;
};

} // namespace dzc