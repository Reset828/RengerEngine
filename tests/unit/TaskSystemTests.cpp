#include "tasks/TaskSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::ErrorDomain;
using dzc::TaskId;
using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::TaskErrorCode;
using dzc::tasks::TaskPriority;
using dzc::tasks::TaskSystem;

class Gate final {
public:
    void enterAndWait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_entered = true;
        m_enteredCondition.notify_all();
        m_releaseCondition.wait(lock, [this] { return m_released; });
    }

    void waitUntilEntered() {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool entered = m_enteredCondition.wait_for(
            lock,
            std::chrono::seconds(5),
            [this] { return m_entered; });
        assert(entered);
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_releaseCondition.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_enteredCondition;
    std::condition_variable m_releaseCondition;
    bool m_entered = false;
    bool m_released = false;
};

void assertTaskError(
    const dzc::Result<TaskId>& result,
    TaskErrorCode expectedCode) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::Task);
    assert(result.error().code == static_cast<std::uint32_t>(expectedCode));
    assert(!result.error().userMessage.empty());
}

void testConstructionValidationAndTaskIds() {
    bool zeroWorkersThrew = false;
    try {
        TaskSystem invalidWorkers(0U);
    } catch (const std::invalid_argument&) {
        zeroWorkersThrew = true;
    }
    assert(zeroWorkersThrew);

    bool zeroCapacityThrew = false;
    try {
        TaskSystem invalidCapacity(1U, 0U);
    } catch (const std::invalid_argument&) {
        zeroCapacityThrew = true;
    }
    assert(zeroCapacityThrew);

    TaskSystem system(1U);
    const auto first = system.submit(TaskPriority::Normal, {}, [](CancellationToken) {});
    const auto second = system.submit(TaskPriority::Normal, {}, [](CancellationToken) {});
    const auto third = system.submit(TaskPriority::Normal, {}, [](CancellationToken) {});

    assert(first.hasValue());
    assert(second.hasValue());
    assert(third.hasValue());
    assert(first.value() == TaskId{1U});
    assert(second.value() == TaskId{2U});
    assert(third.value() == TaskId{3U});
    system.waitForCompletion();
}

void testFifoAndStrictPriorityScheduling() {
    {
        TaskSystem system(1U, 8U);
        std::vector<int> executionOrder;
        assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
            executionOrder.push_back(1);
        }).hasValue());
        assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
            executionOrder.push_back(2);
        }).hasValue());
        assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
            executionOrder.push_back(3);
        }).hasValue());

        system.waitForCompletion();
        assert((executionOrder == std::vector<int>{1, 2, 3}));
    }

    TaskSystem system(1U, 8U);
    Gate gate;
    std::vector<int> executionOrder;

    assert(system.submit(TaskPriority::Critical, {}, [&](CancellationToken) {
        gate.enterAndWait();
    }).hasValue());
    gate.waitUntilEntered();

    assert(system.submit(TaskPriority::Low, {}, [&](CancellationToken) {
        executionOrder.push_back(1);
    }).hasValue());
    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
        executionOrder.push_back(2);
    }).hasValue());
    assert(system.submit(TaskPriority::High, {}, [&](CancellationToken) {
        executionOrder.push_back(3);
    }).hasValue());

    gate.release();
    system.waitForCompletion();
    assert((executionOrder == std::vector<int>{3, 2, 1}));
}

void testPerPriorityCapacityAndSubmitErrors() {
    TaskSystem system(1U, 1U);
    Gate gate;

    const std::function<void(CancellationToken)> emptyTask;
    assertTaskError(
        system.submit(TaskPriority::Normal, {}, emptyTask),
        TaskErrorCode::InvalidTask);
    assertTaskError(
        system.submit(static_cast<TaskPriority>(99U), {}, [](CancellationToken) {}),
        TaskErrorCode::InvalidTask);

    assert(system.submit(TaskPriority::Critical, {}, [&](CancellationToken) {
        gate.enterAndWait();
    }).hasValue());
    gate.waitUntilEntered();

    assert(system.submit(TaskPriority::High, {}, [](CancellationToken) {}).hasValue());
    assertTaskError(
        system.submit(TaskPriority::High, {}, [](CancellationToken) {}),
        TaskErrorCode::QueueFull);
    assert(system.submit(TaskPriority::Low, {}, [](CancellationToken) {}).hasValue());

    gate.release();
    system.stopAccepting();
    assertTaskError(
        system.submit(TaskPriority::Normal, {}, [](CancellationToken) {}),
        TaskErrorCode::NotAccepting);
    system.waitForCompletion();
}

