#include "tasks/BackpressureController.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using dzc::tasks::BackpressureController;
using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::TaskPriority;
using dzc::tasks::TaskSystem;

constexpr auto WaitTimeout = std::chrono::seconds(2);

bool waitUntil(const std::atomic_bool& condition) {
    const auto deadline = std::chrono::steady_clock::now() + WaitTimeout;
    while (!condition.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void testInvalidConstruction() {
    const auto expectInvalid = [](std::size_t capacity,
                                  std::size_t highPercent,
                                  std::size_t lowPercent) {
        bool threw = false;
        try {
            BackpressureController invalid(capacity, highPercent, lowPercent);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    };

    expectInvalid(0U, 80U, 60U);
    expectInvalid(10U, 0U, 0U);
    expectInvalid(10U, 101U, 60U);
    expectInvalid(10U, 80U, 80U);
    expectInvalid(10U, 80U, 90U);
}

void testDefaultAndCustomWatermarks() {
    BackpressureController defaultController(10U);
    assert(defaultController.waitUntilResumed());

    defaultController.updateUsage(8U);
    std::atomic_bool defaultStarted{false};
    std::atomic_bool defaultFinished{false};
    std::atomic_bool defaultResumed{false};
    std::thread defaultWaiter([&] {
        defaultStarted.store(true, std::memory_order_release);
        defaultResumed.store(
            defaultController.waitUntilResumed(),
            std::memory_order_release);
        defaultFinished.store(true, std::memory_order_release);
    });

    assert(waitUntil(defaultStarted));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!defaultFinished.load(std::memory_order_acquire));
    defaultController.updateUsage(7U);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!defaultFinished.load(std::memory_order_acquire));
    defaultController.updateUsage(6U);
    assert(waitUntil(defaultFinished));
    defaultWaiter.join();
    assert(defaultResumed.load(std::memory_order_acquire));

    BackpressureController customController(7U, 50U, 30U);
    customController.updateUsage(3U);
    assert(customController.waitUntilResumed());
    customController.updateUsage(4U);

    std::atomic_bool customFinished{false};
    std::thread customWaiter([&] {
        assert(customController.waitUntilResumed());
        customFinished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!customFinished.load(std::memory_order_acquire));
    customController.updateUsage(3U);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!customFinished.load(std::memory_order_acquire));
    customController.updateUsage(2U);
    assert(waitUntil(customFinished));
    customWaiter.join();
}

void testRoundingBoundariesAndOverload() {
    BackpressureController controller(3U);
    controller.updateUsage(2U);
    assert(controller.waitUntilResumed());
    controller.updateUsage(3U);

    std::atomic_bool finished{false};
    std::thread waiter([&] {
        assert(controller.waitUntilResumed());
        finished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!finished.load(std::memory_order_acquire));
    controller.updateUsage(2U);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!finished.load(std::memory_order_acquire));
    controller.updateUsage(1U);
    assert(waitUntil(finished));
    waiter.join();

    BackpressureController overloaded(10U);
    overloaded.updateUsage(11U);
    CancellationSource cancellation;
    std::atomic_bool overloadedFinished{false};
    std::atomic_bool overloadedResult{true};
    std::thread overloadedWaiter([&] {
        overloadedResult.store(
            overloaded.waitUntilResumed(cancellation.token()),
            std::memory_order_release);
        overloadedFinished.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!overloadedFinished.load(std::memory_order_acquire));
    assert(cancellation.requestCancellation());
    assert(waitUntil(overloadedFinished));
    overloadedWaiter.join();
    assert(!overloadedResult.load(std::memory_order_acquire));

    BackpressureController maximum(
        std::numeric_limits<std::size_t>::max(), 100U, 99U);
    maximum.updateUsage(std::numeric_limits<std::size_t>::max());
    CancellationSource maximumCancellation;
    std::thread maximumWaiter([&] {
        assert(!maximum.waitUntilResumed(maximumCancellation.token()));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(maximumCancellation.requestCancellation());
    maximumWaiter.join();
}

void testCancellationAndCombinedTokens() {
    BackpressureController preCancelledController(10U);
    preCancelledController.updateUsage(8U);
    CancellationSource preCancelled;
    assert(preCancelled.requestCancellation());
    assert(!preCancelledController.waitUntilResumed(preCancelled.token()));

    BackpressureController externalController(10U);
    externalController.updateUsage(8U);
    CancellationSource externalSource;
    TaskSystem externalTaskSystem(1U, 4U, 4U);
    std::atomic_bool externalStarted{false};
    std::atomic_bool externalFinished{false};
    std::atomic_bool externalResumed{true};
    assert(externalTaskSystem.submit(
               TaskPriority::Normal,
               externalSource.token(),
               [&](CancellationToken token) {
                   externalStarted.store(true, std::memory_order_release);
                   externalResumed.store(
                       externalController.waitUntilResumed(token),
                       std::memory_order_release);
                   externalFinished.store(true, std::memory_order_release);
               })
               .hasValue());
    assert(waitUntil(externalStarted));
    assert(externalSource.requestCancellation());
    assert(waitUntil(externalFinished));
    externalTaskSystem.waitForCompletion();
    assert(!externalResumed.load(std::memory_order_acquire));

    BackpressureController internalController(10U);
    internalController.updateUsage(8U);
    TaskSystem internalTaskSystem(1U, 4U, 4U);
    std::atomic_bool internalStarted{false};
    std::atomic_bool internalFinished{false};
    std::atomic_bool internalResumed{true};
    assert(internalTaskSystem.submit(
               TaskPriority::Normal,
               {},
               [&](CancellationToken token) {
                   internalStarted.store(true, std::memory_order_release);
                   internalResumed.store(
                       internalController.waitUntilResumed(token),
                       std::memory_order_release);
                   internalFinished.store(true, std::memory_order_release);
               })
               .hasValue());
    assert(waitUntil(internalStarted));
    internalTaskSystem.requestCancelAll();
    assert(waitUntil(internalFinished));
    internalTaskSystem.waitForCompletion();
    assert(!internalResumed.load(std::memory_order_acquire));
}

void testCloseAndConcurrentStress() {
    BackpressureController controller(10U);
    controller.updateUsage(8U);
    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool resumed{true};
    std::thread waiter([&] {
        started.store(true, std::memory_order_release);
        resumed.store(controller.waitUntilResumed(), std::memory_order_release);
        finished.store(true, std::memory_order_release);
    });

    assert(waitUntil(started));
    controller.close();
    controller.close();
    assert(waitUntil(finished));
    waiter.join();
    assert(!resumed.load(std::memory_order_acquire));
    controller.updateUsage(0U);
    assert(!controller.waitUntilResumed());

    BackpressureController stressController(10U);
    stressController.updateUsage(8U);
    CancellationSource stressCancellation;
    std::atomic_bool go{false};
    std::atomic_uint32_t waitersFinished{0U};
    std::vector<std::thread> threads;
    for (std::size_t index = 0U; index < 8U; ++index) {
        threads.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const bool didResume =
                stressController.waitUntilResumed(stressCancellation.token());
            assert(!didResume);
            waitersFinished.fetch_add(1U, std::memory_order_acq_rel);
        });
    }

    std::thread updaterOne([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t index = 0U; index < 1000U; ++index) {
            stressController.updateUsage((index % 2U == 0U) ? 8U : 7U);
        }
    });
    std::thread updaterTwo([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::size_t index = 0U; index < 1000U; ++index) {
            stressController.updateUsage(10U);
        }
    });

    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(stressCancellation.requestCancellation());
    stressController.close();

    for (std::thread& thread : threads) {
        thread.join();
    }
    updaterOne.join();
    updaterTwo.join();
    assert(waitersFinished.load(std::memory_order_acquire) == 8U);
}

} // namespace

int main() {
    testInvalidConstruction();
    testDefaultAndCustomWatermarks();
    testRoundingBoundariesAndOverload();
    testCancellationAndCombinedTokens();
    testCloseAndConcurrentStress();
    return 0;
}