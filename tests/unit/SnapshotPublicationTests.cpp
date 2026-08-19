#include "dzc/Engine.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

void assertSuccess(const dzc::Result<void>& result) {
    assert(result.hasValue());
}

void testDefaultAndLifecycleSnapshots() {
    dzc::Engine engine;

    const auto created = engine.getSnapshot();
    assert(created != nullptr);
    assert(created->state == dzc::EngineState::Created);
    assert(created->frameId.value == 0U);

    assertSuccess(engine.init(dzc::EngineConfig{}));
    const auto ready = engine.getSnapshot();
    assert(ready != nullptr);
    assert(ready->state == dzc::EngineState::Ready);
    assert(ready->frameId.value == 0U);

    assertSuccess(engine.update(dzc::FrameInput{}));
    const auto running = engine.getSnapshot();
    assert(running != nullptr);
    assert(running->state == dzc::EngineState::Running);
    assert(running->frameId.value == 1U);

    engine.shutdown();
    const auto stopped = engine.getSnapshot();
    assert(stopped != nullptr);
    assert(stopped->state == dzc::EngineState::Stopped);
    assert(stopped->frameId.value == running->frameId.value);
}

void testPublishedSnapshotsRemainImmutable() {
    dzc::Engine engine;
    assertSuccess(engine.init(dzc::EngineConfig{}));

    const auto ready = engine.getSnapshot();
    assert(ready != nullptr);
    const dzc::FrameId readyFrame = ready->frameId;
    const dzc::EngineState readyState = ready->state;

    assertSuccess(engine.update(dzc::FrameInput{}));
    const auto running = engine.getSnapshot();

    assert(running != nullptr);
    assert(running != ready);
    assert(running->state == dzc::EngineState::Running);
    assert(running->frameId.value > readyFrame.value);
    assert(ready->state == readyState);
    assert(ready->frameId == readyFrame);
}

void testConcurrentPublicationAndReads() {
    dzc::Engine engine;
    assertSuccess(engine.init(dzc::EngineConfig{}));

    constexpr std::size_t kReaderCount = 4U;
    constexpr std::uint32_t kUpdateCount = 2048U;

    std::atomic_bool start{false};
    std::atomic_bool finished{false};
    std::atomic_bool valid{true};
    std::atomic_uint32_t readersReady{0U};
    std::vector<std::thread> readers;
    readers.reserve(kReaderCount);

    for (std::size_t index = 0U; index < kReaderCount; ++index) {
        readers.emplace_back([&engine, &start, &finished, &valid, &readersReady]() {
            readersReady.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            std::uint64_t previousFrame = 0U;
            while (!finished.load(std::memory_order_acquire)) {
                const auto snapshot = engine.getSnapshot();
                if (snapshot == nullptr || snapshot->frameId.value < previousFrame) {
                    valid.store(false, std::memory_order_release);
                    return;
                }
                previousFrame = snapshot->frameId.value;

                const dzc::EngineState state = snapshot->state;
                if (state != dzc::EngineState::Ready && state != dzc::EngineState::Running) {
                    valid.store(false, std::memory_order_release);
                    return;
                }
            }

            const auto finalSnapshot = engine.getSnapshot();
            if (finalSnapshot == nullptr || finalSnapshot->frameId.value < previousFrame) {
                valid.store(false, std::memory_order_release);
            }
        });
    }

    while (readersReady.load(std::memory_order_acquire) != kReaderCount) {
        std::this_thread::yield();
    }

    std::thread producer([&engine, &start, &finished, &valid, kUpdateCount]() {
        start.store(true, std::memory_order_release);
        for (std::uint32_t index = 0U; index < kUpdateCount; ++index) {
            if (!engine.update(dzc::FrameInput{}).hasValue()) {
                valid.store(false, std::memory_order_release);
                break;
            }
            if ((index % 16U) == 0U) {
                std::this_thread::yield();
            }
        }
        finished.store(true, std::memory_order_release);
    });

    producer.join();
    for (std::thread& reader : readers) {
        reader.join();
    }

    assert(valid.load(std::memory_order_acquire));
    const auto finalSnapshot = engine.getSnapshot();
    assert(finalSnapshot != nullptr);
    assert(finalSnapshot->state == dzc::EngineState::Running);
    assert(finalSnapshot->frameId.value == kUpdateCount);
}

} // namespace

int main() {
    testDefaultAndLifecycleSnapshots();
    testPublishedSnapshotsRemainImmutable();
    testConcurrentPublicationAndReads();
    return 0;
}
