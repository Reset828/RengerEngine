#include "tasks/BackpressureController.h"
#include "tasks/ConcurrencyGate.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::ErrorDomain;
using dzc::TaskId;
using dzc::tasks::BackpressureController;
using dzc::tasks::CancellationToken;
using dzc::tasks::ConcurrencyGate;
using dzc::tasks::TaskErrorCode;
using dzc::tasks::TaskPriority;
using dzc::tasks::TaskSystem;

constexpr auto kTimeout = std::chrono::seconds(5);

bool waitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

void assertNotAccepting(const dzc::Result<TaskId>& result) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::Task);
    assert(result.error().code ==
           static_cast<std::uint32_t>(TaskErrorCode::NotAccepting));
}

void testShutdownCancelsRunningAndQueuedTasksAndRejectsSubmissions() {
    TaskSystem system(1U, 4U, 4U);
    std::atomic_bool runningStarted{false};
    std::atomic_bool runningCancelled{false};
    std::atomic_bool queuedInvoked{false};
    std::atomic_bool queuedCancelled{false};

    assert(system.submit(TaskPriority::Critical, {}, [&](CancellationToken token) {
        runningStarted.store(true, std::memory_order_release);
        while (!token.isCancellationRequested()) {
            std::this_thread::yield();
        }
        runningCancelled.store(true, std::memory_order_release);
    }).hasValue());
    assert(waitUntil([&] {
        return runningStarted.load(std::memory_order_acquire);
    }));

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        queuedInvoked.store(true, std::memory_order_release);
        queuedCancelled.store(
            token.isCancellationRequested(),
            std::memory_order_release);
    }).hasValue());

    system.shutdown();

    assert(runningCancelled.load(std::memory_order_acquire));
    assert(queuedInvoked.load(std::memory_order_acquire));
    assert(queuedCancelled.load(std::memory_order_acquire));
    assertNotAccepting(system.submit(TaskPriority::Normal, {}, [](CancellationToken) {}));

    system.shutdown();
}

void testShutdownCancelsTaskWaitingForConcurrencyGate() {
    ConcurrencyGate gate(1U);
    const auto heldLease = gate.acquire();
    assert(heldLease.has_value());

    TaskSystem system(1U, 2U, 2U);
    std::atomic_bool taskStarted{false};
    std::atomic_bool taskFinished{false};

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        taskStarted.store(true, std::memory_order_release);
        const auto lease = gate.acquire(token);
        assert(!lease.has_value());
        taskFinished.store(true, std::memory_order_release);
    }).hasValue());

    assert(waitUntil([&] {
        return taskStarted.load(std::memory_order_acquire);
    }));
    system.shutdown();
    assert(taskFinished.load(std::memory_order_acquire));
}

void testShutdownCancelsTaskWaitingForBackpressureRecovery() {
    BackpressureController controller(10U);
    controller.updateUsage(10U);

    TaskSystem system(1U, 2U, 2U);
    std::atomic_bool taskStarted{false};
    std::atomic_bool taskFinished{false};

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        taskStarted.store(true, std::memory_order_release);
        assert(!controller.waitUntilResumed(token));
        taskFinished.store(true, std::memory_order_release);
    }).hasValue());

    assert(waitUntil([&] {
        return taskStarted.load(std::memory_order_acquire);
    }));
    system.shutdown();
    assert(taskFinished.load(std::memory_order_acquire));
}

void testShutdownClosesFullCompletionQueueAndJoinsWorkers() {
    TaskSystem system(1U, 4U, 1U);
    std::atomic_bool firstTaskRan{false};
    std::atomic_bool secondTaskRan{false};

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
        firstTaskRan.store(true, std::memory_order_release);
    }).hasValue());
    assert(waitUntil([&] {
        return firstTaskRan.load(std::memory_order_acquire);
    }));

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken) {
        secondTaskRan.store(true, std::memory_order_release);
    }).hasValue());
    assert(waitUntil([&] {
        return secondTaskRan.load(std::memory_order_acquire);
    }));

    std::atomic_bool shutdownFinished{false};
    std::thread shutdownThread([&] {
        system.shutdown();
        shutdownFinished.store(true, std::memory_order_release);
    });

    assert(waitUntil([&] {
        return shutdownFinished.load(std::memory_order_acquire);
    }));
    shutdownThread.join();

    const auto preservedCompletion = system.tryPopCompletion();
    assert(preservedCompletion.has_value());
}

void testConcurrentShutdownAndWaitForCompletionAreSafe() {
    TaskSystem system(1U, 2U, 2U);
    std::atomic_bool taskStarted{false};
    std::atomic_bool taskCancelled{false};

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        taskStarted.store(true, std::memory_order_release);
        while (!token.isCancellationRequested()) {
            std::this_thread::yield();
        }
        taskCancelled.store(true, std::memory_order_release);
    }).hasValue());
    assert(waitUntil([&] {
        return taskStarted.load(std::memory_order_acquire);
    }));

    std::thread normalWaiter([&] { system.waitForCompletion(); });
    std::vector<std::thread> shutdownThreads;
    shutdownThreads.reserve(4U);
    for (std::size_t index = 0U; index < 4U; ++index) {
        shutdownThreads.emplace_back([&] { system.shutdown(); });
    }

    for (std::thread& thread : shutdownThreads) {
        thread.join();
    }
    normalWaiter.join();

    assert(taskCancelled.load(std::memory_order_acquire));
}

void testDestructorUsesSafeShutdown() {
    std::atomic_bool taskStarted{false};
    std::atomic_bool taskCancelled{false};
    {
        TaskSystem system(1U, 2U, 2U);
        assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
            taskStarted.store(true, std::memory_order_release);
            while (!token.isCancellationRequested()) {
                std::this_thread::yield();
            }
            taskCancelled.store(true, std::memory_order_release);
        }).hasValue());
        assert(waitUntil([&] {
            return taskStarted.load(std::memory_order_acquire);
        }));
    }

    assert(taskCancelled.load(std::memory_order_acquire));
}

void testWaitForCompletionStillDrainsWithoutCancellingTasks() {
    TaskSystem system(1U, 2U, 2U);
    std::atomic_bool taskStarted{false};
    std::atomic_bool releaseTask{false};
    std::atomic_bool tokenWasCancelled{true};
    std::atomic_bool waitFinished{false};

    assert(system.submit(TaskPriority::Normal, {}, [&](CancellationToken token) {
        tokenWasCancelled.store(
            token.isCancellationRequested(),
            std::memory_order_release);
        taskStarted.store(true, std::memory_order_release);
        while (!releaseTask.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }).hasValue());
    assert(waitUntil([&] {
        return taskStarted.load(std::memory_order_acquire);
    }));

    std::thread waiter([&] {
        system.waitForCompletion();
        waitFinished.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!waitFinished.load(std::memory_order_acquire));
    releaseTask.store(true, std::memory_order_release);
    waiter.join();

    assert(!tokenWasCancelled.load(std::memory_order_acquire));
    const auto completion = system.tryPopCompletion();
    assert(completion.has_value());
    assert(completion->result.hasValue());
}

} // namespace

int main() {
    testShutdownCancelsRunningAndQueuedTasksAndRejectsSubmissions();
    testShutdownCancelsTaskWaitingForConcurrencyGate();
    testShutdownCancelsTaskWaitingForBackpressureRecovery();
    testShutdownClosesFullCompletionQueueAndJoinsWorkers();
    testConcurrentShutdownAndWaitForCompletionAreSafe();
    testDestructorUsesSafeShutdown();
    testWaitForCompletionStillDrainsWithoutCancellingTasks();
    return 0;
}