#include "dzc/Engine.h"

#include "compute/common/ComputeBackendFactory.h"
#include "engine/DatasetSession.h"
#include "engine/EngineCoordinator.h"
#include "engine/EngineQueues.h"
#include "engine/EngineStateMachine.h"
#include "engine/EngineTestAccess.h"
#include "render/common/RenderBackendFactory.h"
#include "scene/Scene.h"
#include "tasks/CommandCoalescer.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidConfiguration = 1U;

Error invalidStateError(const char* operation) {
    return Error{
        ErrorDomain::Internal,
        static_cast<std::uint32_t>(EngineErrorCode::InvalidState),
        "Invalid engine state",
        "The requested Engine operation is not legal from the current state.",
        operation};
}

Error invalidConfigurationError() {
    return Error{
        ErrorDomain::Configuration,
        kInvalidConfiguration,
        "Invalid engine configuration",
        "Command and event queue capacities must be greater than zero.",
        "Engine::init"};
}

Error invalidCommandError() {
    return Error{
        ErrorDomain::Configuration,
        kInvalidConfiguration,
        "Invalid engine command",
        "The command violates the EngineCommand value constraints.",
        "Engine::enqueueCommand"};
}

Error queueFullError() {
    return Error{
        ErrorDomain::Task,
        static_cast<std::uint32_t>(tasks::TaskErrorCode::QueueFull),
        "Engine command queue is full",
        "The command was rejected because the bounded command queue is full.",
        "Engine::enqueueCommand"};
}

bool isOperationalState(EngineState state) noexcept {
    return state == EngineState::Ready || state == EngineState::Running ||
           state == EngineState::Loading;
}

class FakeRenderBackend final : public IRenderBackend {
public:
    Result<void> init(const RenderBackendConfig&) override { return Result<void>::success(); }
    Result<void> upload(const UploadBatch&) override { return Result<void>::success(); }
    Result<void> update(const RenderFrame&) override { return Result<void>::success(); }
    Result<void> render() override { return Result<void>::success(); }
    Result<void> resize(const RenderSize&) override { return Result<void>::success(); }
    void release(ChunkId) noexcept override {}
    void shutdown() noexcept override {}
};
class FakeComputeBackend final : public IComputeBackend {};

} // namespace

class Engine::Impl final {
    friend class EngineTestAccess;

public:
    Impl()
        : m_snapshot(std::make_shared<EngineSnapshot>()),
          m_lifecycleTrace(std::make_shared<EngineLifecycleTrace>()),
          m_coordinator(makeCoordinatorStages()) {}

