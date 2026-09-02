#include "diagnostics/MetricsRegistry.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <atomic>
#include <thread>

namespace {

using dzc::diagnostics::MetricsRegistry;
using dzc::diagnostics::MetricsSnapshot;

void assertNear(double actual, double expected, double tolerance = 1.0e-12) {
    assert(std::fabs(actual - expected) <= tolerance);
}

void assertSnapshotIsZero(const MetricsSnapshot& snapshot) {
    assert(snapshot.frameId == 0U);
    assert(snapshot.performance.framesPerSecond == 0.0);
    assert(snapshot.performance.cpuFrameMilliseconds == 0.0);
    assert(!snapshot.performance.gpuFrameMilliseconds.has_value());
    assert(snapshot.geometry.visiblePoints == 0U);
    assert(snapshot.geometry.submittedPoints == 0U);
    assert(snapshot.geometry.visibleChunks == 0U);
    assert(snapshot.geometry.drawnChunks == 0U);
    assert(snapshot.transfer.readerBytes == 0U);
    assert(snapshot.transfer.cacheBytes == 0U);
    assert(snapshot.transfer.uploadBytes == 0U);
    assert(snapshot.memory.cpuResidentBytes == 0U);
    assert(snapshot.memory.cpuBudgetBytes == 0U);
    assert(snapshot.memory.gpuResidentBytes == 0U);
    assert(snapshot.memory.gpuBudgetBytes == 0U);
    assert(snapshot.lod.requests == 0U);
    assert(snapshot.lod.hits == 0U);
    assert(snapshot.lod.ancestorFallbacks == 0U);
    assert(snapshot.runtime.taskQueueDepth == 0U);
    assert(snapshot.runtime.ioActiveCount == 0U);
    assert(snapshot.recording.drawCount == 0U);
    assert(snapshot.recording.durationMilliseconds == 0.0);
    assert(snapshot.recording.workerCount == 0U);
    assert(snapshot.compute.processedPoints == 0U);
    assert(snapshot.compute.synchronizationMilliseconds == 0.0);
}

void testInitialSnapshotAndUpdates() {
    MetricsRegistry registry;
    assertSnapshotIsZero(registry.snapshot());

    registry.beginFrame(42U);
    assert(registry.setFramesPerSecond(60.0));
    assert(registry.setCpuFrameMilliseconds(10.5));
    assert(registry.setGpuFrameMilliseconds(4.25));
    registry.addVisiblePoints(10U);
    registry.addVisiblePoints(5U);
    registry.addSubmittedPoints(20U);
    registry.addVisibleChunks(3U);
    registry.addDrawnChunks(2U);
    registry.addReaderBytes(100U);
    registry.addCacheBytes(200U);
    registry.addUploadBytes(300U);
    registry.setCpuResidentBytes(400U);
    registry.setCpuBudgetBytes(500U);
    registry.setGpuResidentBytes(600U);
    registry.setGpuBudgetBytes(700U);
    registry.addLodRequests(8U);
    registry.addLodHits(6U);
    registry.addAncestorFallbacks(2U);
    registry.setTaskQueueDepth(9U);
    registry.setIoActiveCount(4U);
    registry.addRecordingDrawCount(11U);
    assert(registry.addRecordingDurationMilliseconds(1.5));
    assert(registry.addRecordingDurationMilliseconds(2.5));
    registry.setRecordingWorkerCount(3U);
    registry.addProcessedPoints(12U);
    assert(registry.setSynchronizationMilliseconds(0.75));

    const auto snapshot = registry.snapshot();
    assert(snapshot.frameId == 42U);
    assertNear(snapshot.performance.framesPerSecond, 60.0);
    assertNear(snapshot.performance.cpuFrameMilliseconds, 10.5);
    assert(snapshot.performance.gpuFrameMilliseconds.has_value());
    assertNear(*snapshot.performance.gpuFrameMilliseconds, 4.25);
    assert(snapshot.geometry.visiblePoints == 15U);
    assert(snapshot.geometry.submittedPoints == 20U);
    assert(snapshot.geometry.visibleChunks == 3U);
    assert(snapshot.geometry.drawnChunks == 2U);
    assert(snapshot.transfer.readerBytes == 100U);
    assert(snapshot.transfer.cacheBytes == 200U);
    assert(snapshot.transfer.uploadBytes == 300U);
    assert(snapshot.memory.cpuResidentBytes == 400U);
    assert(snapshot.memory.cpuBudgetBytes == 500U);
    assert(snapshot.memory.gpuResidentBytes == 600U);
    assert(snapshot.memory.gpuBudgetBytes == 700U);
    assert(snapshot.lod.requests == 8U);
    assert(snapshot.lod.hits == 6U);
    assert(snapshot.lod.ancestorFallbacks == 2U);
    assert(snapshot.runtime.taskQueueDepth == 9U);
    assert(snapshot.runtime.ioActiveCount == 4U);
    assert(snapshot.recording.drawCount == 11U);
    assertNear(snapshot.recording.durationMilliseconds, 4.0);
    assert(snapshot.recording.workerCount == 3U);
    assert(snapshot.compute.processedPoints == 12U);
    assertNear(snapshot.compute.synchronizationMilliseconds, 0.75);
}

void testBeginFrameClearsFrameMetricsAndPreservesState() {
    MetricsRegistry registry;
    registry.setCpuResidentBytes(1U);
    registry.setCpuBudgetBytes(2U);
    registry.setGpuResidentBytes(3U);
    registry.setGpuBudgetBytes(4U);
    registry.setTaskQueueDepth(5U);
    registry.setIoActiveCount(6U);
    registry.beginFrame(1U);
    assert(registry.setFramesPerSecond(60.0));
    registry.addVisiblePoints(7U);
    registry.addReaderBytes(8U);
    registry.addLodRequests(9U);
    registry.addRecordingDrawCount(10U);
    assert(registry.addRecordingDurationMilliseconds(11.0));
    registry.setRecordingWorkerCount(12U);
    registry.addProcessedPoints(13U);
    assert(registry.setSynchronizationMilliseconds(14.0));

    registry.beginFrame(2U);
    const auto snapshot = registry.snapshot();
    assert(snapshot.frameId == 2U);
    assert(snapshot.performance.framesPerSecond == 0.0);
    assert(snapshot.geometry.visiblePoints == 0U);
    assert(snapshot.transfer.readerBytes == 0U);
    assert(snapshot.lod.requests == 0U);
    assert(snapshot.recording.drawCount == 0U);
    assert(snapshot.recording.durationMilliseconds == 0.0);
    assert(snapshot.recording.workerCount == 0U);
    assert(snapshot.compute.processedPoints == 0U);
    assert(snapshot.compute.synchronizationMilliseconds == 0.0);
    assert(snapshot.memory.cpuResidentBytes == 1U);
    assert(snapshot.memory.cpuBudgetBytes == 2U);
    assert(snapshot.memory.gpuResidentBytes == 3U);
    assert(snapshot.memory.gpuBudgetBytes == 4U);
    assert(snapshot.runtime.taskQueueDepth == 5U);
    assert(snapshot.runtime.ioActiveCount == 6U);
}

void testReset() {
    MetricsRegistry registry;
    registry.beginFrame(9U);
    registry.addVisiblePoints(1U);
    registry.setGpuBudgetBytes(2U);
    assert(registry.setFramesPerSecond(3.0));
    registry.reset();
    assertSnapshotIsZero(registry.snapshot());
}

void testIntegerSaturation() {
    MetricsRegistry registry;
    registry.addVisiblePoints(std::numeric_limits<std::uint64_t>::max());
    registry.addVisiblePoints(1U);
    registry.addRecordingDrawCount(std::numeric_limits<std::uint64_t>::max());
    registry.addRecordingDrawCount(1U);
    const auto snapshot = registry.snapshot();
    assert(snapshot.geometry.visiblePoints == std::numeric_limits<std::uint64_t>::max());
    assert(snapshot.recording.drawCount == std::numeric_limits<std::uint64_t>::max());
}

void testDoubleValidationAndSaturation() {
    MetricsRegistry registry;
    assert(registry.setFramesPerSecond(60.0));
    assert(!registry.setFramesPerSecond(-1.0));
    assert(!registry.setFramesPerSecond(std::numeric_limits<double>::quiet_NaN()));
    assert(!registry.setFramesPerSecond(std::numeric_limits<double>::infinity()));
    assert(!registry.setFramesPerSecond(-std::numeric_limits<double>::infinity()));
    assertNear(registry.snapshot().performance.framesPerSecond, 60.0);

    assert(registry.addRecordingDurationMilliseconds(std::numeric_limits<double>::max()));
    assert(registry.addRecordingDurationMilliseconds(1.0));
    assert(registry.snapshot().recording.durationMilliseconds ==
           std::numeric_limits<double>::max());

    assert(registry.setSynchronizationMilliseconds(2.0));
    assert(!registry.setSynchronizationMilliseconds(-0.1));
    assert(!registry.setSynchronizationMilliseconds(std::numeric_limits<double>::quiet_NaN()));
    assert(!registry.setSynchronizationMilliseconds(std::numeric_limits<double>::infinity()));
    assertNear(registry.snapshot().compute.synchronizationMilliseconds, 2.0);
}

void testGpuMetricOptionalValidation() {
    MetricsRegistry registry;
    assert(registry.setGpuFrameMilliseconds(3.0));
    assert(!registry.setGpuFrameMilliseconds(-1.0));
    assert(!registry.setGpuFrameMilliseconds(std::numeric_limits<double>::quiet_NaN()));
    assert(!registry.setGpuFrameMilliseconds(std::numeric_limits<double>::infinity()));
    assert(!registry.setGpuFrameMilliseconds(-std::numeric_limits<double>::infinity()));
    assert(registry.snapshot().performance.gpuFrameMilliseconds.has_value());
    assertNear(*registry.snapshot().performance.gpuFrameMilliseconds, 3.0);
    assert(registry.setGpuFrameMilliseconds(std::nullopt));
    assert(!registry.snapshot().performance.gpuFrameMilliseconds.has_value());
}
void testSnapshotIsValueCopy() {
    MetricsRegistry registry;
    registry.addVisiblePoints(10U);
    auto first = registry.snapshot();
    registry.addVisiblePoints(5U);
    const auto second = registry.snapshot();
    assert(first.geometry.visiblePoints == 10U);
    assert(second.geometry.visiblePoints == 15U);
}

void testConcurrentOperationsAreSafe() {
    MetricsRegistry registry;
    std::atomic_bool failed{false};

    std::thread producer([&] {
        for (std::uint64_t index = 0U; index < 5000U; ++index) {
            registry.addVisiblePoints(1U);
            registry.addSubmittedPoints(1U);
            registry.addReaderBytes(1U);
            registry.addLodRequests(1U);
            registry.addRecordingDrawCount(1U);
            registry.addProcessedPoints(1U);
            if (!registry.addRecordingDurationMilliseconds(0.1)) {
                failed = true;
            }
        }
    });

    std::thread stateWriter([&] {
        for (std::uint64_t index = 0U; index < 5000U; ++index) {
            registry.setCpuResidentBytes(index);
            registry.setGpuResidentBytes(index);
            registry.setTaskQueueDepth(index);
            registry.setIoActiveCount(index);
            if (!registry.setFramesPerSecond(60.0) ||
                !registry.setCpuFrameMilliseconds(1.0) ||
                !registry.setGpuFrameMilliseconds(2.0) ||
                !registry.setSynchronizationMilliseconds(0.5)) {
                failed = true;
            }
        }
    });

    std::thread frameController([&] {
        for (std::uint64_t index = 0U; index < 100U; ++index) {
            registry.beginFrame(index);
            if ((index % 10U) == 0U) {
                registry.reset();
            }
        }
    });

    std::thread reader([&] {
        for (std::uint64_t index = 0U; index < 5000U; ++index) {
            const auto snapshot = registry.snapshot();
            if (!std::isfinite(snapshot.performance.framesPerSecond) ||
                !std::isfinite(snapshot.recording.durationMilliseconds) ||
                !std::isfinite(snapshot.compute.synchronizationMilliseconds)) {
                failed = true;
            }
        }
    });

    producer.join();
    stateWriter.join();
    frameController.join();
    reader.join();
    assert(!failed);
}

} // namespace

int main() {
    testInitialSnapshotAndUpdates();
    testBeginFrameClearsFrameMetricsAndPreservesState();
    testReset();
    testIntegerSaturation();
    testDoubleValidationAndSaturation();
    testGpuMetricOptionalValidation();
    testSnapshotIsValueCopy();
    testConcurrentOperationsAreSafe();
    return 0;
}