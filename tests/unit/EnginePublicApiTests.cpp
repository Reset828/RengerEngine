#include <dzc/Engine.h>

#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

void assertInvalidState(const dzc::Result<void>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Internal);
    assert(result.error().code == 1U);
}

void assertSuccess(const dzc::Result<void>& result) {
    assert(result.hasValue());
}

void assertQueueFull(const dzc::Result<void>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Task);
    assert(result.error().code == 3U);
}

void testPublicTypeContract() {
    static_assert(!std::is_copy_constructible_v<dzc::Engine>);
    static_assert(!std::is_copy_assignable_v<dzc::Engine>);
    static_assert(std::is_move_constructible_v<dzc::Engine>);
    static_assert(std::is_move_assignable_v<dzc::Engine>);
    static_assert(std::is_default_constructible_v<dzc::FrameInput>);
}

void testDefaultSnapshotAndInvalidCalls() {
    dzc::Engine engine;
    const auto snapshot = engine.getSnapshot();
    assert(snapshot != nullptr);
    assert(snapshot->state == dzc::EngineState::Created);
    assert(snapshot->frameId.value == 0U);
    assert(engine.pollEvents().empty());
    assertInvalidState(engine.update(dzc::FrameInput{}));
    assertInvalidState(engine.render());
    assertInvalidState(engine.resize(dzc::RenderSize{640U, 480U, 1.0F}));
    assertInvalidState(engine.enqueueCommand(dzc::ResetViewCommand{}));
}

void testInitializationAndRunningLifecycle() {
    dzc::Engine engine;
    dzc::EngineConfig config;
    config.commandQueueCapacity = 2U;
    config.eventQueueCapacity = 2U;
    assertSuccess(engine.init(config));
    assert(engine.getSnapshot()->state == dzc::EngineState::Ready);

    assertSuccess(engine.update(dzc::FrameInput{}));
    const auto running = engine.getSnapshot();
    assert(running->state == dzc::EngineState::Running);
    assert(running->frameId.value == 1U);
    assertSuccess(engine.update(dzc::FrameInput{}));
    assert(engine.getSnapshot()->frameId.value == 2U);
    assertSuccess(engine.render());
    assertSuccess(engine.resize(dzc::RenderSize{1280U, 720U, 2.0F}));
    assertInvalidState(engine.init(config));

    engine.shutdown();
    assert(engine.getSnapshot()->state == dzc::EngineState::Stopped);
    assertInvalidState(engine.render());
    assertInvalidState(engine.update(dzc::FrameInput{}));
    assertInvalidState(engine.resize(dzc::RenderSize{1U, 1U, 1.0F}));
    assertInvalidState(engine.enqueueCommand(dzc::ResetViewCommand{}));
    engine.shutdown();
}

void testInvalidConfigurationPreservesCreatedState() {
    for (const bool zeroCommandCapacity : {true, false}) {
        dzc::Engine engine;
        dzc::EngineConfig config;
        if (zeroCommandCapacity) {
            config.commandQueueCapacity = 0U;
        } else {
            config.eventQueueCapacity = 0U;
        }
        const auto result = engine.init(config);
        assert(!result.hasValue());
        assert(result.error().domain == dzc::ErrorDomain::Configuration);
        assert(result.error().code == 1U);
        assert(engine.getSnapshot()->state == dzc::EngineState::Created);
    }
}

void testCommandValidationAndQueuePolicy() {
    dzc::Engine engine;
    dzc::EngineConfig config;
    config.commandQueueCapacity = 1U;
    config.eventQueueCapacity = 1U;
    assertSuccess(engine.init(config));

    assertSuccess(engine.enqueueCommand(dzc::ResetViewCommand{}));
    assertQueueFull(engine.enqueueCommand(dzc::ResetViewCommand{}));
    assertSuccess(engine.enqueueCommand(dzc::ShutdownCommand{}));
    assertInvalidState(engine.enqueueCommand(dzc::ResetViewCommand{}));
    assertSuccess(engine.update(dzc::FrameInput{}));
    assert(engine.getSnapshot()->state == dzc::EngineState::Stopped);

    dzc::Engine second;
    assertSuccess(second.init(dzc::EngineConfig{}));
    assert(!second.enqueueCommand(dzc::LoadDatasetCommand{""}).hasValue());
    assert(!second.enqueueCommand(dzc::LoadDatasetCommand{std::string("bad\x80", 4)}).hasValue());
    assert(!second.enqueueCommand(dzc::SetPointSizeCommand{std::nanf("")}).hasValue());
}

