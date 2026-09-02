#include "diagnostics/MetricsRegistry.h"

#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

namespace dzc::diagnostics {
namespace {

void saturatingAdd(std::uint64_t& target, std::uint64_t value) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (maximum - target < value) {
        target = maximum;
        return;
    }
    target += value;
}

bool isValidNonNegative(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

void saturatingAdd(double& target, double value) noexcept {
    const double maximum = std::numeric_limits<double>::max();
    if (maximum - target < value) {
        target = maximum;
        return;
    }
    target += value;
}

} // namespace

class MetricsRegistry::Impl final {
public:
    void beginFrame(std::uint64_t frameId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.frameId = frameId;
        clearFrameMetrics();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot = MetricsSnapshot{};
    }

    MetricsSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_snapshot;
    }

    bool setFramesPerSecond(double value) {
        return setNonNegative(m_snapshot.performance.framesPerSecond, value);
    }

    bool setCpuFrameMilliseconds(double value) {
        return setNonNegative(m_snapshot.performance.cpuFrameMilliseconds, value);
    }

    bool setGpuFrameMilliseconds(std::optional<double> value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!value.has_value()) {
            m_snapshot.performance.gpuFrameMilliseconds.reset();
            return true;
        }
        if (!isValidNonNegative(*value))
            return false;
        m_snapshot.performance.gpuFrameMilliseconds = *value;
        return true;
    }

    void addVisiblePoints(std::uint64_t value) {
        addInteger(m_snapshot.geometry.visiblePoints, value);
    }

    void addSubmittedPoints(std::uint64_t value) {
        addInteger(m_snapshot.geometry.submittedPoints, value);
    }

    void addVisibleChunks(std::uint64_t value) {
        addInteger(m_snapshot.geometry.visibleChunks, value);
    }

    void addDrawnChunks(std::uint64_t value) {
        addInteger(m_snapshot.geometry.drawnChunks, value);
    }

    void addReaderBytes(std::uint64_t value) {
        addInteger(m_snapshot.transfer.readerBytes, value);
    }

    void addCacheBytes(std::uint64_t value) {
        addInteger(m_snapshot.transfer.cacheBytes, value);
    }

    void addUploadBytes(std::uint64_t value) {
        addInteger(m_snapshot.transfer.uploadBytes, value);
    }

    void setCpuResidentBytes(std::uint64_t value) {
        setInteger(m_snapshot.memory.cpuResidentBytes, value);
    }

    void setCpuBudgetBytes(std::uint64_t value) {
        setInteger(m_snapshot.memory.cpuBudgetBytes, value);
    }

    void setGpuResidentBytes(std::uint64_t value) {
        setInteger(m_snapshot.memory.gpuResidentBytes, value);
    }

    void setGpuBudgetBytes(std::uint64_t value) {
        setInteger(m_snapshot.memory.gpuBudgetBytes, value);
    }

    void addLodRequests(std::uint64_t value) {
        addInteger(m_snapshot.lod.requests, value);
    }

    void addLodHits(std::uint64_t value) {
        addInteger(m_snapshot.lod.hits, value);
    }

    void addAncestorFallbacks(std::uint64_t value) {
        addInteger(m_snapshot.lod.ancestorFallbacks, value);
    }

    void setTaskQueueDepth(std::uint64_t value) {
        setInteger(m_snapshot.runtime.taskQueueDepth, value);
    }

    void setIoActiveCount(std::uint64_t value) {
        setInteger(m_snapshot.runtime.ioActiveCount, value);
    }

    void addRecordingDrawCount(std::uint64_t value) {
        addInteger(m_snapshot.recording.drawCount, value);
    }

    bool addRecordingDurationMilliseconds(double value) {
        if (!isValidNonNegative(value)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        saturatingAdd(m_snapshot.recording.durationMilliseconds, value);
        return true;
    }

    void setRecordingWorkerCount(std::uint32_t value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.recording.workerCount = value;
    }

    void addProcessedPoints(std::uint64_t value) {
        addInteger(m_snapshot.compute.processedPoints, value);
    }

    bool setSynchronizationMilliseconds(double value) {
        return setNonNegative(m_snapshot.compute.synchronizationMilliseconds, value);
    }

private:
    void clearFrameMetrics() noexcept {
        m_snapshot.performance = PerformanceMetrics{};
        m_snapshot.geometry = GeometryMetrics{};
        m_snapshot.transfer = TransferMetrics{};
        m_snapshot.lod = LodMetrics{};
        m_snapshot.recording = RecordingMetrics{};
        m_snapshot.compute = ComputeMetrics{};
    }

    template <typename Member>
    void addInteger(Member& member, std::uint64_t value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        saturatingAdd(member, value);
    }

    template <typename Member>
    void setInteger(Member& member, Member value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        member = value;
    }

    bool setNonNegative(double& member, double value) {
        if (!isValidNonNegative(value)) {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        member = value;
        return true;
    }

    mutable std::mutex m_mutex;
    MetricsSnapshot m_snapshot;
};

MetricsRegistry::MetricsRegistry() : m_impl(std::make_unique<Impl>()) {}

MetricsRegistry::~MetricsRegistry() = default;

MetricsRegistry::MetricsRegistry(MetricsRegistry&&) noexcept = default;

MetricsRegistry& MetricsRegistry::operator=(MetricsRegistry&&) noexcept = default;

void MetricsRegistry::beginFrame(std::uint64_t frameId) {
    m_impl->beginFrame(frameId);
}

void MetricsRegistry::reset() {
    m_impl->reset();
}

MetricsSnapshot MetricsRegistry::snapshot() const {
    return m_impl->snapshot();
}

bool MetricsRegistry::setFramesPerSecond(double value) {
    return m_impl->setFramesPerSecond(value);
}

bool MetricsRegistry::setCpuFrameMilliseconds(double value) {
    return m_impl->setCpuFrameMilliseconds(value);
}

bool MetricsRegistry::setGpuFrameMilliseconds(std::optional<double> value) {
    return m_impl->setGpuFrameMilliseconds(std::move(value));
}

void MetricsRegistry::addVisiblePoints(std::uint64_t value) {
    m_impl->addVisiblePoints(value);
}

void MetricsRegistry::addSubmittedPoints(std::uint64_t value) {
    m_impl->addSubmittedPoints(value);
}

void MetricsRegistry::addVisibleChunks(std::uint64_t value) {
    m_impl->addVisibleChunks(value);
}

void MetricsRegistry::addDrawnChunks(std::uint64_t value) {
    m_impl->addDrawnChunks(value);
}

void MetricsRegistry::addReaderBytes(std::uint64_t value) {
    m_impl->addReaderBytes(value);
}

void MetricsRegistry::addCacheBytes(std::uint64_t value) {
    m_impl->addCacheBytes(value);
}

void MetricsRegistry::addUploadBytes(std::uint64_t value) {
    m_impl->addUploadBytes(value);
}

void MetricsRegistry::setCpuResidentBytes(std::uint64_t value) {
    m_impl->setCpuResidentBytes(value);
}

void MetricsRegistry::setCpuBudgetBytes(std::uint64_t value) {
    m_impl->setCpuBudgetBytes(value);
}

void MetricsRegistry::setGpuResidentBytes(std::uint64_t value) {
    m_impl->setGpuResidentBytes(value);
}

void MetricsRegistry::setGpuBudgetBytes(std::uint64_t value) {
    m_impl->setGpuBudgetBytes(value);
}

void MetricsRegistry::addLodRequests(std::uint64_t value) {
    m_impl->addLodRequests(value);
}

void MetricsRegistry::addLodHits(std::uint64_t value) {
    m_impl->addLodHits(value);
}

void MetricsRegistry::addAncestorFallbacks(std::uint64_t value) {
    m_impl->addAncestorFallbacks(value);
}

void MetricsRegistry::setTaskQueueDepth(std::uint64_t value) {
    m_impl->setTaskQueueDepth(value);
}

void MetricsRegistry::setIoActiveCount(std::uint64_t value) {
    m_impl->setIoActiveCount(value);
}

void MetricsRegistry::addRecordingDrawCount(std::uint64_t value) {
    m_impl->addRecordingDrawCount(value);
}

bool MetricsRegistry::addRecordingDurationMilliseconds(double value) {
    return m_impl->addRecordingDurationMilliseconds(value);
}

void MetricsRegistry::setRecordingWorkerCount(std::uint32_t value) {
    m_impl->setRecordingWorkerCount(value);
}

void MetricsRegistry::addProcessedPoints(std::uint64_t value) {
    m_impl->addProcessedPoints(value);
}

bool MetricsRegistry::setSynchronizationMilliseconds(double value) {
    return m_impl->setSynchronizationMilliseconds(value);
}

} // namespace dzc::diagnostics