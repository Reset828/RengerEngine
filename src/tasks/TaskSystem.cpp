#include "tasks/TaskSystem.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dzc::tasks {
namespace {

constexpr std::size_t kPriorityCount = 4U;

std::optional<std::size_t> priorityIndex(TaskPriority priority) noexcept {
    switch (priority) {
    case TaskPriority::Critical:
        return 0U;
    case TaskPriority::High:
        return 1U;
    case TaskPriority::Normal:
        return 2U;
    case TaskPriority::Low:
        return 3U;
    }

    return std::nullopt;
}

Error makeTaskError(TaskErrorCode code, const char* userMessage) {
    Error error;
    error.domain = ErrorDomain::Task;
    error.code = static_cast<std::uint32_t>(code);
    error.userMessage = userMessage;
    return error;
}

} // namespace

class TaskSystem::Impl final {
public:
    Impl(std::uint32_t workerThreads, std::size_t queueCapacity)
        : m_queueCapacity(queueCapacity) {
        if (workerThreads == 0U) {
            throw std::invalid_argument("TaskSystem workerThreads must be greater than zero");
        }
        if (queueCapacity == 0U) {
            throw std::invalid_argument("TaskSystem queueCapacity must be greater than zero");
        }

        m_workers.reserve(workerThreads);
        try {
            for (std::uint32_t index = 0U; index < workerThreads; ++index) {
                m_workers.emplace_back([this] { workerLoop(); });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_exitRequested = true;
            }
            m_workAvailable.notify_all();
            for (std::thread& worker : m_workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            throw;
        }
    }

    ~Impl() {
        waitForCompletion();
    }

    Result<TaskId> submit(
        TaskPriority priority,
        std::shared_ptr<CancellationSource> cancellationSource,
        CancellationToken token,
        std::function<void(CancellationToken)> task) {
        if (!task) {
            return Result<TaskId>::failure(makeTaskError(
                TaskErrorCode::InvalidTask,
                "Task submission requires a non-empty task function."));
        }

        const std::optional<std::size_t> index = priorityIndex(priority);
        if (!index.has_value()) {
            return Result<TaskId>::failure(makeTaskError(
                TaskErrorCode::InvalidTask,
                "Task submission uses an invalid task priority."));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_accepting) {
            return Result<TaskId>::failure(makeTaskError(
                TaskErrorCode::NotAccepting,
                "Task system is no longer accepting tasks."));
        }
        if (m_taskIdExhausted) {
            return Result<TaskId>::failure(makeTaskError(
                TaskErrorCode::TaskIdExhausted,
                "Task identifier space is exhausted."));
        }
        if (m_queues[*index].size() == m_queueCapacity) {
            return Result<TaskId>::failure(makeTaskError(
                TaskErrorCode::QueueFull,
                "The selected task priority queue is full."));
        }

        const TaskId taskId{m_nextTaskId};
        try {
            const auto insertion = m_activeCancellationSources.emplace(
                taskId.value,
                std::move(cancellationSource));
            if (!insertion.second) {
                return Result<TaskId>::failure(makeTaskError(
                    TaskErrorCode::TaskIdExhausted,
                    "Task identifier space is exhausted."));
            }

            try {
                m_queues[*index].push_back(TaskItem{
                    taskId,
                    std::move(token),
                    std::move(task)});
            } catch (...) {
                m_activeCancellationSources.erase(insertion.first);
                throw;
            }
        } catch (...) {
            throw;
        }

        if (m_nextTaskId == std::numeric_limits<std::uint64_t>::max()) {
            m_taskIdExhausted = true;
        } else {
            ++m_nextTaskId;
        }

        m_workAvailable.notify_one();
        return Result<TaskId>::success(taskId);
    }

    void stopAccepting() noexcept {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_accepting = false;
        }
        m_workAvailable.notify_all();
    }

    void requestCancelAll() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_activeCancellationSources) {
            entry.second->requestCancellation();
        }
    }

    void waitForCompletion() noexcept {
        stopAccepting();

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_workersJoined) {
                return;
            }
            if (m_joinInProgress) {
                m_workersJoinedCondition.wait(lock, [this] { return m_workersJoined; });
                return;
            }

            m_allTasksCompleted.wait(lock, [this] {
                return queuedTaskCountLocked() == 0U && m_activeTaskCount == 0U;
            });
            m_exitRequested = true;
            m_joinInProgress = true;
        }

        m_workAvailable.notify_all();
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_workersJoined = true;
            m_joinInProgress = false;
        }
        m_workersJoinedCondition.notify_all();
    }