void testCommandConsumptionAndCoalescing() {
    dzc::Engine engine;
    dzc::EngineConfig config;
    config.commandQueueCapacity = 4U;
    assertSuccess(engine.init(config));
    assertSuccess(engine.enqueueCommand(dzc::SetPointSizeCommand{2.0F}));
    assertSuccess(engine.enqueueCommand(dzc::SetShadingModeCommand{dzc::ShadingMode::Height}));
    assertSuccess(engine.enqueueCommand(dzc::SetPointSizeCommand{7.0F}));
    assertSuccess(engine.enqueueCommand(
        dzc::SetFixedColorCommand{dzc::ColorRgba{0.2F, 0.3F, 0.4F, 1.0F}}));
    assertSuccess(engine.update(dzc::FrameInput{}));

    const auto snapshot = engine.getSnapshot();
    assert(snapshot->state == dzc::EngineState::Running);
    assert(snapshot->pointSize == 7.0F);
    assert(snapshot->shadingMode == dzc::ShadingMode::Height);
    assert(snapshot->fixedColor == (dzc::ColorRgba{0.2F, 0.3F, 0.4F, 1.0F}));

    dzc::Engine barrier;
    dzc::EngineConfig barrierConfig;
    barrierConfig.commandQueueCapacity = 2U;
    assertSuccess(barrier.init(barrierConfig));
    assertSuccess(barrier.enqueueCommand(dzc::SetPointSizeCommand{2.0F}));
    assertSuccess(barrier.enqueueCommand(dzc::LoadDatasetCommand{"dataset"}));
    assertQueueFull(barrier.enqueueCommand(dzc::SetPointSizeCommand{6.0F}));
    assertSuccess(barrier.update(dzc::FrameInput{}));
    assert(barrier.getSnapshot()->pointSize == 2.0F);
}

void testDatasetAndViewCommandsStartDatasetLifecycle() {
    dzc::Engine engine;
    dzc::EngineConfig config;
    config.commandQueueCapacity = 4U;
    assertSuccess(engine.init(config));
    assertSuccess(engine.enqueueCommand(dzc::LoadDatasetCommand{"dataset"}));
    assertSuccess(engine.enqueueCommand(dzc::CancelDatasetLoadCommand{dzc::DatasetId{4U}}));
    assertSuccess(engine.enqueueCommand(dzc::UnloadDatasetCommand{dzc::DatasetId{4U}}));
    assertSuccess(engine.enqueueCommand(dzc::ResetViewCommand{}));
    assertSuccess(engine.update(dzc::FrameInput{}));

    const auto snapshot = engine.getSnapshot();
    assert(snapshot->state == dzc::EngineState::Loading);
    assert(snapshot->dataset.id == dzc::DatasetId{1U});
    assert(snapshot->dataset.state == dzc::DatasetState::Opening);
}

void testShutdownStopsCoordinatorBeforeSnapshotStage() {
    dzc::Engine engine;
    assertSuccess(engine.init(dzc::EngineConfig{}));
    const auto ready = engine.getSnapshot();
    assert(ready->state == dzc::EngineState::Ready);
    assert(ready->frameId.value == 0U);

    assertSuccess(engine.enqueueCommand(dzc::ShutdownCommand{}));
    assertSuccess(engine.update(dzc::FrameInput{}));

    const auto stopped = engine.getSnapshot();
    assert(stopped->state == dzc::EngineState::Stopped);
    assert(stopped->frameId.value == ready->frameId.value);
    assertInvalidState(engine.update(dzc::FrameInput{}));
}

void testMoveAndShutdownSafety() {
    dzc::Engine source;
    assertSuccess(source.init(dzc::EngineConfig{}));
    dzc::Engine moved(std::move(source));
    assert(moved.getSnapshot()->state == dzc::EngineState::Ready);

    dzc::Engine assigned;
    assigned = std::move(moved);
    assert(assigned.getSnapshot()->state == dzc::EngineState::Ready);
    assigned.shutdown();
    assigned.shutdown();
}

} // namespace

int main() {
    testPublicTypeContract();
    testDefaultSnapshotAndInvalidCalls();
    testInitializationAndRunningLifecycle();
    testInvalidConfigurationPreservesCreatedState();
    testCommandValidationAndQueuePolicy();
    testCommandConsumptionAndCoalescing();
    testDatasetAndViewCommandsStartDatasetLifecycle();
    testShutdownStopsCoordinatorBeforeSnapshotStage();
    testMoveAndShutdownSafety();
    return 0;
}