void testExceptionsDoNotEscapeOrStopWorkers() {
    TaskSystem system(1U, 8U);
    std::atomic_uint completed{0U};

    assert(system.submit(TaskPriority::Normal, {}, [](CancellationToken) {
        throw std::runtime_error("expected test exception");
    }).hasValue());
    assert(system.submit(TaskPriority::Normal, {}, [](CancellationToken) {
        throw 17;
    }).hasValue());
    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
        completed.fetch_add(1U, std::memory_order_relaxed);
    }).hasValue());

    system.waitForCompletion();
    assert(completed.load(std::memory_order_relaxed) == 1U);
}

void testCombinedCancellationAndFutureSubmissions() {
    {
        TaskSystem system(1U, 8U);
        CancellationSource externalSource;
        assert(externalSource.requestCancellation());
        std::atomic_bool sawExternalCancellation{false};

        assert(system.submit(TaskPriority::Normal, externalSource.token(), [&](CancellationToken token) {
            sawExternalCancellation.store(
                token.isCancellationRequested(),
                std::memory_order_release);
        }).hasValue());
        system.waitForCompletion();
        assert(sawExternalCancellation.load(std::memory_order_acquire));
    }

    TaskSystem system(1U, 8U);
    Gate gate;
    std::atomic_bool runningSawCancellation{false};
    std::atomic_bool queuedSawCancellation{false};
    std::atomic_bool futureTaskWasCancelled{true};

    assert(system.submit(TaskPriority::Critical, {}, [&](CancellationToken token) {
        gate.enterAndWait();
        while (!token.isCancellationRequested()) {
            std::this_thread::yield();
        }
        runningSawCancellation.store(true, std::memory_order_release);
    }).hasValue());

    // The worker must reach the task body before requestCancelAll() is issued.
    gate.waitUntilEntered();
    assert(system.submit(TaskPriority::Low, {}, [&](CancellationToken token) {
        queuedSawCancellation.store(
            token.isCancellationRequested(),
            std::memory_order_release);
    }).hasValue());

    system.requestCancelAll();
    gate.release();

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        futureTaskWasCancelled.store(
            token.isCancellationRequested(),
            std::memory_order_release);
    }).hasValue());

    system.waitForCompletion();
    assert(runningSawCancellation.load(std::memory_order_acquire));
    assert(queuedSawCancellation.load(std::memory_order_acquire));
    assert(!futureTaskWasCancelled.load(std::memory_order_acquire));
}

void testStopDrainConcurrentWaitAndDestruction() {
    {
        TaskSystem system(2U, 64U);
        std::atomic_uint completed{0U};
        for (std::size_t index = 0U; index < 40U; ++index) {
            assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
                completed.fetch_add(1U, std::memory_order_relaxed);
            }).hasValue());
        }

        std::vector<std::thread> waiters;
        for (std::size_t index = 0U; index < 4U; ++index) {
            waiters.emplace_back([&] { system.waitForCompletion(); });
        }
        for (std::thread& waiter : waiters) {
            waiter.join();
        }
        assert(completed.load(std::memory_order_relaxed) == 40U);
        assertTaskError(
            system.submit(TaskPriority::Normal, {}, [](CancellationToken) {}),
            TaskErrorCode::NotAccepting);
    }

    std::atomic_uint destroyedCompleted{0U};
    {
        TaskSystem system(1U, 4U);
        assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
            destroyedCompleted.fetch_add(1U, std::memory_order_relaxed);
        }).hasValue());
    }
    assert(destroyedCompleted.load(std::memory_order_relaxed) == 1U);
}

void testMultiProducerStress() {
    constexpr std::size_t producerCount = 4U;
    constexpr std::size_t tasksPerProducer = 80U;

    TaskSystem system(4U, 128U);
    std::atomic_uint completed{0U};
    std::atomic_bool start{false};
    std::vector<std::thread> producers;
    producers.reserve(producerCount);

    for (std::size_t producer = 0U; producer < producerCount; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start.load(std::memory_order_acquire)) {
            }

            for (std::size_t index = 0U; index < tasksPerProducer; ++index) {
                const auto priority = static_cast<TaskPriority>(index % 4U);
                const auto result = system.submit(priority, {}, [&](CancellationToken) {
                    completed.fetch_add(1U, std::memory_order_relaxed);
                });
                assert(result.hasValue());
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread& producer : producers) {
        producer.join();
    }

    system.waitForCompletion();
    assert(completed.load(std::memory_order_relaxed) == producerCount * tasksPerProducer);
}

} // namespace

int main() {
    testConstructionValidationAndTaskIds();
    testFifoAndStrictPriorityScheduling();
    testPerPriorityCapacityAndSubmitErrors();
    testExceptionsDoNotEscapeOrStopWorkers();
    testCombinedCancellationAndFutureSubmissions();
    testStopDrainConcurrentWaitAndDestruction();
    testMultiProducerStress();
    return 0;
}