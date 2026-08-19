#include "dzc/Engine.h"

#include "compute/common/ComputeBackendFactory.h"
#include "engine/EngineQueues.h"
#include "engine/EngineStateMachine.h"
#include "render/common/RenderBackendFactory.h"
#include "scene/Scene.h"
#include "tasks/CommandCoalescer.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
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

class FakeRenderBackend final : public IRenderBackend {};
class FakeComputeBackend final : public IComputeBackend {};

} // namespace

class Engine::Impl final {
public:
    Impl()
        : m_snapshot(std::make_shared<EngineSnapshot>()) {}

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

        try {
            m_config = config;
            m_commandQueue = std::make_unique<tasks::CommandCoalescer>(
                static_cast<std::size_t>(config.commandQueueCapacity));
            m_eventQueue = std::make_unique<EngineEventQueue>(
                static_cast<std::size_t>(config.eventQueueCapacity));
            m_renderBackend = std::make_unique<FakeRenderBackend>();
            m_computeBackend = std::make_unique<FakeComputeBackend>();
            m_shutdownRequested.store(false, std::memory_order_release);
        } catch (const std::exception& exception) {
            static_cast<void>(m_stateMachine.transition(EngineStateTrigger::InitializationFailed));
            publishSnapshot(EngineState::Failed, exception.what());
            return Result<void>::failure(Error{
                ErrorDomain::Resource,
                kInvalidConfiguration,
                "Engine initialization failed",
                exception.what(),
                "Engine::init"});
        } catch (...) {
            constexpr const char* kUnknownFailure = "Unknown exception during Engine initialization.";
            static_cast<void>(m_stateMachine.transition(EngineStateTrigger::InitializationFailed));
            publishSnapshot(EngineState::Failed, kUnknownFailure);
            return Result<void>::failure(Error{
                ErrorDomain::Resource,
                kInvalidConfiguration,
                "Engine initialization failed",
                kUnknownFailure,
                "Engine::init"});
        }

        const Result<void> ready =
            m_stateMachine.transition(EngineStateTrigger::InitializationSucceeded);
        if (!ready.hasValue()) {
            return ready;
        }
        publishSnapshot(EngineState::Ready);
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
        (void)input;
        const EngineState state = m_stateMachine.state();
        if (!isOperationalState(state)) {
            return Result<void>::failure(invalidStateError("Engine::update"));
        }

        bool shutdownInBatch = false;
        if (m_commandQueue != nullptr) {
            const auto commands = m_commandQueue->popBatch(
                static_cast<std::size_t>(m_config.commandQueueCapacity));
            for (const EngineCommand& command : commands) {
                if (std::holds_alternative<ShutdownCommand>(command)) {
                    shutdownInBatch = true;
                    break;
                }
                const Result<void> applied = applyCommand(command);
                if (!applied.hasValue()) {
                    return applied;
                }
            }
        }

        if (shutdownInBatch || m_shutdownRequested.load(std::memory_order_acquire)) {
            shutdown();
            return Result<void>::success();
        }

        if (state == EngineState::Ready) {
            const Result<void> running =
                m_stateMachine.transition(EngineStateTrigger::FirstValidFrame);
            if (!running.hasValue()) {
                return running;
            }
        }

        ++m_snapshotFrame;
        publishSnapshot(m_stateMachine.state());
        return Result<void>::success();
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
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshot;
    }

    std::vector<EngineEvent> pollEvents() {
        return m_eventQueue == nullptr ? std::vector<EngineEvent>{} : m_eventQueue->poll();
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

        if (m_stateMachine.state() == EngineState::ShuttingDown) {
            m_shutdownRequested.store(true, std::memory_order_release);
            if (m_commandQueue != nullptr) {
                m_commandQueue->close();
            }
            if (m_eventQueue != nullptr) {
                m_eventQueue->close();
            }
            m_renderBackend.reset();
            m_computeBackend.reset();
            static_cast<void>(m_stateMachine.transition(EngineStateTrigger::ResourcesReleased));
            publishSnapshot(EngineState::Stopped);
        }
    }

private:
    Result<void> applyCommand(const EngineCommand& command) {
        SceneParameters parameters = m_scene.frameInput().parameters;
        bool sceneChanged = false;

        std::visit(
            [this, &parameters, &sceneChanged](const auto& value) {
                using Command = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Command, SetPointSizeCommand>) {
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
                } else {
                    // Dataset and camera commands are deliberately consumed as
                    // FIFO no-ops until their owning modules are implemented.
                }
            },
            command);

        return sceneChanged ? m_scene.applyParameters(parameters) : Result<void>::success();
    }

    void publishSnapshot(EngineState state, const char* diagnostic = nullptr) {
        const SceneFrameInput sceneFrame = m_scene.frameInput();
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        auto next = std::make_shared<EngineSnapshot>(*m_snapshot);
        next->frameId.value = m_snapshotFrame;
        next->state = state;
        next->backend = m_config.backend;
        next->cudaAvailable = false;
        next->cudaEnabled = false;
        next->pointSize = sceneFrame.parameters.pointSize;
        next->shadingMode = sceneFrame.parameters.shadingMode;
        next->fixedColor = sceneFrame.parameters.fixedColor;
        next->backgroundColor = sceneFrame.parameters.backgroundColor;
        if (diagnostic != nullptr) {
            next->mostRecentError = Error{
                ErrorDomain::Resource,
                kInvalidConfiguration,
                "Engine initialization failed",
                diagnostic,
                "Engine::init"};
        }
        m_snapshot = std::move(next);
    }

    EngineStateMachine m_stateMachine;
    EngineConfig m_config;
    Scene m_scene;
    std::unique_ptr<tasks::CommandCoalescer> m_commandQueue;
    std::unique_ptr<EngineEventQueue> m_eventQueue;
    std::unique_ptr<IRenderBackend> m_renderBackend;
    std::unique_ptr<IComputeBackend> m_computeBackend;
    OptionalFeatureMode m_requestedCudaMode{OptionalFeatureMode::Auto};
    std::atomic_bool m_shutdownRequested{false};
    mutable std::mutex m_snapshotMutex;
    std::shared_ptr<const EngineSnapshot> m_snapshot;
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

} // namespace dzc
