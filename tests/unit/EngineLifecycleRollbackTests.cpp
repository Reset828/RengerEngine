#include "engine/EngineTestAccess.h"

#include <dzc/Engine.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using dzc::DatasetLoadedEvent;
using dzc::DatasetState;
using dzc::Engine;
using dzc::EngineConfig;
using dzc::EngineInitializationStage;
using dzc::EngineLifecycleRecord;
using dzc::EngineLifecycleResourceState;
using dzc::EngineLifecycleTrace;
using dzc::EngineState;
using dzc::EngineTestAccess;
using dzc::FrameInput;
using dzc::LoadDatasetCommand;
using dzc::Result;

void assertSuccess(const Result<void>& result) {
    assert(result.hasValue());
}

void assertNoResources(const EngineLifecycleResourceState& state) {
    assert(!state.commandQueue);
    assert(!state.eventQueue);
    assert(!state.renderBackend);
    assert(!state.computeBackend);
}

void assertRecords(
    const std::shared_ptr<const EngineLifecycleTrace>& trace,
    std::initializer_list<EngineLifecycleRecord> expected) {
    assert(trace != nullptr);
    assert(trace->records == std::vector<EngineLifecycleRecord>(expected));
}

EngineConfig smallConfig() {
    EngineConfig config;
    config.commandQueueCapacity = 2U;
    config.eventQueueCapacity = 2U;
    return config;
}

void testSuccessfulInitializationCreatesResourcesInOrder() {
    Engine engine;
    assertSuccess(engine.init(smallConfig()));

    const auto resources = EngineTestAccess::resourceState(engine);
    assert(resources.commandQueue);
    assert(resources.eventQueue);
    assert(resources.renderBackend);
    assert(resources.computeBackend);
    assert(engine.getSnapshot()->state == EngineState::Ready);

    assertRecords(
        EngineTestAccess::lifecycleTrace(engine),
        {EngineLifecycleRecord::CommandQueueCreated,
         EngineLifecycleRecord::EventQueueCreated,
         EngineLifecycleRecord::RenderBackendCreated,
         EngineLifecycleRecord::ComputeBackendCreated,
         EngineLifecycleRecord::ReadySnapshotPublished});
}

void testInitializationFailureRollsBackEveryStage() {
    struct FailureCase final {
        EngineInitializationStage stage;
        std::vector<EngineLifecycleRecord> expected;
    };
    const std::array<FailureCase, 4U> cases{{
        {EngineInitializationStage::CommandQueue,
         {EngineLifecycleRecord::InitializationFailed,
          EngineLifecycleRecord::FailedSnapshotPublished}},
        {EngineInitializationStage::EventQueue,
         {EngineLifecycleRecord::CommandQueueCreated,
          EngineLifecycleRecord::CommandQueueReleased,
          EngineLifecycleRecord::InitializationFailed,
          EngineLifecycleRecord::FailedSnapshotPublished}},
        {EngineInitializationStage::RenderBackend,
         {EngineLifecycleRecord::CommandQueueCreated,
          EngineLifecycleRecord::EventQueueCreated,
          EngineLifecycleRecord::EventQueueReleased,
          EngineLifecycleRecord::CommandQueueReleased,
          EngineLifecycleRecord::InitializationFailed,
          EngineLifecycleRecord::FailedSnapshotPublished}},
        {EngineInitializationStage::ComputeBackend,
         {EngineLifecycleRecord::CommandQueueCreated,
          EngineLifecycleRecord::EventQueueCreated,
          EngineLifecycleRecord::RenderBackendCreated,
          EngineLifecycleRecord::RenderBackendReleased,
          EngineLifecycleRecord::EventQueueReleased,
          EngineLifecycleRecord::CommandQueueReleased,
          EngineLifecycleRecord::InitializationFailed,
          EngineLifecycleRecord::FailedSnapshotPublished}},
    }};

    for (const FailureCase& failure : cases) {
        Engine engine;
        assert(EngineTestAccess::failInitializationAt(engine, failure.stage));

        const Result<void> initialized = engine.init(smallConfig());
        assert(!initialized.hasValue());
        assert(initialized.error().domain == dzc::ErrorDomain::Resource);
        assert(engine.getSnapshot()->state == EngineState::Failed);
        assert(engine.getSnapshot()->mostRecentError.has_value());
        assertNoResources(EngineTestAccess::resourceState(engine));

        const Result<void> repeatInit = engine.init(smallConfig());
        assert(!repeatInit.hasValue());
        assert(repeatInit.error().domain == dzc::ErrorDomain::Internal);
        assert(repeatInit.error().code == 1U);

        const auto trace = EngineTestAccess::lifecycleTrace(engine);
        assert(trace != nullptr);
        assert(trace->records == failure.expected);

        engine.shutdown();
        assert(engine.getSnapshot()->state == EngineState::Stopped);
        assertNoResources(EngineTestAccess::resourceState(engine));
    }
}