    Result<void> init(const EngineConfig& config) {
        if (m_stateMachine.state() != EngineState::Created) {
            return Result<void>::failure(invalidStateError("Engine::init"));
        }
        if (!config.hasValidQueueCapacities()) {
            return Result<void>::failure(invalidConfigurationError());
        }

        const Result<void> initializing = m_stateMachine.transition(EngineStateTrigger::Init);
        if (!initializing.hasValue()) {
            return initializing;
        }

        std::unique_ptr<tasks::CommandCoalescer> commandQueue;
        std::unique_ptr<EngineEventQueue> eventQueue;
        std::unique_ptr<IRenderBackend> renderBackend;
        std::unique_ptr<IComputeBackend> computeBackend;
        auto rollback = [&]() noexcept {
            if (computeBackend != nullptr) {
                computeBackend.reset();
                recordLifecycle(EngineLifecycleRecord::ComputeBackendReleased);
            }
            if (renderBackend != nullptr) {
                renderBackend.reset();
                recordLifecycle(EngineLifecycleRecord::RenderBackendReleased);
            }
            if (eventQueue != nullptr) {
                eventQueue.reset();
                recordLifecycle(EngineLifecycleRecord::EventQueueReleased);
            }
            if (commandQueue != nullptr) {
                commandQueue.reset();
                recordLifecycle(EngineLifecycleRecord::CommandQueueReleased);
            }
        };

        try {
            throwIfInitializationFailureInjected(EngineInitializationStage::CommandQueue);
            commandQueue = std::make_unique<tasks::CommandCoalescer>(
                static_cast<std::size_t>(config.commandQueueCapacity));
            recordLifecycle(EngineLifecycleRecord::CommandQueueCreated);

            throwIfInitializationFailureInjected(EngineInitializationStage::EventQueue);
            eventQueue = std::make_unique<EngineEventQueue>(
                static_cast<std::size_t>(config.eventQueueCapacity));
            recordLifecycle(EngineLifecycleRecord::EventQueueCreated);

            throwIfInitializationFailureInjected(EngineInitializationStage::RenderBackend);
            renderBackend = std::make_unique<FakeRenderBackend>();
            recordLifecycle(EngineLifecycleRecord::RenderBackendCreated);

            throwIfInitializationFailureInjected(EngineInitializationStage::ComputeBackend);
            computeBackend = std::make_unique<FakeComputeBackend>();
            recordLifecycle(EngineLifecycleRecord::ComputeBackendCreated);
        } catch (const std::exception& exception) {
            rollback();
            recordLifecycle(EngineLifecycleRecord::InitializationFailed);
            return finishInitializationFailure(exception.what());
        } catch (...) {
            rollback();
            recordLifecycle(EngineLifecycleRecord::InitializationFailed);
            return finishInitializationFailure("Unknown exception during Engine initialization.");
        }

        m_config = config;
        m_requestedCudaMode = config.cudaMode;
        m_commandQueue = std::move(commandQueue);
        m_eventQueue = std::move(eventQueue);
        m_renderBackend = std::move(renderBackend);
        m_computeBackend = std::move(computeBackend);
        m_shutdownRequested.store(false, std::memory_order_release);
        m_initializationFailureStage.reset();

        const Result<void> ready =
            m_stateMachine.transition(EngineStateTrigger::InitializationSucceeded);
        if (!ready.hasValue()) {
            return ready;
        }
        publishSnapshot(EngineState::Ready);
        recordLifecycle(EngineLifecycleRecord::ReadySnapshotPublished);
        return Result<void>::success();
    }

    Result<void> enqueueCommand(EngineCommand command) {
        if (!isOperationalState(m_stateMachine.state())) {
            return Result<void>::failure(invalidStateError("Engine::enqueueCommand"));
        }
        if (!isValidEngineCommand(command)) {
            return Result<void>::failure(invalidCommandError());
        }

        if (std::holds_alternative<ShutdownCommand>(command)) {
            m_shutdownRequested.store(true, std::memory_order_release);
            return Result<void>::success();
        }
        if (m_shutdownRequested.load(std::memory_order_acquire)) {
            return Result<void>::failure(invalidStateError("Engine::enqueueCommand"));
        }
        if (m_commandQueue == nullptr || !m_commandQueue->push(std::move(command))) {
            return Result<void>::failure(queueFullError());
        }
        return Result<void>::success();
    }

    Result<void> update(const FrameInput& input) {
        if (!isOperationalState(m_stateMachine.state())) {
            return Result<void>::failure(invalidStateError("Engine::update"));
        }
        return m_coordinator.run(input);
    }

    Result<void> render() const {
        if (!isOperationalState(m_stateMachine.state())) {
            return Result<void>::failure(invalidStateError("Engine::render"));
        }
        return Result<void>::success();
    }

    Result<void> resize(const RenderSize& size) {
        if (!isOperationalState(m_stateMachine.state())) {
            return Result<void>::failure(invalidStateError("Engine::resize"));
        }

        SceneParameters parameters = m_scene.frameInput().parameters;
        parameters.renderSize = size;
        const Result<void> result = m_scene.applyParameters(parameters);
        if (result.hasValue()) {
            publishSnapshot(m_stateMachine.state());
        }
        return result;
    }

    std::shared_ptr<const EngineSnapshot> snapshot() const {
        return std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
    }

