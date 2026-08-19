#include "engine/EngineCoordinator.h"

#include <utility>

namespace dzc {
namespace {

Result<EngineCoordinatorControl> continueStage() {
    return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Continue);
}

} // namespace

EngineCoordinator::EngineCoordinator(EngineCoordinatorStages stages) noexcept
    : m_stages(std::move(stages)) {}

Result<void> EngineCoordinator::run(const FrameInput& input) const {
    const auto execute = [&input](const EngineCoordinatorStage& stage,
                                  bool& shouldStop) -> Result<void> {
        const Result<EngineCoordinatorControl> result = stage ? stage(input) : continueStage();
        if (!result.hasValue()) {
            return Result<void>::failure(result.error());
        }
        if (result.value() == EngineCoordinatorControl::Stop) {
            shouldStop = true;
        }
        return Result<void>::success();
    };

    bool shouldStop = false;
    for (const EngineCoordinatorStage* stage : {
             &m_stages.command,
             &m_stages.taskCompletion,
             &m_stages.camera,
             &m_stages.visibility,
             &m_stages.residency,
             &m_stages.frameDescription,
             &m_stages.diagnostics,
             &m_stages.snapshot}) {
        const Result<void> stageResult = execute(*stage, shouldStop);
        if (!stageResult.hasValue()) {
            return stageResult;
        }
        if (shouldStop) {
            return Result<void>::success();
        }
    }

    return Result<void>::success();
}

} // namespace dzc