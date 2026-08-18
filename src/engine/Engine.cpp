#include "dzc/Engine.h"

#include "compute/common/ComputeBackendFactory.h"
#include "engine/EngineStateMachine.h"
#include "render/common/RenderBackendFactory.h"
#include "scene/Scene.h"
#include "tasks/BoundedQueue.h"
#include "tasks/TaskSystem.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
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
            m_commandQueue = std::make_unique<tasks::BoundedQueue<EngineCommand>>(
                config.commandQueueCapacity);
            m_eventQueue = std::make_unique<tasks::BoundedQueue<EngineEvent>>(
                config.eventQueueCapacity);
            m_renderBackend = std::make_unique<FakeRenderBackend>();
            m_computeBackend = std::make_unique<FakeComputeBackend>();
        } catch (const std::exception& exception) {
            m_stateMachine.transition(EngineStateTrigger::InitializationFailed);
            publishState(EngineState::Failed, exception.what());
            return Result<void>::failure(Error{
                ErrorDomain::Resource,
                kInvalidConfiguration,
                "Engine initialization failed",
                exception.what(),
                "Engine::init"});
        } catch (...) {
            m_stateMachine.transition(EngineStateTrigger::InitializationFailed);
            publishState(EngineState::Failed, "Unknown exception during Engine initialization.");
            return Result<void>::failure(Error{
                ErrorDomain::Resource,
                kInvalidConfiguration,
                "Engine initialization failed",
                "Unknown exception during Engine initialization.",
                "Engine::init"});
        }

        const Result<void> ready =
            m_stateMachine.transition(EngineStateTrigger::InitializationSucceeded);
        if (!ready.hasValue()) {
            return ready;
        }
        publishState(EngineState::Ready);
        return Result<void>::success();
    }

    Result<void> enqueueCommand(EngineCommand command) {
        if (!isOperationalState(m_stateMachine.state())) {
            return Result<void>::failure(invalidStateError("Engine::enqueueCommand"));
        }
        if (!isValidEngineCommand(command)) {
            return Result<void>::failure(Error{
                ErrorDomain::Configuration,
                kInvalidConfiguration,
                "Invalid engine command",
                "The command violates the EngineCommand value constraints.",
                "Engine::enqueueCommand"});
        }
        if (m_commandQueue == nullptr || !m_commandQueue->tryPush(std::move(command))) {
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

        if (state == EngineState::Ready) {
            const Result<void> running =
                m_stateMachine.transition(EngineStateTrigger::FirstValidFrame);
            if (!running.hasValue()) {
                return running;
            }
        }

        ++m_snapshotFrame;
        publishState(m_stateMachine.state());
        publishFrameId();
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

        SceneFrameInput frame = m_scene.frameInput();
        frame.parameters.renderSize = size;
        return m_scene.applyParameters(frame.parameters);
    }

    std::shared_ptr<const EngineSnapshot> snapshot() const {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return m_snapshot;
    }

    std::vector<EngineEvent> pollEvents() {
        std::vector<EngineEvent> events;
        if (m_eventQueue == nullptr) {
            return events;
        }
        while (std::optional<EngineEvent> event = m_eventQueue->tryPop()) {
            events.emplace_back(std::move(*event));
        }
        return events;
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
            if (m_commandQueue != nullptr) {
                m_commandQueue->close();
            }
            if (m_eventQueue != nullptr) {
                m_eventQueue->close();
            }
            m_commandQueue.reset();
            m_eventQueue.reset();
            m_renderBackend.reset();
            m_computeBackend.reset();
            m_stateMachine.transition(EngineStateTrigger::ResourcesReleased);
            publishState(EngineState::Stopped);
        }
    }

private:
    void publishState(EngineState state, const char* diagnostic = nullptr) {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        auto next = std::make_shared<EngineSnapshot>(*m_snapshot);
        next->state = state;
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

    void publishFrameId() {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        auto next = std::make_shared<EngineSnapshot>(*m_snapshot);
        next->frameId.value = m_snapshotFrame;
        next->state = m_stateMachine.state();
        m_snapshot = std::move(next);
    }

    EngineStateMachine m_stateMachine;
    EngineConfig m_config;
    Scene m_scene;
    std::unique_ptr<tasks::BoundedQueue<EngineCommand>> m_commandQueue;
    std::unique_ptr<tasks::BoundedQueue<EngineEvent>> m_eventQueue;
    std::unique_ptr<IRenderBackend> m_renderBackend;
    std::unique_ptr<IComputeBackend> m_computeBackend;
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