    std::vector<EngineEvent> pollEvents() {
        return m_eventQueue == nullptr ? std::vector<EngineEvent>{} : m_eventQueue->poll();
    }

    void injectDatasetCompletion(tasks::TaskCompletion completion) {
        m_datasetSession.injectCompletionForTesting(std::move(completion));
    }

    void shutdown() noexcept {
        const EngineState state = m_stateMachine.state();
        if (state == EngineState::Created || state == EngineState::Initializing ||
            state == EngineState::Stopped) {
            return;
        }

        if (state == EngineState::Ready || state == EngineState::Running ||
            state == EngineState::Loading || state == EngineState::Failed) {
            if (!m_stateMachine.transition(EngineStateTrigger::Shutdown).hasValue()) {
                return;
            }
        }

        if (m_stateMachine.state() != EngineState::ShuttingDown) {
            return;
        }

        m_shutdownRequested.store(true, std::memory_order_release);
        recordLifecycle(EngineLifecycleRecord::StoppingRequested);

        if (m_commandQueue != nullptr) {
            m_commandQueue->close();
            recordLifecycle(EngineLifecycleRecord::CommandQueueClosed);
            m_commandQueue.reset();
            recordLifecycle(EngineLifecycleRecord::CommandQueueReleased);
        }

        if (m_datasetSession.requestShutdownCancellation()) {
            recordLifecycle(EngineLifecycleRecord::DatasetCancellationRequested);
        }

        if (m_computeBackend != nullptr) {
            m_computeBackend.reset();
            recordLifecycle(EngineLifecycleRecord::ComputeBackendReleased);
        }
        if (m_renderBackend != nullptr) {
            m_renderBackend.reset();
            recordLifecycle(EngineLifecycleRecord::RenderBackendReleased);
        }

        m_datasetSession.clearForShutdown();
        m_scene.clearDataset();
        recordLifecycle(EngineLifecycleRecord::DatasetSessionCleared);
        recordLifecycle(EngineLifecycleRecord::SceneCleared);
        m_mostRecentError.reset();

        static_cast<void>(m_stateMachine.transition(EngineStateTrigger::ResourcesReleased));
        publishSnapshot(EngineState::Stopped);
        recordLifecycle(EngineLifecycleRecord::StoppedSnapshotPublished);

        if (m_eventQueue != nullptr) {
            m_eventQueue->close();
            recordLifecycle(EngineLifecycleRecord::EventQueueClosed);
        }
    }

private:
    void recordLifecycle(EngineLifecycleRecord record) noexcept {
        try {
            m_lifecycleTrace->records.push_back(record);
        } catch (...) {
            // Lifecycle tracing is test-only and must never compromise noexcept cleanup.
        }
    }

    void throwIfInitializationFailureInjected(EngineInitializationStage stage) {
        if (m_initializationFailureStage.has_value() &&
            m_initializationFailureStage.value() == stage) {
            throw std::runtime_error("Injected Engine initialization failure.");
        }
    }

    Result<void> finishInitializationFailure(const char* description) {
        const Error error{
            ErrorDomain::Resource,
            kInvalidConfiguration,
            "Engine initialization failed",
            description,
            "Engine::init"};
        static_cast<void>(m_stateMachine.transition(EngineStateTrigger::InitializationFailed));
        publishSnapshot(EngineState::Failed, error);
        recordLifecycle(EngineLifecycleRecord::FailedSnapshotPublished);
        return Result<void>::failure(error);
    }

    bool failInitializationAt(EngineInitializationStage stage) noexcept {
        if (m_stateMachine.state() != EngineState::Created) {
            return false;
        }
        m_initializationFailureStage = stage;
        return true;
    }

    std::shared_ptr<const EngineLifecycleTrace> lifecycleTrace() const {
        return m_lifecycleTrace;
    }

    EngineLifecycleResourceState resourceState() const noexcept {
        return EngineLifecycleResourceState{
            m_commandQueue != nullptr,
            m_eventQueue != nullptr,
            m_renderBackend != nullptr,
            m_computeBackend != nullptr};
    }

