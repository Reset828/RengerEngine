#include "engine/EngineStateMachine.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using dzc::EngineErrorCode;
using dzc::EngineState;
using dzc::EngineStateMachine;
using dzc::EngineStateTrigger;

dzc::Result<void> transition(EngineStateMachine& machine, EngineStateTrigger trigger) {
    return machine.transition(trigger);
}

void assertSuccess(const dzc::Result<void>& result) {
    assert(result.hasValue());
}

void assertInvalidState(const dzc::Result<void>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Internal);
    assert(result.error().code == static_cast<std::uint32_t>(EngineErrorCode::InvalidState));
    assert(result.error().userMessage == "Invalid engine state transition");
}

void moveToReady(EngineStateMachine& machine) {
    assertSuccess(transition(machine, EngineStateTrigger::Init));
    assertSuccess(transition(machine, EngineStateTrigger::InitializationSucceeded));
}

void moveToRunning(EngineStateMachine& machine) {
    moveToReady(machine);
    assertSuccess(transition(machine, EngineStateTrigger::FirstValidFrame));
}

void testStableEnumContracts() {
    static_assert(std::is_same_v<std::underlying_type_t<EngineStateTrigger>, std::uint8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<EngineErrorCode>, std::uint32_t>);
    static_assert(static_cast<std::uint32_t>(EngineErrorCode::InvalidState) == 1U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::Init) == 0U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::InitializationSucceeded) == 1U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::InitializationFailed) == 2U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::LoadDataset) == 3U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::DatasetCompleted) == 4U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::DatasetCancelled) == 5U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::DatasetRecoverableFailure) == 6U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::FirstValidFrame) == 7U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::Shutdown) == 8U);
    static_assert(static_cast<std::uint8_t>(EngineStateTrigger::ResourcesReleased) == 9U);
}

void testInitialAndInitializationTransitions() {
    EngineStateMachine machine;
    assert(machine.state() == EngineState::Created);

    assertSuccess(transition(machine, EngineStateTrigger::Init));
    assert(machine.state() == EngineState::Initializing);
    assertSuccess(transition(machine, EngineStateTrigger::InitializationSucceeded));
    assert(machine.state() == EngineState::Ready);
    assertSuccess(transition(machine, EngineStateTrigger::FirstValidFrame));
    assert(machine.state() == EngineState::Running);
}

void testFatalInitializationAndShutdownTransitions() {
    EngineStateMachine machine;
    assertSuccess(transition(machine, EngineStateTrigger::Init));
    assertSuccess(transition(machine, EngineStateTrigger::InitializationFailed));
    assert(machine.state() == EngineState::Failed);
    assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
    assert(machine.state() == EngineState::ShuttingDown);
    assertSuccess(transition(machine, EngineStateTrigger::ResourcesReleased));
    assert(machine.state() == EngineState::Stopped);
}

void testLoadingReturnsToReadyForAllResults() {
    const std::vector<EngineStateTrigger> results{
        EngineStateTrigger::DatasetCompleted,
        EngineStateTrigger::DatasetCancelled,
        EngineStateTrigger::DatasetRecoverableFailure};

    for (const EngineStateTrigger result : results) {
        EngineStateMachine machine;
        moveToReady(machine);
        assertSuccess(transition(machine, EngineStateTrigger::LoadDataset));
        assert(machine.state() == EngineState::Loading);
        assertSuccess(transition(machine, result));
        assert(machine.state() == EngineState::Ready);
    }
}

void testLoadingReturnsToRunningForAllResults() {
    const std::vector<EngineStateTrigger> results{
        EngineStateTrigger::DatasetCompleted,
        EngineStateTrigger::DatasetCancelled,
        EngineStateTrigger::DatasetRecoverableFailure};

    for (const EngineStateTrigger result : results) {
        EngineStateMachine machine;
        moveToRunning(machine);
        assertSuccess(transition(machine, EngineStateTrigger::LoadDataset));
        assert(machine.state() == EngineState::Loading);
        assertSuccess(transition(machine, result));
        assert(machine.state() == EngineState::Running);
    }
}

