#include "engine/DatasetSession.h"
#include "engine/EngineTestAccess.h"

#include <dzc/Engine.h>

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using dzc::CancelDatasetLoadCommand;
using dzc::DatasetId;
using dzc::DatasetLoadCancelledEvent;
using dzc::DatasetLoadedEvent;
using dzc::DatasetSession;
using dzc::DatasetSessionCompletionKind;
using dzc::DatasetState;
using dzc::Engine;
using dzc::EngineConfig;
using dzc::EngineEvent;
using dzc::EngineState;
using dzc::EngineTestAccess;
using dzc::Error;
using dzc::ErrorDomain;
using dzc::ErrorEvent;
using dzc::FrameInput;
using dzc::LoadDatasetCommand;
using dzc::Result;
using dzc::TaskId;
using dzc::tasks::TaskCompletion;

constexpr std::uint32_t kReadFailure = 2U;

void assertSuccess(const Result<void>& result) {
    assert(result.hasValue());
}

TaskCompletion successfulCompletion(DatasetId datasetId, std::uint64_t taskId) {
    return TaskCompletion{TaskId{taskId}, datasetId, Result<void>::success()};
}

TaskCompletion failedCompletion(DatasetId datasetId, std::uint64_t taskId) {
    return TaskCompletion{
        TaskId{taskId},
        datasetId,
        Result<void>::failure(Error{
            ErrorDomain::FileIo,
            kReadFailure,
            "Dataset read failed",
            "The deterministic test reader reported a read failure.",
            "DatasetReplacementTests"})};
}

bool containsLoadedEvent(const std::vector<EngineEvent>& events, DatasetId datasetId) {
    for (const EngineEvent& event : events) {
        if (const auto* loaded = std::get_if<DatasetLoadedEvent>(&event)) {
            if (loaded->datasetId == datasetId) {
                return true;
            }
        }
    }
    return false;
}

bool containsCancelledEvent(const std::vector<EngineEvent>& events, DatasetId datasetId) {
    for (const EngineEvent& event : events) {
        if (const auto* cancelled = std::get_if<DatasetLoadCancelledEvent>(&event)) {
            if (cancelled->datasetId == datasetId) {
                return true;
            }
        }
    }
    return false;
}

bool containsErrorEvent(const std::vector<EngineEvent>& events, DatasetId datasetId) {
    for (const EngineEvent& event : events) {
        if (const auto* error = std::get_if<ErrorEvent>(&event)) {
            if (error->context.datasetId == datasetId) {
                return true;
            }
        }
    }
    return false;
}

void testSessionRejectsOldCompletionAfterReplacement() {
    DatasetSession session;
    const auto first = session.beginLoad("first.las");
    const auto second = session.beginLoad("second.las");
    assert(first.hasValue());
    assert(second.hasValue());
    assert(first.value() == DatasetId{1U});
    assert(second.value() == DatasetId{2U});

    const auto oldResult = session.applyCompletion(successfulCompletion(first.value(), 11U));
    assert(oldResult.kind == DatasetSessionCompletionKind::Ignored);
    assert(session.snapshotSummary().id == second.value());
    assert(session.snapshotSummary().state == DatasetState::Opening);

    const auto currentResult = session.applyCompletion(successfulCompletion(second.value(), 12U));
    assert(currentResult.kind == DatasetSessionCompletionKind::Loaded);
    assert(session.sceneDatasetId() == second.value());
    assert(session.snapshotSummary().id == second.value());
    assert(session.snapshotSummary().state == DatasetState::Ready);
}

void testReplacementFailureAndCancellationPreserveActiveDataset() {
    Engine engine;
    assertSuccess(engine.init(EngineConfig{}));

    const DatasetId first{1U};
    const DatasetId second{2U};
    const DatasetId third{3U};

    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"first.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.id == first);
    assert(EngineTestAccess::injectDatasetCompletion(engine, successfulCompletion(first, 101U)));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Running);
    assert(engine.getSnapshot()->dataset.id == first);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Ready);
    assert(containsLoadedEvent(engine.pollEvents(), first));

    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"second.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.id == first);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Ready);

    assert(EngineTestAccess::injectDatasetCompletion(engine, failedCompletion(second, 102U)));
    assertSuccess(engine.update(FrameInput{}));
    const auto failedReplacement = engine.getSnapshot();
    assert(failedReplacement->state == EngineState::Running);
    assert(failedReplacement->dataset.id == first);
    assert(failedReplacement->dataset.state == DatasetState::Ready);
    assert(failedReplacement->mostRecentError.has_value());
    assert(failedReplacement->mostRecentError->domain == ErrorDomain::FileIo);
    assert(containsErrorEvent(engine.pollEvents(), second));

    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"third.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.id == first);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Ready);
    assertSuccess(engine.enqueueCommand(CancelDatasetLoadCommand{third}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.id == first);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Ready);

    assert(EngineTestAccess::injectDatasetCompletion(engine, successfulCompletion(third, 103U)));
    assertSuccess(engine.update(FrameInput{}));
    const auto cancelledReplacement = engine.getSnapshot();
    assert(cancelledReplacement->state == EngineState::Running);
    assert(cancelledReplacement->dataset.id == first);
    assert(cancelledReplacement->dataset.state == DatasetState::Ready);
    assert(containsCancelledEvent(engine.pollEvents(), third));
}

void testEngineFiltersOutOfOrderResults() {
    Engine engine;
    assertSuccess(engine.init(EngineConfig{}));

    const DatasetId first{1U};
    const DatasetId second{2U};

    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"first.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"second.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.id == second);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Opening);

    assert(EngineTestAccess::injectDatasetCompletion(engine, successfulCompletion(first, 201U)));
    assertSuccess(engine.update(FrameInput{}));
    const auto afterOldResult = engine.getSnapshot();
    assert(afterOldResult->state == EngineState::Loading);
    assert(afterOldResult->dataset.id == second);
    assert(afterOldResult->dataset.state == DatasetState::Opening);
    assert(!containsLoadedEvent(engine.pollEvents(), first));

    assert(EngineTestAccess::injectDatasetCompletion(engine, successfulCompletion(second, 202U)));
    assertSuccess(engine.update(FrameInput{}));
    const auto afterCurrentResult = engine.getSnapshot();
    assert(afterCurrentResult->state == EngineState::Running);
    assert(afterCurrentResult->dataset.id == second);
    assert(afterCurrentResult->dataset.state == DatasetState::Ready);
    assert(containsLoadedEvent(engine.pollEvents(), second));
}

void testInitialLoadFailureReportsErrorWithoutActiveDataset() {
    Engine engine;
    assertSuccess(engine.init(EngineConfig{}));

    const DatasetId first{1U};
    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"broken.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(EngineTestAccess::injectDatasetCompletion(engine, failedCompletion(first, 301U)));
    assertSuccess(engine.update(FrameInput{}));

    const auto snapshot = engine.getSnapshot();
    assert(snapshot->state == EngineState::Running);
    assert(snapshot->dataset.id == first);
    assert(snapshot->dataset.state == DatasetState::Error);
    assert(snapshot->mostRecentError.has_value());
    assert(snapshot->mostRecentError->domain == ErrorDomain::FileIo);
    assert(containsErrorEvent(engine.pollEvents(), first));
}

} // namespace

int main() {
    testSessionRejectsOldCompletionAfterReplacement();
    testReplacementFailureAndCancellationPreserveActiveDataset();
    testEngineFiltersOutOfOrderResults();
    testInitialLoadFailureReportsErrorWithoutActiveDataset();
    return 0;
}
