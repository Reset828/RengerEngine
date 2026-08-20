#pragma once

#include <dzc/Engine.h>

#include "tasks/TaskCompletion.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace dzc {

// Private test-only lifecycle protocol. It deliberately stays outside the
// public include tree so production Engine callers cannot configure failures.
enum class EngineInitializationStage : std::uint8_t {
    CommandQueue,
    EventQueue,
    RenderBackend,
    ComputeBackend
};

enum class EngineLifecycleRecord : std::uint8_t {
    CommandQueueCreated,
    EventQueueCreated,
    RenderBackendCreated,
    ComputeBackendCreated,
    InitializationFailed,
    FailedSnapshotPublished,
    ReadySnapshotPublished,
    ComputeBackendReleased,
    RenderBackendReleased,
    EventQueueReleased,
    CommandQueueReleased,
    StoppingRequested,
    CommandQueueClosed,
    DatasetCancellationRequested,
    DatasetSessionCleared,
    SceneCleared,
    StoppedSnapshotPublished,
    EventQueueClosed
};

struct EngineLifecycleTrace final {
    std::vector<EngineLifecycleRecord> records;
};

struct EngineLifecycleResourceState final {
    bool commandQueue{false};
    bool eventQueue{false};
    bool renderBackend{false};
    bool computeBackend{false};
};

// Private test seam for deterministic Dataset completion ordering and Engine
// lifecycle failure/ordering verification. This header is intentionally outside
// the public include tree.
class EngineTestAccess final {
public:
    static bool injectDatasetCompletion(Engine& engine, tasks::TaskCompletion completion);

    static bool failInitializationAt(
        Engine& engine,
        EngineInitializationStage stage) noexcept;
    static std::shared_ptr<const EngineLifecycleTrace> lifecycleTrace(const Engine& engine);
    static EngineLifecycleResourceState resourceState(const Engine& engine) noexcept;
};

} // namespace dzc