void testShutdownFromAllPermittedStates() {
    {
        EngineStateMachine machine;
        moveToReady(machine);
        assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
        assert(machine.state() == EngineState::ShuttingDown);
    }
    {
        EngineStateMachine machine;
        moveToRunning(machine);
        assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
        assert(machine.state() == EngineState::ShuttingDown);
    }
    {
        EngineStateMachine machine;
        moveToReady(machine);
        assertSuccess(transition(machine, EngineStateTrigger::LoadDataset));
        assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
        assert(machine.state() == EngineState::ShuttingDown);
    }
    {
        EngineStateMachine machine;
        assertSuccess(transition(machine, EngineStateTrigger::Init));
        assertSuccess(transition(machine, EngineStateTrigger::InitializationFailed));
        assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
        assert(machine.state() == EngineState::ShuttingDown);
    }
}

void testInvalidTransitionsPreserveState() {
    const std::vector<EngineStateTrigger> triggers{
        EngineStateTrigger::Init,
        EngineStateTrigger::InitializationSucceeded,
        EngineStateTrigger::InitializationFailed,
        EngineStateTrigger::LoadDataset,
        EngineStateTrigger::DatasetCompleted,
        EngineStateTrigger::DatasetCancelled,
        EngineStateTrigger::DatasetRecoverableFailure,
        EngineStateTrigger::FirstValidFrame,
        EngineStateTrigger::Shutdown,
        EngineStateTrigger::ResourcesReleased};

    const std::vector<std::pair<EngineState, std::vector<EngineStateTrigger>>> invalidByState{
        {EngineState::Created, {EngineStateTrigger::InitializationSucceeded, EngineStateTrigger::LoadDataset,
                                EngineStateTrigger::Shutdown, EngineStateTrigger::ResourcesReleased}},
        {EngineState::Initializing, {EngineStateTrigger::Init, EngineStateTrigger::LoadDataset,
                                     EngineStateTrigger::FirstValidFrame, EngineStateTrigger::Shutdown}},
        {EngineState::Ready, {EngineStateTrigger::InitializationSucceeded, EngineStateTrigger::DatasetCompleted,
                              EngineStateTrigger::DatasetCancelled, EngineStateTrigger::DatasetRecoverableFailure}},
        {EngineState::Running, {EngineStateTrigger::InitializationSucceeded, EngineStateTrigger::FirstValidFrame,
                                EngineStateTrigger::DatasetCompleted, EngineStateTrigger::DatasetCancelled}},
        {EngineState::Loading, {EngineStateTrigger::Init, EngineStateTrigger::InitializationSucceeded,
                                EngineStateTrigger::InitializationFailed, EngineStateTrigger::LoadDataset,
                                EngineStateTrigger::FirstValidFrame}},
        {EngineState::Failed, {EngineStateTrigger::Init, EngineStateTrigger::InitializationSucceeded,
                               EngineStateTrigger::InitializationFailed, EngineStateTrigger::LoadDataset,
                               EngineStateTrigger::DatasetCompleted, EngineStateTrigger::FirstValidFrame}},
        {EngineState::ShuttingDown, {EngineStateTrigger::Shutdown, EngineStateTrigger::Init}},
        {EngineState::Stopped, triggers}};

    for (const auto& entry : invalidByState) {
        EngineStateMachine machine;
        switch (entry.first) {
        case EngineState::Created:
            break;
        case EngineState::Initializing:
            assertSuccess(transition(machine, EngineStateTrigger::Init));
            break;
        case EngineState::Ready:
            moveToReady(machine);
            break;
        case EngineState::Running:
            moveToRunning(machine);
            break;
        case EngineState::Loading:
            moveToReady(machine);
            assertSuccess(transition(machine, EngineStateTrigger::LoadDataset));
            break;
        case EngineState::Failed:
            assertSuccess(transition(machine, EngineStateTrigger::Init));
            assertSuccess(transition(machine, EngineStateTrigger::InitializationFailed));
            break;
        case EngineState::ShuttingDown:
            moveToReady(machine);
            assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
            break;
        case EngineState::Stopped:
            moveToReady(machine);
            assertSuccess(transition(machine, EngineStateTrigger::Shutdown));
            assertSuccess(transition(machine, EngineStateTrigger::ResourcesReleased));
            break;
        }

        for (const EngineStateTrigger trigger : entry.second) {
            const EngineState before = machine.state();
            const dzc::Result<void> result = transition(machine, trigger);
            assertInvalidState(result);
            assert(machine.state() == before);
        }
    }
}

} // namespace

int main() {
    testStableEnumContracts();
    testInitialAndInitializationTransitions();
    testFatalInitializationAndShutdownTransitions();
    testLoadingReturnsToReadyForAllResults();
    testLoadingReturnsToRunningForAllResults();
    testShutdownFromAllPermittedStates();
    testInvalidTransitionsPreserveState();
    return 0;
}