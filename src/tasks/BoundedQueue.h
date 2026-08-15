#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace dzc::tasks {

template <typename T>
class BoundedQueue final {
    static_assert(std::is_move_constructible<T>::value,
                  "BoundedQueue<T> requires a move-constructible T");

public:
    explicit BoundedQueue(std::size_t capacity = 1024U)
        : m_storage(capacity),
          m_capacity(capacity) {
        if (capacity == 0U) {
            throw std::invalid_argument("BoundedQueue capacity must be greater than zero");
        }
    }

    ~BoundedQueue() {
        close();
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    bool tryPush(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed || m_size == m_capacity) {
            return false;
        }

        m_storage[m_tail].emplace(std::move(value));
        m_tail = nextIndex(m_tail);
        ++m_size;
        m_notEmpty.notify_one();
        return true;
    }

    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_size == 0U) {
            return std::nullopt;
        }

        std::optional<T> result(std::move(*m_storage[m_head]));
        m_storage[m_head].reset();
        m_head = nextIndex(m_head);
        --m_size;
        m_notFull.notify_one();
        return result;
    }

    std::vector<T> tryPopBatch(std::size_t maxCount) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (maxCount == 0U || m_size == 0U) {
            return {};
        }

        const std::size_t count = std::min(maxCount, m_size);
        std::vector<T> result;
        result.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            result.emplace_back(std::move(*m_storage[m_head]));
            m_storage[m_head].reset();
            m_head = nextIndex(m_head);
            --m_size;
        }
        m_notFull.notify_all();
        return result;
    }

    void close() noexcept {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

private:
    std::size_t nextIndex(std::size_t index) const noexcept {
        ++index;
        return index == m_capacity ? 0U : index;
    }

    std::vector<std::optional<T>> m_storage;
    const std::size_t m_capacity;
    std::size_t m_head = 0U;
    std::size_t m_tail = 0U;
    std::size_t m_size = 0U;
    bool m_closed = false;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
};

} // namespace dzc::tasks