private:
    struct TaskItem final {
        TaskId id;
        CancellationToken token;
        std::function<void(CancellationToken)> task;
    };

    struct TaskFailureRecord final {
        TaskId id;
        Error error;
    };

    std::size_t queuedTaskCountLocked() const noexcept {
        std::size_t count = 0U;
        for (const std::deque<TaskItem>& queue : m_queues) {
            count += queue.size();
        }
        return count;
    }

    std::optional<TaskItem> takeNextTaskLocked() {
        for (std::deque<TaskItem>& queue : m_queues) {
            if (!queue.empty()) {
                TaskItem item = std::move(queue.front());
                queue.pop_front();
                ++m_activeTaskCount;
                return item;
            }
        }
        return std::nullopt;
    }

    void recordFailure(TaskId id, TaskErrorCode code, const char* diagnosticMessage) noexcept {
        try {
            Error error = makeTaskError(code, "Task execution failed.");
            error.diagnosticMessage = diagnosticMessage;

            std::lock_guard<std::mutex> lock(m_mutex);
            m_failures.push_back(TaskFailureRecord{id, std::move(error)});
        } catch (...) {
            // An allocation failure while recording a failure must not terminate a worker.
        }
    }

    void recordFailure(TaskId id, const std::exception& exception) noexcept {
        try {
            Error error = makeTaskError(
                TaskErrorCode::UnhandledException,
                "Task execution failed.");
            error.diagnosticMessage = exception.what();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_failures.push_back(TaskFailureRecord{id, std::move(error)});
        } catch (...) {
            // An allocation failure while recording a failure must not terminate a worker.
        }
    }

    void finishTask(TaskId id) noexcept {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_activeCancellationSources.erase(id.value);
            --m_activeTaskCount;
        }
        m_allTasksCompleted.notify_all();
    }

    void workerLoop() noexcept {
        for (;;) {
            std::optional<TaskItem> item;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_workAvailable.wait(lock, [this] {
                    return m_exitRequested || queuedTaskCountLocked() != 0U;
                });

                if (m_exitRequested && queuedTaskCountLocked() == 0U) {
                    return;
                }

                item = takeNextTaskLocked();
            }

            if (!item.has_value()) {
                continue;
            }

            try {
                item->task(item->token);
            } catch (const std::exception& exception) {
                recordFailure(item->id, exception);
            } catch (...) {
                recordFailure(
                    item->id,
                    TaskErrorCode::UnknownException,
                    "Task threw a non-standard exception.");
            }

            finishTask(item->id);
        }
    }

    const std::size_t m_queueCapacity;
    std::array<std::deque<TaskItem>, kPriorityCount> m_queues;
    std::unordered_map<std::uint64_t, std::shared_ptr<CancellationSource>>
        m_activeCancellationSources;
    std::deque<TaskFailureRecord> m_failures;
    std::vector<std::thread> m_workers;

    std::uint64_t m_nextTaskId = 1U;
    std::size_t m_activeTaskCount = 0U;
    bool m_taskIdExhausted = false;
    bool m_accepting = true;
    bool m_exitRequested = false;
    bool m_joinInProgress = false;
    bool m_workersJoined = false;

    std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    std::condition_variable m_allTasksCompleted;
    std::condition_variable m_workersJoinedCondition;
};

TaskSystem::TaskSystem(std::uint32_t workerThreads, std::size_t queueCapacity)
    : m_impl(std::make_unique<Impl>(workerThreads, queueCapacity)) {}

TaskSystem::~TaskSystem() {
    waitForCompletion();
}

Result<TaskId> TaskSystem::submit(
    TaskPriority priority,
    CancellationToken token,
    std::function<void(CancellationToken)> task) {
    const auto cancellationSource = std::make_shared<CancellationSource>();
    CancellationToken combinedToken = CancellationToken::combine(
        token,
        cancellationSource->token());
    return m_impl->submit(
        priority,
        cancellationSource,
        std::move(combinedToken),
        std::move(task));
}

void TaskSystem::stopAccepting() noexcept {
    m_impl->stopAccepting();
}

void TaskSystem::requestCancelAll() noexcept {
    m_impl->requestCancelAll();
}

void TaskSystem::waitForCompletion() noexcept {
    m_impl->waitForCompletion();
}

} // namespace dzc::tasks