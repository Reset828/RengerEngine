#pragma once

#include "dzc/EngineCommand.h"
#include "dzc/EngineConfig.h"
#include "dzc/EngineEvent.h"
#include "dzc/EngineSnapshot.h"
#include "dzc/FrameInput.h"
#include "dzc/Result.h"
#include "dzc/EngineTypes.h"

#include <memory>
#include <vector>

namespace dzc {

class IRenderBackend;
class IComputeBackend;

class Engine final {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    Result<void> init(const EngineConfig& config);
    // Initializes the Engine with application-composed backend instances.
    Result<void> init(
        const EngineConfig& config,
        std::unique_ptr<IRenderBackend> renderBackend,
        std::unique_ptr<IComputeBackend> computeBackend);
    Result<void> enqueueCommand(EngineCommand command);
    Result<void> update(const FrameInput& input);
    Result<void> render();
    Result<void> resize(const RenderSize& size);

    std::shared_ptr<const EngineSnapshot> getSnapshot() const;
    std::vector<EngineEvent> pollEvents();

    void shutdown() noexcept;

private:
    friend class EngineTestAccess;
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
