#include "tasks/TaskCompletionQueue.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dzc::tasks {

class TaskCompletionQueue::Impl final {
public:
    explicit Impl(std::size_t capacity)
        : m_capacity(capacity) {
        if (capacity == 0U) {
            throw std::invalid_argument(
                "TaskCompletionQueue capacity must be greater than zero");
        }
    }

    bool push(TaskCompletion completion) noexcept {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_spaceAvailable.wait(lock, [this] {
            return m_closed || m_queue.size() < m_capacity;
        });

        if (m_closed) {
            return false;
        }

        try {
            m_queue.push_back(std::move(completion));
        } catch (...) {
            return false;
        }

        lock.unlock();
        m_itemAvailable.notify_one();
        return true;
    }

    std::optional<TaskCompletion> tryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }

        TaskCompletion completion = std::move(m_queue.front());
        m_queue.pop_front();
        m_spaceAvailable.notify_one();
        return completion;
    }

    std::vector<TaskCompletion> tryPopBatch(std::size_t maxCount) {
        std::vector<TaskCompletion> result;
        if (maxCount == 0U) {
            return result;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const std::size_t count =
            (m_queue.size() < maxCount) ? m_queue.size() : maxCount;
        result.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            result.push_back(std::move(m_queue.front()));
            m_queue.pop_front();
        }
        if (count != 0U) {
            m_spaceAvailable.notify_all();
        }
        return result;
    }

    void close() noexcept {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_spaceAvailable.notify_all();
        m_itemAvailable.notify_all();
    }

private:
    const std::size_t m_capacity;
    std::deque<TaskCompletion> m_queue;
    bool m_closed = false;
    std::mutex m_mutex;
    std::condition_variable m_spaceAvailable;
    std::condition_variable m_itemAvailable;
};

TaskCompletionQueue::TaskCompletionQueue(std::size_t capacity)
    : m_impl(std::make_unique<Impl>(capacity)) {}

TaskCompletionQueue::~TaskCompletionQueue() {
    close();
}

bool TaskCompletionQueue::push(TaskCompletion completion) noexcept {
    return m_impl->push(std::move(completion));
}

std::optional<TaskCompletion> TaskCompletionQueue::tryPop() {
    return m_impl->tryPop();
}

std::vector<TaskCompletion> TaskCompletionQueue::tryPopBatch(std::size_t maxCount) {
    return m_impl->tryPopBatch(maxCount);
}

void TaskCompletionQueue::close() noexcept {
    if (m_impl != nullptr) {
        m_impl->close();
    }
}

} // namespace dzc::tasks