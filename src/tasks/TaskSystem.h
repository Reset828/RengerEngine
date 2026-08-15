#pragma once

#include "tasks/Cancellation.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

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
    UnknownException = 6U
};

class TaskSystem final {
public:
    explicit TaskSystem(
        std::uint32_t workerThreads,
        std::size_t queueCapacity = 1024U);
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;
    TaskSystem(TaskSystem&&) = delete;
    TaskSystem& operator=(TaskSystem&&) = delete;

    Result<TaskId> submit(
        TaskPriority priority,
        CancellationToken token,
        std::function<void(CancellationToken)> task);

    void stopAccepting() noexcept;
    void requestCancelAll() noexcept;
    void waitForCompletion() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::tasks