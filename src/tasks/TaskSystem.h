#pragma once

#include "tasks/Cancellation.h"
#include "tasks/TaskCompletion.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <dzc/EngineTypes.h>
#include <dzc/Result.h>

namespace dzc::tasks {

enum class TaskPriority : std::uint8_t {
    Critical,
    High,
    Normal,
    Low
};

enum class TaskErrorCode : std::uint32_t {
    InvalidTask = 1U,
    NotAccepting = 2U,
    QueueFull = 3U,
    TaskIdExhausted = 4U,
    UnhandledException = 5U,
    UnknownException = 6U,
    Cancelled = 7U
};

class TaskSystem final {
public:
    explicit TaskSystem(
        std::uint32_t workerThreads,
        std::size_t queueCapacity = 1024U,
        std::size_t completionQueueCapacity = 1024U);
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;
    TaskSystem(TaskSystem&&) = delete;
    TaskSystem& operator=(TaskSystem&&) = delete;

    template <typename Task>
    Result<TaskId> submit(
        TaskPriority priority,
        CancellationToken token,
        Task&& task) {
        using Callable = std::decay_t<Task>;
        static_assert(
            std::is_invocable_v<Callable&, CancellationToken>,
            "Task must be invocable with CancellationToken.");
        using Return = std::invoke_result_t<Callable&, CancellationToken>;
        static_assert(
            std::is_same_v<Return, void> || std::is_same_v<Return, Result<void>>,
            "Task must return void or dzc::Result<void>.");

        if constexpr (std::is_same_v<Return, void>) {
            return submitVoid(
                priority,
                std::move(token),
                std::function<void(CancellationToken)>(std::forward<Task>(task)));
        } else {
            return submitResult(
                priority,
                std::move(token),
                std::function<Result<void>(CancellationToken)>(
                    std::forward<Task>(task)));
        }
    }

    template <typename Task>
    Result<TaskId> submitForDataset(
        DatasetId datasetId,
        TaskPriority priority,
        CancellationToken token,
        Task&& task) {
        using Callable = std::decay_t<Task>;
        static_assert(
            std::is_invocable_v<Callable&, CancellationToken>,
            "Task must be invocable with CancellationToken.");
        using Return = std::invoke_result_t<Callable&, CancellationToken>;
        static_assert(
            std::is_same_v<Return, void> || std::is_same_v<Return, Result<void>>,
            "Task must return void or dzc::Result<void>.");

        if constexpr (std::is_same_v<Return, void>) {
            return submitForDatasetVoid(
                datasetId,
                priority,
                std::move(token),
                std::function<void(CancellationToken)>(std::forward<Task>(task)));
        } else {
            return submitForDatasetResult(
                datasetId,
                priority,
                std::move(token),
                std::function<Result<void>(CancellationToken)>(
                    std::forward<Task>(task)));
        }
    }

    std::optional<TaskCompletion> tryPopCompletion();
    std::vector<TaskCompletion> tryPopCompletionBatch(std::size_t maxCount);

    void stopAccepting() noexcept;
    void requestCancelAll() noexcept;
    void waitForCompletion() noexcept;
    void shutdown() noexcept;

private:
    Result<TaskId> submitResult(
        TaskPriority priority,
        CancellationToken token,
        std::function<Result<void>(CancellationToken)> task);
    Result<TaskId> submitVoid(
        TaskPriority priority,
        CancellationToken token,
        std::function<void(CancellationToken)> task);
    Result<TaskId> submitForDatasetResult(
        DatasetId datasetId,
        TaskPriority priority,
        CancellationToken token,
        std::function<Result<void>(CancellationToken)> task);
    Result<TaskId> submitForDatasetVoid(
        DatasetId datasetId,
        TaskPriority priority,
        CancellationToken token,
        std::function<void(CancellationToken)> task);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::tasks
