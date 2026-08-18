#pragma once

#include "dzc/EngineTypes.h"
#include "dzc/Error.h"

#include <cstdint>
#include <string>
#include <variant>

namespace dzc {

enum class EventSeverity : std::uint8_t {
    Info,
    Warning,
    RecoverableError,
    FatalError
};

struct EventContext final {
    DatasetId datasetId;
    ChunkId chunkId;
    TaskId taskId;
    FrameId frameId;
};

struct MessageEvent final {
    EventSeverity severity{EventSeverity::Info};
    std::string message;
    EventContext context;
};

struct ErrorEvent final {
    EventSeverity severity{EventSeverity::RecoverableError};
    Error error;
    EventContext context;
};

struct DatasetProgressEvent final {
    DatasetId datasetId;
    std::uint64_t completedUnits{0};
    std::uint64_t totalUnits{0};
};

struct DatasetLoadedEvent final {
    DatasetId datasetId;
};

struct DatasetLoadCancelledEvent final {
    DatasetId datasetId;
};

struct FeatureDegradedEvent final {
    std::string feature;
    std::string reason;
};

using EngineEvent = std::variant<
    MessageEvent,
    ErrorEvent,
    DatasetProgressEvent,
    DatasetLoadedEvent,
    DatasetLoadCancelledEvent,
    FeatureDegradedEvent>;

} // namespace dzc