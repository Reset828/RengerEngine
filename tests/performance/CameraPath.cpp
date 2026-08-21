#include "CameraPath.h"

#include <dzc/OrbitCameraController.h>

#include <cmath>
#include <cstdint>
#include <utility>

namespace dzc::performance {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;

Error pathError(const char* userMessage, const char* diagnosticMessage, const char* context) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        userMessage,
        diagnosticMessage,
        context};
}

bool isValidRenderSize(const RenderSize& size) noexcept {
    return size.width != 0U && size.height != 0U &&
           std::isfinite(size.devicePixelRatio) && size.devicePixelRatio > 0.0F;
}

} // namespace

Result<CameraPathReplayResult> CameraPathReplayer::replay(const CameraPath& path) noexcept {
    if (!isValidRenderSize(path.renderSize)) {
        return Result<CameraPathReplayResult>::failure(pathError(
            "Camera path render size is invalid",
            "Camera path replay requires non-zero dimensions and a finite positive device pixel ratio.",
            "CameraPathReplayer::replay(renderSize)"));
    }
    if (!path.sceneBounds.isValid()) {
        return Result<CameraPathReplayResult>::failure(pathError(
            "Camera path scene bounds are invalid",
            "Camera path replay requires finite, axis-ordered scene bounds.",
            "CameraPathReplayer::replay(sceneBounds)"));
    }

    double previousTime = 0.0;
    for (std::size_t index = 0; index < path.steps.size(); ++index) {
        const CameraPathStep& step = path.steps[index];
        if (!std::isfinite(step.timeSeconds) || step.timeSeconds < 0.0) {
            return Result<CameraPathReplayResult>::failure(pathError(
                "Camera path timestamp is invalid",
                "Every camera path timestamp must be finite and non-negative.",
                "CameraPathReplayer::replay(step.timeSeconds)"));
        }
        if (index != 0U && step.timeSeconds < previousTime) {
            return Result<CameraPathReplayResult>::failure(pathError(
                "Camera path timestamps are out of order",
                "Camera path timestamps must be non-decreasing.",
                "CameraPathReplayer::replay(step ordering)"));
        }
        previousTime = step.timeSeconds;
    }

    OrbitCameraController controller;
    (void)controller.matrices(path.renderSize);
    const Result<void> initialUpdate = controller.update(0.0, path.sceneBounds);
    if (!initialUpdate.hasValue()) {
        return Result<CameraPathReplayResult>::failure(initialUpdate.error());
    }

    CameraPathReplayResult replayResult;
    replayResult.frames.reserve(path.steps.size());
    previousTime = 0.0;
    for (const CameraPathStep& step : path.steps) {
        if (step.input.has_value()) {
            const Result<void> inputResult = controller.submitInput(*step.input);
            if (!inputResult.hasValue()) {
                return Result<CameraPathReplayResult>::failure(inputResult.error());
            }
        }

        const double deltaSeconds = step.timeSeconds - previousTime;
        const Result<void> updateResult = controller.update(deltaSeconds, path.sceneBounds);
        if (!updateResult.hasValue()) {
            return Result<CameraPathReplayResult>::failure(updateResult.error());
        }

        CameraPathFrame frame;
        frame.timeSeconds = step.timeSeconds;
        frame.state = controller.state();
        frame.matrices = controller.matrices(path.renderSize);
        replayResult.frames.push_back(std::move(frame));
        previousTime = step.timeSeconds;
    }

    return Result<CameraPathReplayResult>::success(std::move(replayResult));
}

} // namespace dzc::performance