void testShutdownUsesSpecifiedOrderAndIsIdempotent() {
    Engine engine;
    assertSuccess(engine.init(smallConfig()));
    engine.shutdown();

    assert(engine.getSnapshot()->state == EngineState::Stopped);
    const auto resources = EngineTestAccess::resourceState(engine);
    assert(!resources.commandQueue);
    assert(resources.eventQueue);
    assert(!resources.renderBackend);
    assert(!resources.computeBackend);

    const auto beforeRepeatShutdown = EngineTestAccess::lifecycleTrace(engine);
    assertRecords(
        beforeRepeatShutdown,
        {EngineLifecycleRecord::CommandQueueCreated,
         EngineLifecycleRecord::EventQueueCreated,
         EngineLifecycleRecord::RenderBackendCreated,
         EngineLifecycleRecord::ComputeBackendCreated,
         EngineLifecycleRecord::ReadySnapshotPublished,
         EngineLifecycleRecord::StoppingRequested,
         EngineLifecycleRecord::CommandQueueClosed,
         EngineLifecycleRecord::CommandQueueReleased,
         EngineLifecycleRecord::ComputeBackendReleased,
         EngineLifecycleRecord::RenderBackendReleased,
         EngineLifecycleRecord::DatasetSessionCleared,
         EngineLifecycleRecord::SceneCleared,
         EngineLifecycleRecord::StoppedSnapshotPublished,
         EngineLifecycleRecord::EventQueueClosed});

    const std::vector<EngineLifecycleRecord> recordsBeforeRepeat = beforeRepeatShutdown->records;
    engine.shutdown();
    const auto afterRepeatShutdown = EngineTestAccess::lifecycleTrace(engine);
    assert(afterRepeatShutdown->records == recordsBeforeRepeat);
}

void testLoadingShutdownCancelsAndClearsDatasetAndEventsDrain() {
    Engine engine;
    assertSuccess(engine.init(smallConfig()));
    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"candidate.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);
    assert(engine.getSnapshot()->dataset.state == DatasetState::Opening);

    engine.shutdown();
    const auto snapshot = engine.getSnapshot();
    assert(snapshot->state == EngineState::Stopped);
    assert(snapshot->dataset.state == DatasetState::None);

    const auto trace = EngineTestAccess::lifecycleTrace(engine);
    const auto cancellation = std::find(
        trace->records.begin(),
        trace->records.end(),
        EngineLifecycleRecord::DatasetCancellationRequested);
    const auto computeRelease = std::find(
        trace->records.begin(),
        trace->records.end(),
        EngineLifecycleRecord::ComputeBackendReleased);
    const auto datasetClear = std::find(
        trace->records.begin(),
        trace->records.end(),
        EngineLifecycleRecord::DatasetSessionCleared);
    assert(cancellation != trace->records.end());
    assert(computeRelease != trace->records.end());
    assert(datasetClear != trace->records.end());
    assert(cancellation < computeRelease);
    assert(computeRelease < datasetClear);

    // A closed queue remains allocated and can still be drained safely.
    assert(engine.pollEvents().empty());
    assert(EngineTestAccess::resourceState(engine).eventQueue);
}

void testShutdownPreservesAcceptedEventsForPolling() {
    Engine engine;
    assertSuccess(engine.init(smallConfig()));
    assertSuccess(engine.enqueueCommand(LoadDatasetCommand{"completed.las"}));
    assertSuccess(engine.update(FrameInput{}));
    assert(engine.getSnapshot()->state == EngineState::Loading);

    dzc::tasks::TaskCompletion completion{
        dzc::TaskId{71U}, dzc::DatasetId{1U}, Result<void>::success()};
    assert(EngineTestAccess::injectDatasetCompletion(engine, std::move(completion)));
    assertSuccess(engine.update(FrameInput{}));

    engine.shutdown();
    const auto events = engine.pollEvents();
    assert(events.size() == 1U);
    const auto* loaded = std::get_if<DatasetLoadedEvent>(&events.front());
    assert(loaded != nullptr);
    assert(loaded->datasetId == dzc::DatasetId{1U});
    assert(engine.pollEvents().empty());
}

void testDestructorPerformsShutdownFallback() {
    std::shared_ptr<const EngineLifecycleTrace> trace;
    {
        Engine engine;
        assertSuccess(engine.init(smallConfig()));
        trace = EngineTestAccess::lifecycleTrace(engine);
        assert(trace != nullptr);
    }

    assert(std::find(
               trace->records.begin(),
               trace->records.end(),
               EngineLifecycleRecord::StoppedSnapshotPublished) != trace->records.end());
    assert(std::find(
               trace->records.begin(),
               trace->records.end(),
               EngineLifecycleRecord::EventQueueClosed) != trace->records.end());
}

} // namespace

int main() {
    testSuccessfulInitializationCreatesResourcesInOrder();
    testInitializationFailureRollsBackEveryStage();
    testShutdownUsesSpecifiedOrderAndIsIdempotent();
    testLoadingShutdownCancelsAndClearsDatasetAndEventsDrain();
    testShutdownPreservesAcceptedEventsForPolling();
    testDestructorPerformsShutdownFallback();
    return 0;
}
