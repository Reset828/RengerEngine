#pragma once

#include <dzc/EngineState.h>
#include <dzc/Result.h>

#include <cstdint>

namespace dzc {

enum class EngineStateTrigger : std::uint8_t {
    Init,
    InitializationSucceeded,
    InitializationFailed,
    LoadDataset,
    DatasetCompleted,
    DatasetCancelled,
    DatasetRecoverableFailure,
    FirstValidFrame,
    Shutdown,
    ResourcesReleased
};

enum class EngineErrorCode : std::uint32_t {
    InvalidState = 1
};

class EngineStateMachine final {
public:
    EngineStateMachine() noexcept = default;

    EngineState state() const noexcept;

    Result<void> transition(EngineStateTrigger trigger);

private:
    EngineState m_state{EngineState::Created};
    EngineState m_loadingReturnState{EngineState::Ready};
};

} // namespace dzc