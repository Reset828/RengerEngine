#include "engine/EngineCoordinator.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using dzc::EngineCoordinator;
using dzc::EngineCoordinatorControl;
using dzc::EngineCoordinatorStage;
using dzc::EngineCoordinatorStages;
using dzc::Error;
using dzc::ErrorDomain;
using dzc::FrameInput;
using dzc::Result;

constexpr const char* kStageNames[] = {
    "command",
    "taskCompletion",
    "camera",
    "visibility",
    "residency",
    "frameDescription",
    "diagnostics",
    "snapshot"};

Error makeFailure(std::uint32_t code) {
    return Error{
        ErrorDomain::Internal,
        code,
        "Injected stage failure",
        "The coordinator test injected a unique stage failure.",
        "EngineCoordinatorTests"};
}

EngineCoordinatorStages makeRecordingStages(std::vector<std::string>& calls,
                                            std::vector<const FrameInput*>& inputs) {
    const auto makeStage = [&calls, &inputs](const char* name) {
        return [&calls, &inputs, name](const FrameInput& input) {
            calls.emplace_back(name);
            inputs.push_back(&input);
            return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Continue);
        };
    };

    EngineCoordinatorStages stages;
    stages.command = makeStage(kStageNames[0]);
    stages.taskCompletion = makeStage(kStageNames[1]);
    stages.camera = makeStage(kStageNames[2]);
    stages.visibility = makeStage(kStageNames[3]);
    stages.residency = makeStage(kStageNames[4]);
    stages.frameDescription = makeStage(kStageNames[5]);
    stages.diagnostics = makeStage(kStageNames[6]);
    stages.snapshot = makeStage(kStageNames[7]);
    return stages;
}

void testSuccessfulStagesRunInFixedOrder() {
    std::vector<std::string> calls;
    std::vector<const FrameInput*> inputs;
    EngineCoordinator coordinator(makeRecordingStages(calls, inputs));
    const FrameInput input;

    const Result<void> result = coordinator.run(input);
    assert(result.hasValue());
    assert(calls.size() == 8U);
    assert(inputs.size() == calls.size());
    for (std::size_t index = 0; index < calls.size(); ++index) {
        assert(calls[index] == kStageNames[index]);
        assert(inputs[index] == &input);
    }
}

void testEachFailureShortCircuitsAndPreservesError() {
    for (std::size_t failingIndex = 0; failingIndex < 8U; ++failingIndex) {
        std::vector<std::string> calls;
        std::vector<const FrameInput*> inputs;
        EngineCoordinatorStages stages = makeRecordingStages(calls, inputs);
        const Error expected = makeFailure(static_cast<std::uint32_t>(100U + failingIndex));
        const auto failingStage = [&calls, &inputs, expected, failingIndex](const FrameInput& input) {
            calls.emplace_back(kStageNames[failingIndex]);
            inputs.push_back(&input);
            return Result<EngineCoordinatorControl>::failure(expected);
        };

        switch (failingIndex) {
        case 0U:
            stages.command = failingStage;
            break;
        case 1U:
            stages.taskCompletion = failingStage;
            break;
        case 2U:
            stages.camera = failingStage;
            break;
        case 3U:
            stages.visibility = failingStage;
            break;
        case 4U:
            stages.residency = failingStage;
            break;
        case 5U:
            stages.frameDescription = failingStage;
            break;
        case 6U:
            stages.diagnostics = failingStage;
            break;
        case 7U:
            stages.snapshot = failingStage;
            break;
        default:
            assert(false);
        }

        EngineCoordinator coordinator(std::move(stages));
        const FrameInput input;
        const Result<void> result = coordinator.run(input);
        assert(!result.hasValue());
        assert(result.error().domain == expected.domain);
        assert(result.error().code == expected.code);
        assert(result.error().userMessage == expected.userMessage);
        assert(result.error().diagnosticMessage == expected.diagnosticMessage);
        assert(result.error().context == expected.context);
        assert(calls.size() == failingIndex + 1U);
        assert(inputs.size() == calls.size());
        for (std::size_t index = 0; index < calls.size(); ++index) {
            assert(calls[index] == kStageNames[index]);
            assert(inputs[index] == &input);
        }
    }
}

void testNormalStopSkipsRemainingStages() {
    std::vector<std::string> calls;
    std::vector<const FrameInput*> inputs;
    EngineCoordinatorStages stages = makeRecordingStages(calls, inputs);
    stages.command = [&calls, &inputs](const FrameInput& input) {
        calls.emplace_back(kStageNames[0]);
        inputs.push_back(&input);
        return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Stop);
    };
    EngineCoordinator coordinator(std::move(stages));
    const FrameInput input;

    const Result<void> result = coordinator.run(input);
    assert(result.hasValue());
    assert(calls.size() == 1U);
    assert(calls.front() == kStageNames[0]);
    assert(inputs.size() == 1U);
    assert(inputs.front() == &input);
}

} // namespace

int main() {
    testSuccessfulStagesRunInFixedOrder();
    testEachFailureShortCircuitsAndPreservesError();
    testNormalStopSkipsRemainingStages();
    return 0;
}