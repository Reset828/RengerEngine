#pragma once

#include "tasks/TaskCompletion.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace dzc::tasks {

class TaskCompletionQueue final {
public:
    explicit TaskCompletionQueue(std::size_t capacity = 1024U);
    ~TaskCompletionQueue();

    TaskCompletionQueue(const TaskCompletionQueue&) = delete;
    TaskCompletionQueue& operator=(const TaskCompletionQueue&) = delete;
    TaskCompletionQueue(TaskCompletionQueue&&) = delete;
    TaskCompletionQueue& operator=(TaskCompletionQueue&&) = delete;

    // Waits for space while the queue is open. Returns false when the queue
    // is closed or an allocation failure prevents publication.
    bool push(TaskCompletion completion) noexcept;
    std::optional<TaskCompletion> tryPop();
    std::vector<TaskCompletion> tryPopBatch(std::size_t maxCount);

    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc::tasks