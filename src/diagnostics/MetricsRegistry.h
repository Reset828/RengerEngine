#pragma once

#include <cstdint>
#include <memory>
#include <optional>

namespace dzc::diagnostics {

struct PerformanceMetrics {
    double framesPerSecond = 0.0;
    double cpuFrameMilliseconds = 0.0;
    std::optional<double> gpuFrameMilliseconds;
};

struct GeometryMetrics {
    std::uint64_t visiblePoints = 0U;
    std::uint64_t submittedPoints = 0U;
    std::uint64_t visibleChunks = 0U;
    std::uint64_t drawnChunks = 0U;
};

struct TransferMetrics {
    std::uint64_t readerBytes = 0U;
    std::uint64_t cacheBytes = 0U;
    std::uint64_t uploadBytes = 0U;
};

struct MemoryMetrics {
    std::uint64_t cpuResidentBytes = 0U;
    std::uint64_t cpuBudgetBytes = 0U;
    std::uint64_t gpuResidentBytes = 0U;
    std::uint64_t gpuBudgetBytes = 0U;
};

struct LodMetrics {
    std::uint64_t requests = 0U;
    std::uint64_t hits = 0U;
    std::uint64_t ancestorFallbacks = 0U;
};

struct RuntimeMetrics {
    std::uint64_t taskQueueDepth = 0U;
    std::uint64_t ioActiveCount = 0U;
};

struct RecordingMetrics {
    std::uint64_t drawCount = 0U;
    double durationMilliseconds = 0.0;
    std::uint32_t workerCount = 0U;
};

struct ComputeMetrics {
    std::uint64_t processedPoints = 0U;
    double synchronizationMilliseconds = 0.0;
};

struct MetricsSnapshot {
    std::uint64_t frameId = 0U;
    PerformanceMetrics performance;
    GeometryMetrics geometry;
    TransferMetrics transfer;
    MemoryMetrics memory;
    LodMetrics lod;
    RuntimeMetrics runtime;
    RecordingMetrics recording;
    ComputeMetrics compute;
};

class MetricsRegistry final {
public:
    MetricsRegistry();
    ~MetricsRegistry();

    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;
    MetricsRegistry(MetricsRegistry&&) noexcept;
    MetricsRegistry& operator=(MetricsRegistry&&) noexcept;

    void beginFrame(std::uint64_t frameId);
    void reset();
    MetricsSnapshot snapshot() const;

    bool setFramesPerSecond(double value);
    bool setCpuFrameMilliseconds(double value);
    bool setGpuFrameMilliseconds(std::optional<double> value);

    void addVisiblePoints(std::uint64_t value);
    void addSubmittedPoints(std::uint64_t value);
    void addVisibleChunks(std::uint64_t value);
    void addDrawnChunks(std::uint64_t value);

    void addReaderBytes(std::uint64_t value);
    void addCacheBytes(std::uint64_t value);
    void addUploadBytes(std::uint64_t value);

    void setCpuResidentBytes(std::uint64_t value);
    void setCpuBudgetBytes(std::uint64_t value);
    void setGpuResidentBytes(std::uint64_t value);
    void setGpuBudgetBytes(std::uint64_t value);

    void addLodRequests(std::uint64_t value);
    void addLodHits(std::uint64_t value);
    void addAncestorFallbacks(std::uint64_t value);

    void setTaskQueueDepth(std::uint64_t value);
    void setIoActiveCount(std::uint64_t value);

    void addRecordingDrawCount(std::uint64_t value);
    bool addRecordingDurationMilliseconds(double value);
    void setRecordingWorkerCount(std::uint32_t value);

    void addProcessedPoints(std::uint64_t value);
    bool setSynchronizationMilliseconds(double value);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::diagnostics