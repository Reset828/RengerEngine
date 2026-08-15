#include <diagnostics/FrameStatistics.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

namespace {

using dzc::diagnostics::FrameStatistics;
using dzc::diagnostics::IClock;
using ClockPoint = std::chrono::steady_clock::time_point;

class TestClock final : public IClock {
public:
    explicit TestClock(ClockPoint now = ClockPoint{})
        : m_now(now) {}

    ClockPoint now() const noexcept override {
        std::lock_guard lock(m_mutex);
        return m_now;
    }

    void set(ClockPoint now) {
        std::lock_guard lock(m_mutex);
        m_now = now;
    }

    void advance(std::chrono::milliseconds amount) {
        std::lock_guard lock(m_mutex);
        m_now += amount;
    }

private:
    mutable std::mutex m_mutex;
    ClockPoint m_now;
};

void assertNear(double actual, double expected, double tolerance = 1.0e-9) {
    assert(actual >= expected - tolerance);
    assert(actual <= expected + tolerance);
}

void testFixedTimeSequence() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock);

    assert(statistics.addFrame(std::chrono::milliseconds(100)));
    clock->advance(std::chrono::milliseconds(100));
    assert(statistics.addFrame(std::chrono::milliseconds(100)));
    clock->advance(std::chrono::milliseconds(100));
    assert(statistics.addFrame(std::chrono::milliseconds(100)));

    const auto result = statistics.snapshot();
    assert(result.frameWindow.sampleCount == 3U);
    assertNear(result.frameWindow.fps, 15.0);
    assertNear(result.frameWindow.averageFrameTimeMs, 100.0);
    assert(result.timeWindow.sampleCount == 3U);
    assertNear(result.timeWindow.fps, 15.0);
    assertNear(result.timeWindow.averageFrameTimeMs, 100.0);
}

void testFrameWindowBoundary() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock, 2U, std::chrono::seconds(10));

    assert(statistics.addFrame(std::chrono::milliseconds(50)));
    clock->advance(std::chrono::milliseconds(100));
    assert(statistics.addFrame(std::chrono::milliseconds(100)));
    clock->advance(std::chrono::milliseconds(100));
    assert(statistics.addFrame(std::chrono::milliseconds(150)));

    const auto result = statistics.snapshot();
    assert(result.frameWindow.sampleCount == 2U);
    assertNear(result.frameWindow.fps, 20.0);
    assertNear(result.frameWindow.averageFrameTimeMs, 125.0);
    assert(result.timeWindow.sampleCount == 3U);
}

void testTimeWindowIncludesBoundary() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock, 120U, std::chrono::seconds(1));

    assert(statistics.addFrame(std::chrono::milliseconds(10)));
    clock->advance(std::chrono::seconds(1));
    assert(statistics.addFrame(std::chrono::milliseconds(20)));

    auto result = statistics.snapshot();
    assert(result.timeWindow.sampleCount == 2U);

    clock->advance(std::chrono::milliseconds(1));
    result = statistics.snapshot();
    assert(result.timeWindow.sampleCount == 1U);
    assertNear(result.timeWindow.averageFrameTimeMs, 20.0);
}

void testSnapshotExpiresTimeWindowWithoutNewFrame() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock, 120U, std::chrono::seconds(1));

    assert(statistics.addFrame(std::chrono::milliseconds(16)));
    clock->advance(std::chrono::milliseconds(1001));

    const auto result = statistics.snapshot();
    assert(result.frameWindow.sampleCount == 1U);
    assert(result.timeWindow.sampleCount == 0U);
    assert(result.timeWindow.fps == 0.0);
    assert(result.timeWindow.averageFrameTimeMs == 0.0);
}

