#pragma once

#include "dzc/EngineConfig.h"
#include "dzc/EngineState.h"
#include "dzc/EngineTypes.h"
#include "dzc/Error.h"

#include <cstdint>
#include <optional>
#include <string>

namespace dzc {

enum class DatasetState : std::uint8_t {
    None,
    Opening,
    Building,
    Ready,
    Cancelling,
    Error
};

struct DatasetSummary final {
    DatasetId id;
    DatasetState state{DatasetState::None};
    std::string displayName;
    std::uint64_t totalPointCount{0};
    std::uint64_t visiblePointCount{0};
    std::uint64_t chunkCount{0};
    std::uint64_t visibleChunkCount{0};
    double progress{0.0};
};

struct PerformanceSnapshot final {
    double framesPerSecond{0.0};
    double cpuFrameMilliseconds{0.0};
    std::optional<double> gpuFrameMilliseconds;
    std::uint64_t uploadedBytesThisFrame{0};
    std::uint32_t recordingWorkerCount{0};
};

struct MemorySnapshot final {
    std::uint64_t cpuResidentBytes{0};
    std::uint64_t cpuBudgetBytes{0};
    std::uint64_t gpuResidentBytes{0};
    std::uint64_t gpuBudgetBytes{0};
};

struct EngineSnapshot final {
    FrameId frameId;
    EngineState state{EngineState::Created};
    RenderBackendType backend{RenderBackendType::OpenGL};
    bool cudaAvailable{false};
    bool cudaEnabled{false};
    DatasetSummary dataset;
    PerformanceSnapshot performance;
    MemorySnapshot memory;
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    ColorRgba backgroundColor;
    std::optional<Error> mostRecentError;
};

} // namespace dzc