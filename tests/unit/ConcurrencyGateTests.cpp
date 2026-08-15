#include "tasks/ConcurrencyGate.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::ConcurrencyGate;
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

void acquireThenThrow(ConcurrencyGate& gate) {
    auto lease = gate.acquire();
    assert(lease.has_value());
    throw std::runtime_error("test exception");
}

void testDefaultAndCustomCapacity() {
    ConcurrencyGate defaultGate;
    auto first = defaultGate.acquire();
    auto second = defaultGate.acquire();
    assert(first.has_value());
    assert(second.has_value());

    std::atomic_bool waiterStarted{false};
    std::atomic_bool waiterFinished{false};
    std::atomic_bool waiterAcquired{false};
    std::thread waiter([&] {
        waiterStarted.store(true, std::memory_order_release);
        auto third = defaultGate.acquire();
        waiterAcquired.store(third.has_value(), std::memory_order_release);
        waiterFinished.store(true, std::memory_order_release);
    });

    assert(waitUntil(waiterStarted));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!waiterFinished.load(std::memory_order_acquire));

    first.reset();
    assert(waitUntil(waiterFinished));
    waiter.join();
    assert(waiterAcquired.load(std::memory_order_acquire));
    second.reset();

    ConcurrencyGate singleGate(1U);
    auto onlyLease = singleGate.acquire();
    assert(onlyLease.has_value());
    CancellationSource cancellation;
    cancellation.requestCancellation();
    assert(!singleGate.acquire(cancellation.token()).has_value());
}

