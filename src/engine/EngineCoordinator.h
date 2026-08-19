#pragma once

#include "dzc/FrameInput.h"
#include "dzc/Result.h"

#include <cstdint>
#include <functional>

namespace dzc {

enum class EngineCoordinatorControl : std::uint8_t {
    Continue,
    Stop
};

using EngineCoordinatorStage =
    std::function<Result<EngineCoordinatorControl>(const FrameInput&)>;

struct EngineCoordinatorStages final {
    EngineCoordinatorStage command;
    EngineCoordinatorStage taskCompletion;
    EngineCoordinatorStage camera;
    EngineCoordinatorStage visibility;
    EngineCoordinatorStage residency;
    EngineCoordinatorStage frameDescription;
    EngineCoordinatorStage diagnostics;
    EngineCoordinatorStage snapshot;
};

class EngineCoordinator final {
public:
    EngineCoordinator() = default;
    explicit EngineCoordinator(EngineCoordinatorStages stages) noexcept;

    Result<void> run(const FrameInput& input) const;

private:
    EngineCoordinatorStages m_stages;
};

} // namespace dzc