void testZeroSampleIsSafe() {
    FrameStatistics statistics;
    const auto result = statistics.snapshot();
    assert(result.frameWindow.sampleCount == 0U);
    assert(result.frameWindow.fps == 0.0);
    assert(result.frameWindow.averageFrameTimeMs == 0.0);
    assert(result.timeWindow.sampleCount == 0U);
    assert(!statistics.addFrame(std::chrono::nanoseconds::zero()));
}

void testInvalidDeltaIsRejected() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock);

    assert(!statistics.addFrame(std::chrono::nanoseconds(-1)));
    assert(!statistics.addFrame(std::chrono::nanoseconds::zero()));
    assert(statistics.snapshot().frameWindow.sampleCount == 0U);
    assert(statistics.addFrame(std::chrono::milliseconds(1)));
}

void testClockRollbackIsRejectedAndEqualTimeIsAccepted() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock);

    assert(statistics.addFrame(std::chrono::milliseconds(10)));
    clock->set(ClockPoint{} - std::chrono::milliseconds(1));
    assert(!statistics.addFrame(std::chrono::milliseconds(10)));
    clock->set(ClockPoint{});
    assert(statistics.addFrame(std::chrono::milliseconds(20)));
    assert(statistics.snapshot().frameWindow.sampleCount == 2U);
}

void testInvalidWindowsAreIndependent() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics noFrameWindow(clock, 0U, std::chrono::seconds(1));
    assert(noFrameWindow.addFrame(std::chrono::milliseconds(10)));
    auto result = noFrameWindow.snapshot();
    assert(result.frameWindow.sampleCount == 0U);
    assert(result.timeWindow.sampleCount == 1U);

    FrameStatistics noTimeWindow(clock, 2U, std::chrono::milliseconds(0));
    assert(noTimeWindow.addFrame(std::chrono::milliseconds(10)));
    result = noTimeWindow.snapshot();
    assert(result.frameWindow.sampleCount == 1U);
    assert(result.timeWindow.sampleCount == 0U);

    FrameStatistics noWindows(clock, 0U, std::chrono::milliseconds(0));
    assert(!noWindows.addFrame(std::chrono::milliseconds(10)));
}

void testResetClearsSamplesAndClockBaseline() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock);

    assert(statistics.addFrame(std::chrono::milliseconds(10)));
    statistics.reset();
    clock->set(ClockPoint{} - std::chrono::seconds(1));
    assert(statistics.addFrame(std::chrono::milliseconds(20)));

    const auto result = statistics.snapshot();
    assert(result.frameWindow.sampleCount == 1U);
    assertNear(result.frameWindow.averageFrameTimeMs, 20.0);
}

void testConcurrentOperationsAreSafe() {
    auto clock = std::make_shared<TestClock>();
    FrameStatistics statistics(clock, 120U, std::chrono::seconds(1));
    std::atomic_bool failed{false};

    std::thread writer([&] {
        for (int index = 0; index < 2000; ++index) {
            if (!statistics.addFrame(std::chrono::milliseconds(1))) {
                failed.store(true);
            }
        }
    });
    std::thread reader([&] {
        for (int index = 0; index < 2000; ++index) {
            const auto result = statistics.snapshot();
            if (result.frameWindow.sampleCount > 120U ||
                result.timeWindow.sampleCount > 2000U) {
                failed.store(true);
            }
        }
    });
    std::thread resetter([&] {
        for (int index = 0; index < 20; ++index) {
            statistics.reset();
        }
    });

    writer.join();
    reader.join();
    resetter.join();
    assert(!failed.load());
}

} // namespace

int main() {
    testFixedTimeSequence();
    testFrameWindowBoundary();
    testTimeWindowIncludesBoundary();
    testSnapshotExpiresTimeWindowWithoutNewFrame();
    testZeroSampleIsSafe();
    testInvalidDeltaIsRejected();
    testClockRollbackIsRejectedAndEqualTimeIsAccepted();
    testInvalidWindowsAreIndependent();
    testResetClearsSamplesAndClockBaseline();
    testConcurrentOperationsAreSafe();
    return 0;
}
