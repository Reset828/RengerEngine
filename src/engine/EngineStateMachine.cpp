#include "engine/EngineStateMachine.h"

#include <utility>

namespace dzc {
namespace {

Result<void> invalidTransition(EngineState state, EngineStateTrigger trigger) {
    (void)state;
    (void)trigger;
    return Result<void>::failure(Error{
        ErrorDomain::Internal,
        static_cast<std::uint32_t>(EngineErrorCode::InvalidState),
        "Invalid engine state transition",
        "The requested EngineState transition is not legal from the current state.",
        {}});
}

} // namespace

EngineState EngineStateMachine::state() const noexcept {
    return m_state;
}

Result<void> EngineStateMachine::transition(EngineStateTrigger trigger) {
    switch (m_state) {
    case EngineState::Created:
        if (trigger == EngineStateTrigger::Init) {
            m_state = EngineState::Initializing;
            return Result<void>::success();
        }
        break;

    case EngineState::Initializing:
        if (trigger == EngineStateTrigger::InitializationSucceeded) {
            m_state = EngineState::Ready;
            return Result<void>::success();
        }
        if (trigger == EngineStateTrigger::InitializationFailed) {
            m_state = EngineState::Failed;
            return Result<void>::success();
        }
        break;

    case EngineState::Ready:
        if (trigger == EngineStateTrigger::LoadDataset) {
            m_loadingReturnState = EngineState::Ready;
            m_state = EngineState::Loading;
            return Result<void>::success();
        }
        if (trigger == EngineStateTrigger::FirstValidFrame) {
            m_state = EngineState::Running;
            return Result<void>::success();
        }
        if (trigger == EngineStateTrigger::Shutdown) {
            m_state = EngineState::ShuttingDown;
            return Result<void>::success();
        }
        break;

    case EngineState::Running:
        if (trigger == EngineStateTrigger::LoadDataset) {
            m_loadingReturnState = EngineState::Running;
            m_state = EngineState::Loading;
            return Result<void>::success();
        }
        if (trigger == EngineStateTrigger::Shutdown) {
            m_state = EngineState::ShuttingDown;
            return Result<void>::success();
        }
        break;

    case EngineState::Loading:
        if (trigger == EngineStateTrigger::DatasetCompleted ||
            trigger == EngineStateTrigger::DatasetCancelled ||
            trigger == EngineStateTrigger::DatasetRecoverableFailure) {
            m_state = m_loadingReturnState;
            return Result<void>::success();
        }
        if (trigger == EngineStateTrigger::Shutdown) {
            m_state = EngineState::ShuttingDown;
            return Result<void>::success();
        }
        break;

    case EngineState::Failed:
        if (trigger == EngineStateTrigger::Shutdown) {
            m_state = EngineState::ShuttingDown;
            return Result<void>::success();
        }
        break;

    case EngineState::ShuttingDown:
        if (trigger == EngineStateTrigger::ResourcesReleased) {
            m_state = EngineState::Stopped;
            return Result<void>::success();
        }
        break;

    case EngineState::Stopped:
        break;
    }

    return invalidTransition(m_state, trigger);
}

} // namespace dzc