    EngineCoordinatorStages makeCoordinatorStages() {
        const auto noOp = [](const FrameInput&) {
            return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Continue);
        };

        EngineCoordinatorStages stages;
        stages.command = [this](const FrameInput& input) {
            return consumeCommands(input);
        };
        stages.taskCompletion = [this](const FrameInput& input) {
            return consumeTaskCompletions(input);
        };
        stages.camera = noOp;
        stages.visibility = noOp;
        stages.residency = noOp;
        stages.frameDescription = noOp;
        stages.diagnostics = noOp;
        stages.snapshot = [this](const FrameInput&) {
            if (m_stateMachine.state() == EngineState::Ready) {
                const Result<void> running =
                    m_stateMachine.transition(EngineStateTrigger::FirstValidFrame);
                if (!running.hasValue()) {
                    return Result<EngineCoordinatorControl>::failure(running.error());
                }
            }

            ++m_snapshotFrame;
            publishSnapshot(m_stateMachine.state());
            return Result<EngineCoordinatorControl>::success(
                EngineCoordinatorControl::Continue);
        };
        return stages;
    }

    Result<EngineCoordinatorControl> consumeCommands(const FrameInput&) {
        if (m_commandQueue != nullptr) {
            const auto commands = m_commandQueue->popBatch(
                static_cast<std::size_t>(m_config.commandQueueCapacity));
            for (const EngineCommand& command : commands) {
                if (std::holds_alternative<ShutdownCommand>(command)) {
                    shutdown();
                    return Result<EngineCoordinatorControl>::success(
                        EngineCoordinatorControl::Stop);
                }
                const Result<void> applied = applyCommand(command);
                if (!applied.hasValue()) {
                    return Result<EngineCoordinatorControl>::failure(applied.error());
                }
            }
        }

        if (m_shutdownRequested.load(std::memory_order_acquire)) {
            shutdown();
            return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Stop);
        }

        return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Continue);
    }

    Result<EngineCoordinatorControl> consumeTaskCompletions(const FrameInput&) {
        for (tasks::TaskCompletion completion : m_datasetSession.takeInjectedCompletions()) {
            const DatasetSessionCompletion applied =
                m_datasetSession.applyCompletion(std::move(completion));
            const Result<void> result = applyDatasetCompletion(applied);
            if (!result.hasValue()) {
                return Result<EngineCoordinatorControl>::failure(result.error());
            }
        }
        return Result<EngineCoordinatorControl>::success(EngineCoordinatorControl::Continue);
    }

    Result<void> applyDatasetCompletion(const DatasetSessionCompletion& completion) {
        switch (completion.kind) {
        case DatasetSessionCompletionKind::Ignored:
            return Result<void>::success();

        case DatasetSessionCompletionKind::Loaded: {
            m_scene.setDataset(m_datasetSession.sceneDatasetId());
            m_mostRecentError.reset();
            const Result<void> transition =
                m_stateMachine.transition(EngineStateTrigger::DatasetCompleted);
            if (!transition.hasValue()) {
                return transition;
            }
            pushEvent(DatasetLoadedEvent{completion.datasetId});
            return Result<void>::success();
        }

        case DatasetSessionCompletionKind::Cancelled: {
            m_scene.setDataset(m_datasetSession.sceneDatasetId());
            const Result<void> transition =
                m_stateMachine.transition(EngineStateTrigger::DatasetCancelled);
            if (!transition.hasValue()) {
                return transition;
            }
            pushEvent(DatasetLoadCancelledEvent{completion.datasetId});
            return Result<void>::success();
        }

        case DatasetSessionCompletionKind::Failed: {
            m_scene.setDataset(m_datasetSession.sceneDatasetId());
            if (completion.error.has_value()) {
                m_mostRecentError = completion.error;
                pushEvent(ErrorEvent{
                    EventSeverity::RecoverableError,
                    *completion.error,
                    EventContext{completion.datasetId, {}, completion.taskId, {}}});
            }
            const Result<void> transition =
                m_stateMachine.transition(EngineStateTrigger::DatasetRecoverableFailure);
            return transition;
        }
        }

        return Result<void>::success();
    }

    Result<void> applyCommand(const EngineCommand& command) {
        SceneParameters parameters = m_scene.frameInput().parameters;
        bool sceneChanged = false;
        Result<void> commandResult = Result<void>::success();

        std::visit(
            [this, &parameters, &sceneChanged, &commandResult](const auto& value) {
                using Command = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Command, LoadDatasetCommand>) {
                    const Result<DatasetId> session = m_datasetSession.beginLoad(value.path);
                    if (!session.hasValue()) {
                        commandResult = Result<void>::failure(session.error());
                        return;
                    }
                    if (m_stateMachine.state() != EngineState::Loading) {
                        commandResult = m_stateMachine.transition(EngineStateTrigger::LoadDataset);
                    }
                } else if constexpr (std::is_same_v<Command, CancelDatasetLoadCommand>) {
                    static_cast<void>(m_datasetSession.requestCancel(value.datasetId));
                } else if constexpr (std::is_same_v<Command, UnloadDatasetCommand>) {
                    if (m_datasetSession.unload(value.datasetId)) {
                        m_scene.setDataset(m_datasetSession.sceneDatasetId());
                        if (!m_datasetSession.sceneDatasetId().has_value() &&
                            m_stateMachine.state() != EngineState::Loading) {
                            m_mostRecentError.reset();
                        }
                    }
                } else if constexpr (std::is_same_v<Command, SetPointSizeCommand>) {
                    parameters.pointSize = value.pixels;
                    sceneChanged = true;
                } else if constexpr (std::is_same_v<Command, SetShadingModeCommand>) {
                    parameters.shadingMode = value.mode;
                    sceneChanged = true;
                } else if constexpr (std::is_same_v<Command, SetFixedColorCommand>) {
                    parameters.fixedColor = value.color;
                    sceneChanged = true;
                } else if constexpr (std::is_same_v<Command, SetBackgroundColorCommand>) {
                    parameters.backgroundColor = value.color;
                    sceneChanged = true;
                } else if constexpr (std::is_same_v<Command, ResizeCommand>) {
                    parameters.renderSize = value.size;
                    sceneChanged = true;
                } else if constexpr (std::is_same_v<Command, SetCudaModeCommand>) {
                    m_requestedCudaMode = value.mode;
                } else if constexpr (std::is_same_v<Command, SubmitInputCommand>) {
                    static_cast<void>(value);
                }
            },
            command);

        if (!commandResult.hasValue()) {
            return commandResult;
        }
        return sceneChanged ? m_scene.applyParameters(parameters) : Result<void>::success();
    }

    void pushEvent(EngineEvent event) {
        if (m_eventQueue != nullptr) {
            static_cast<void>(m_eventQueue->tryPush(std::move(event)));
        }
    }

    void publishSnapshot(EngineState state, std::optional<Error> error = std::nullopt) {
        const SceneFrameInput sceneFrame = m_scene.frameInput();
        const auto current = std::atomic_load_explicit(&m_snapshot, std::memory_order_acquire);
        auto next = std::make_shared<EngineSnapshot>(*current);
        next->frameId.value = m_snapshotFrame;
        next->state = state;
        next->backend = m_config.backend;
        next->cudaAvailable = false;
        next->cudaEnabled = false;
        next->cudaMode = m_requestedCudaMode;
        next->dataset = m_datasetSession.snapshotSummary();
        next->pointSize = sceneFrame.parameters.pointSize;
        next->shadingMode = sceneFrame.parameters.shadingMode;
        next->fixedColor = sceneFrame.parameters.fixedColor;
        next->backgroundColor = sceneFrame.parameters.backgroundColor;
        if (error.has_value()) {
            next->mostRecentError = std::move(error);
        } else {
            next->mostRecentError = m_mostRecentError;
        }
        std::atomic_store_explicit(
            &m_snapshot,
            std::shared_ptr<const EngineSnapshot>(std::move(next)),
            std::memory_order_release);
    }

    EngineStateMachine m_stateMachine;
    std::shared_ptr<const EngineSnapshot> m_snapshot;
    std::shared_ptr<EngineLifecycleTrace> m_lifecycleTrace;
    EngineCoordinator m_coordinator;
    EngineConfig m_config;
    Scene m_scene;
    DatasetSession m_datasetSession;
    std::unique_ptr<tasks::CommandCoalescer> m_commandQueue;
    std::unique_ptr<EngineEventQueue> m_eventQueue;
    std::unique_ptr<IRenderBackend> m_renderBackend;
    std::unique_ptr<IComputeBackend> m_computeBackend;
    OptionalFeatureMode m_requestedCudaMode{OptionalFeatureMode::Auto};
    std::atomic_bool m_shutdownRequested{false};
    std::optional<Error> m_mostRecentError;
    std::optional<EngineInitializationStage> m_initializationFailureStage;
    std::uint64_t m_snapshotFrame{0U};
};

