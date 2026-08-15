#include "tasks/TaskCompletionQueue.h"
#include "tasks/TaskSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::DatasetId;
using dzc::ErrorDomain;
using dzc::Result;
using dzc::TaskId;
using dzc::tasks::CancellationSource;
using dzc::tasks::CancellationToken;
using dzc::tasks::TaskCompletion;
using dzc::tasks::TaskCompletionQueue;
using dzc::tasks::TaskErrorCode;
using dzc::tasks::TaskPriority;
using dzc::tasks::TaskSystem;

TaskCompletion successCompletion(std::uint64_t id) {
    return TaskCompletion{TaskId{id}, std::nullopt, Result<void>::success()};
}

void assertError(const Result<void>& result, TaskErrorCode expectedCode) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::Task);
    assert(result.error().code == static_cast<std::uint32_t>(expectedCode));
    assert(!result.error().userMessage.empty());
}

void testQueueCapacityFifoCloseAndBlockingPush() {
    bool zeroCapacityThrew = false;
    try {
        TaskCompletionQueue invalid(0U);
    } catch (const std::invalid_argument&) {
        zeroCapacityThrew = true;
    }
    assert(zeroCapacityThrew);

    TaskCompletionQueue queue(1U);
    assert(queue.push(successCompletion(1U)));

    std::atomic_bool producerFinished{false};
    std::atomic_bool producerSucceeded{false};
    std::thread producer([&] {
        producerSucceeded.store(queue.push(successCompletion(2U)),
                                std::memory_order_release);
        producerFinished.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!producerFinished.load(std::memory_order_acquire));

    const auto first = queue.tryPop();
    assert(first.has_value());
    assert(first->taskId == TaskId{1U});
    producer.join();
    assert(producerSucceeded.load(std::memory_order_acquire));

    const auto second = queue.tryPop();
    assert(second.has_value());
    assert(second->taskId == TaskId{2U});
    assert(!queue.tryPop().has_value());

    queue.close();
    assert(!queue.push(successCompletion(3U)));
    queue.close();
}

void testQueueCloseWakesBlockedPublisherAndPreservesAcceptedEntries() {
    TaskCompletionQueue queue(1U);
    assert(queue.push(successCompletion(1U)));

    std::atomic_bool publisherStarted{false};
    std::atomic_bool publisherSucceeded{true};
    std::thread publisher([&] {
        publisherStarted.store(true, std::memory_order_release);
        publisherSucceeded.store(queue.push(successCompletion(2U)),
                               std::memory_order_release);
    });

    while (!publisherStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.close();
    publisher.join();

    assert(!publisherSucceeded.load(std::memory_order_acquire));
    const auto accepted = queue.tryPop();
    assert(accepted.has_value());
    assert(accepted->taskId == TaskId{1U});
    assert(!queue.tryPop().has_value());
}
void testBatchAndResultKinds() {
    TaskCompletionQueue queue(8U);
    assert(queue.push(successCompletion(1U)));
    assert(queue.push(TaskCompletion{
        TaskId{2U},
        DatasetId{7U},
        Result<void>::failure(dzc::Error{
            ErrorDomain::Task,
            static_cast<std::uint32_t>(TaskErrorCode::UnknownException),
            "failure",
            "diagnostic",
            "context"})}));

    const auto batch = queue.tryPopBatch(8U);
    assert(batch.size() == 2U);
    assert(batch[0].taskId == TaskId{1U});
    assert(batch[0].result.hasValue());
    assert(batch[1].datasetId.has_value());
    assert(batch[1].datasetId->value == 7U);
    assertError(batch[1].result, TaskErrorCode::UnknownException);
    assert(queue.tryPopBatch(0U).empty());
}

void testTaskSystemPublishesSuccessFailureCancellationAndDataset() {
    TaskSystem system(1U, 8U, 8U);
    CancellationSource externalSource;

    assert(system.submit(TaskPriority::Normal, {}, [](CancellationToken) {
        return Result<void>::success();
    }).hasValue());

    assert(system.submitForDataset(
        DatasetId{42U},
        TaskPriority::High,
        {},
        [](CancellationToken) {
            dzc::Error error;
            error.domain = ErrorDomain::Task;
            error.code = 900U;
            error.userMessage = "task failure";
            return Result<void>::failure(std::move(error));
        }).hasValue());

    assert(system.submit(TaskPriority::Low, {}, [](CancellationToken) -> Result<void> {
        throw std::runtime_error("boom");
    }).hasValue());

    externalSource.requestCancellation();
    assert(system.submit(TaskPriority::Normal, externalSource.token(),
                         [](CancellationToken token) {
                             assert(token.isCancellationRequested());
                             return Result<void>::success();
                         }).hasValue());

    system.waitForCompletion();
    const auto completions = system.tryPopCompletionBatch(16U);
    assert(completions.size() == 4U);
    for (const TaskCompletion& completion : completions) {
        switch (completion.taskId.value) {
        case 1U:
            assert(completion.result.hasValue());
            assert(!completion.datasetId.has_value());
            break;
        case 2U:
            assert(completion.datasetId.has_value());
            assert(completion.datasetId->value == 42U);
            assertError(completion.result, static_cast<TaskErrorCode>(900U));
            break;
        case 3U:
            assertError(completion.result, TaskErrorCode::UnhandledException);
            break;
        case 4U:
            assertError(completion.result, TaskErrorCode::Cancelled);
            break;
        default:
            assert(false);
        }
    }
    assert(!system.tryPopCompletion().has_value());
}

void testCompletionOrderAndOldDatasetFiltering() {
    TaskSystem system(1U, 4U, 4U);
    assert(system.submitForDataset(DatasetId{1U}, TaskPriority::Normal, {},
                                   [](CancellationToken) {
                                       return Result<void>::success();
                                   }).hasValue());
    assert(system.submitForDataset(DatasetId{2U}, TaskPriority::Normal, {},
                                   [](CancellationToken) {
                                       return Result<void>::success();
                                   }).hasValue());
    system.waitForCompletion();

    const auto first = system.tryPopCompletion();
    const auto second = system.tryPopCompletion();
    assert(first.has_value() && second.has_value());
    assert(first->taskId == TaskId{1U});
    assert(second->taskId == TaskId{2U});
    const DatasetId current{2U};
    assert(first->datasetId.has_value() && first->datasetId.value() != current);
    assert(second->datasetId.has_value() && second->datasetId.value() == current);
}

void testTaskSystemCompletionCapacityValidation() {
    bool zeroCompletionCapacityThrew = false;
    try {
        TaskSystem invalid(1U, 1U, 0U);
    } catch (const std::invalid_argument&) {
        zeroCompletionCapacityThrew = true;
    }
    assert(zeroCompletionCapacityThrew);
}

} // namespace

int main() {
    testQueueCapacityFifoCloseAndBlockingPush();
    testQueueCloseWakesBlockedPublisherAndPreservesAcceptedEntries();
    testBatchAndResultKinds();
    testTaskSystemPublishesSuccessFailureCancellationAndDataset();
    testCompletionOrderAndOldDatasetFiltering();
    testTaskSystemCompletionCapacityValidation();
    return 0;
}