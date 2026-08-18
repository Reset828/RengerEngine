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

void testCommandValidationAndCapacity() {
    dzc::Engine engine;
    dzc::EngineConfig config;
    config.commandQueueCapacity = 1U;
    config.eventQueueCapacity = 1U;
    assertSuccess(engine.init(config));

    assertSuccess(engine.enqueueCommand(dzc::ResetViewCommand{}));
    const auto full = engine.enqueueCommand(dzc::ShutdownCommand{});
    assert(!full.hasValue());
    assert(full.error().domain == dzc::ErrorDomain::Task);
    assert(full.error().code == 3U);

    dzc::Engine second;
    assertSuccess(second.init(dzc::EngineConfig{}));
    assert(!second.enqueueCommand(dzc::LoadDatasetCommand{""}).hasValue());
    assert(!second.enqueueCommand(dzc::LoadDatasetCommand{std::string("bad\x80", 4)}).hasValue());
    assert(!second.enqueueCommand(dzc::SetPointSizeCommand{std::nanf("")}).hasValue());
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
    testCommandValidationAndCapacity();
    testMoveAndShutdownSafety();
    return 0;
}