Engine::Engine()
    : m_impl(std::make_unique<Impl>()) {}

Engine::~Engine() {
    shutdown();
}

Engine::Engine(Engine&& other) noexcept = default;

Engine& Engine::operator=(Engine&& other) noexcept {
    if (this != &other) {
        shutdown();
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Result<void> Engine::init(const EngineConfig& config) {
    if (m_impl == nullptr) {
        return Result<void>::failure(invalidStateError("Engine::init"));
    }
    return m_impl->init(config);
}

Result<void> Engine::enqueueCommand(EngineCommand command) {
    if (m_impl == nullptr) {
        return Result<void>::failure(invalidStateError("Engine::enqueueCommand"));
    }
    return m_impl->enqueueCommand(std::move(command));
}

Result<void> Engine::update(const FrameInput& input) {
    if (m_impl == nullptr) {
        return Result<void>::failure(invalidStateError("Engine::update"));
    }
    return m_impl->update(input);
}

Result<void> Engine::render() {
    if (m_impl == nullptr) {
        return Result<void>::failure(invalidStateError("Engine::render"));
    }
    return m_impl->render();
}

Result<void> Engine::resize(const RenderSize& size) {
    if (m_impl == nullptr) {
        return Result<void>::failure(invalidStateError("Engine::resize"));
    }
    return m_impl->resize(size);
}

std::shared_ptr<const EngineSnapshot> Engine::getSnapshot() const {
    if (m_impl == nullptr) {
        return std::make_shared<const EngineSnapshot>();
    }
    return m_impl->snapshot();
}

std::vector<EngineEvent> Engine::pollEvents() {
    if (m_impl == nullptr) {
        return {};
    }
    return m_impl->pollEvents();
}

void Engine::shutdown() noexcept {
    if (m_impl != nullptr) {
        m_impl->shutdown();
    }
}

bool EngineTestAccess::injectDatasetCompletion(
    Engine& engine,
    tasks::TaskCompletion completion) {
    if (engine.m_impl == nullptr) {
        return false;
    }
    engine.m_impl->injectDatasetCompletion(std::move(completion));
    return true;
}

bool EngineTestAccess::failInitializationAt(
    Engine& engine,
    EngineInitializationStage stage) noexcept {
    return engine.m_impl != nullptr && engine.m_impl->failInitializationAt(stage);
}

std::shared_ptr<const EngineLifecycleTrace> EngineTestAccess::lifecycleTrace(const Engine& engine) {
    return engine.m_impl == nullptr
               ? std::make_shared<const EngineLifecycleTrace>()
               : engine.m_impl->lifecycleTrace();
}

EngineLifecycleResourceState EngineTestAccess::resourceState(const Engine& engine) noexcept {
    return engine.m_impl == nullptr
               ? EngineLifecycleResourceState{}
               : engine.m_impl->resourceState();
}

} // namespace dzc
