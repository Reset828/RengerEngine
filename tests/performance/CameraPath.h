#pragma once

#include <dzc/Bounds3d.h>
#include <dzc/CameraTypes.h>
#include <dzc/EngineTypes.h>
#include <dzc/InputEvent.h>
#include <dzc/Result.h>

#include <optional>
#include <vector>

namespace dzc::performance {

struct CameraPathStep final {
    double timeSeconds{0.0};
    std::optional<InputEvent> input;
};

struct CameraPath final {
    RenderSize renderSize;
    Bounds3d sceneBounds;
    std::vector<CameraPathStep> steps;
};

struct CameraPathFrame final {
    double timeSeconds{0.0};
    CameraState state;
    CameraMatrices matrices;
};

struct CameraPathReplayResult final {
    std::vector<CameraPathFrame> frames;
};

class CameraPathReplayer final {
public:
    static Result<CameraPathReplayResult> replay(const CameraPath& path) noexcept;
};

} // namespace dzc::performance