void testZeroCapacityThrows() {
    bool threw = false;
    try {
        ConcurrencyGate invalidGate(0U);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
}

void testLeaseReleaseMoveAndExceptionUnwind() {
    ConcurrencyGate gate(2U);
    auto first = gate.acquire();
    auto second = gate.acquire();
    assert(first.has_value());
    assert(second.has_value());

    *first = std::move(*second);
    auto releasedByMoveAssignment = gate.acquire();
    assert(releasedByMoveAssignment.has_value());
    releasedByMoveAssignment.reset();

    {
        ConcurrencyGate::Lease movedLease(std::move(*first));
        auto releasedByMoveConstruction = gate.acquire();
        assert(releasedByMoveConstruction.has_value());
        releasedByMoveConstruction.reset();
    }

    bool threw = false;
    try {
        acquireThenThrow(gate);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    auto afterUnwind = gate.acquire();
    assert(afterUnwind.has_value());

    std::optional<ConcurrencyGate::Lease> lateLease;
    {
        ConcurrencyGate lateReleaseGate(1U);
        lateLease = lateReleaseGate.acquire();
        assert(lateLease.has_value());
    }
    lateLease.reset();
}

void testDefaultTokenWaitsForRelease() {
    ConcurrencyGate gate(1U);
    auto heldLease = gate.acquire();
    assert(heldLease.has_value());

    std::atomic_bool started{false};
    std::atomic_bool acquired{false};
    std::thread waiter([&] {
        started.store(true, std::memory_order_release);
        auto lease = gate.acquire();
        acquired.store(lease.has_value(), std::memory_order_release);
    });

    assert(waitUntil(started));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!acquired.load(std::memory_order_acquire));
    heldLease.reset();
    waiter.join();
    assert(acquired.load(std::memory_order_acquire));
}

void testCancellationWakesWaitingAcquire() {
    ConcurrencyGate gate(1U);
    auto heldLease = gate.acquire();
    assert(heldLease.has_value());

    CancellationSource source;
    CancellationSource alreadyCancelled;
    assert(alreadyCancelled.requestCancellation());
    assert(!gate.acquire(alreadyCancelled.token()).has_value());

    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool acquired{true};
    std::thread waiter([&] {
        started.store(true, std::memory_order_release);
        auto lease = gate.acquire(source.token());
        acquired.store(lease.has_value(), std::memory_order_release);
        finished.store(true, std::memory_order_release);
    });

    assert(waitUntil(started));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(source.requestCancellation());
    assert(waitUntil(finished));
    waiter.join();
    assert(!acquired.load(std::memory_order_acquire));
}

void testCombinedTokenCancellationWakesAcquire() {
    ConcurrencyGate gate(1U);
    auto heldLease = gate.acquire();
    assert(heldLease.has_value());

    TaskSystem taskSystem(1U);
    CancellationSource externalSource;
    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool acquired{true};

    const auto submitted = taskSystem.submit(
        TaskPriority::Normal,
        externalSource.token(),
        [&](CancellationToken token) {
            started.store(true, std::memory_order_release);
            const auto lease = gate.acquire(token);
            acquired.store(lease.has_value(), std::memory_order_release);
            finished.store(true, std::memory_order_release);
        });
    assert(submitted.hasValue());
    assert(waitUntil(started));
    assert(externalSource.requestCancellation());
    assert(waitUntil(finished));
    taskSystem.waitForCompletion();
    assert(!acquired.load(std::memory_order_acquire));

    ConcurrencyGate internalGate(1U);
    auto internalHeldLease = internalGate.acquire();
    assert(internalHeldLease.has_value());
    TaskSystem internalTaskSystem(1U);
    started.store(false, std::memory_order_release);
    finished.store(false, std::memory_order_release);
    acquired.store(true, std::memory_order_release);

    const auto internalSubmitted = internalTaskSystem.submit(
        TaskPriority::Normal,
        CancellationToken{},
        [&](CancellationToken token) {
            started.store(true, std::memory_order_release);
            const auto lease = internalGate.acquire(token);
            acquired.store(lease.has_value(), std::memory_order_release);
            finished.store(true, std::memory_order_release);
        });
    assert(internalSubmitted.hasValue());
    assert(waitUntil(started));
    internalTaskSystem.requestCancelAll();
    assert(waitUntil(finished));
    internalTaskSystem.waitForCompletion();
    assert(!acquired.load(std::memory_order_acquire));
}

void testCloseBehavior() {
    ConcurrencyGate gate(1U);
    auto heldLease = gate.acquire();
    assert(heldLease.has_value());

    std::atomic_bool started{false};
    std::atomic_bool finished{false};
    std::atomic_bool acquired{true};
    std::thread waiter([&] {
        started.store(true, std::memory_order_release);
        const auto lease = gate.acquire();
        acquired.store(lease.has_value(), std::memory_order_release);
        finished.store(true, std::memory_order_release);
    });

    assert(waitUntil(started));
    gate.close();
    gate.close();
    assert(waitUntil(finished));
    waiter.join();
    assert(!acquired.load(std::memory_order_acquire));
    assert(!gate.acquire().has_value());
    heldLease.reset();
    assert(!gate.acquire().has_value());
}

void testConcurrentAcquireReleaseCloseAndCancel() {
    ConcurrencyGate gate(2U);
    CancellationSource source;
    std::atomic_bool go{false};
    std::atomic_uint32_t active{0U};
    std::atomic_uint32_t peak{0U};
    std::atomic_uint32_t completed{0U};
    std::vector<std::thread> workers;

    for (std::size_t index = 0U; index < 8U; ++index) {
        workers.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (;;) {
                auto lease = gate.acquire(source.token());
                if (!lease.has_value()) {
                    break;
                }

                const auto current = active.fetch_add(1U, std::memory_order_acq_rel) + 1U;
                auto observedPeak = peak.load(std::memory_order_acquire);
                while (current > observedPeak &&
                       !peak.compare_exchange_weak(
                           observedPeak,
                           current,
                           std::memory_order_acq_rel,
                           std::memory_order_acquire)) {
                }
                std::this_thread::yield();
                active.fetch_sub(1U, std::memory_order_acq_rel);
                completed.fetch_add(1U, std::memory_order_acq_rel);
            }
        });
    }

    std::thread canceller([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        source.requestCancellation();
    });
    std::thread closer([&] {
        while (!go.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gate.close();
    });

    go.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    canceller.join();
    closer.join();

    assert(peak.load(std::memory_order_acquire) <= 2U);
    assert(completed.load(std::memory_order_acquire) != 0U);
    assert(active.load(std::memory_order_acquire) == 0U);
    assert(!gate.acquire().has_value());
}

} // namespace

int main() {
    testDefaultAndCustomCapacity();
    testZeroCapacityThrows();
    testLeaseReleaseMoveAndExceptionUnwind();
    testDefaultTokenWaitsForRelease();
    testCancellationWakesWaitingAcquire();
    testCombinedTokenCancellationWakesAcquire();
    testCloseBehavior();
    testConcurrentAcquireReleaseCloseAndCancel();
    return 0;
}