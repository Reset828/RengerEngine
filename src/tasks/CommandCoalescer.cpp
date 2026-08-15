#include "tasks/CommandCoalescer.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dzc::tasks {

class CommandCoalescer::Impl final {
public:
    explicit Impl(std::size_t capacityIn)
        : storage(capacityIn),
          capacity(capacityIn) {}

    std::size_t nextIndex(std::size_t index) const noexcept {
        ++index;
        return index == capacity ? 0U : index;
    }

    std::size_t previousIndex(std::size_t index) const noexcept {
        return index == 0U ? capacity - 1U : index - 1U;
    }

    static bool isCoalescable(const EngineCommand& command) noexcept {
        return std::holds_alternative<SetPointSizeCommand>(command) ||
               std::holds_alternative<SetShadingModeCommand>(command) ||
               std::holds_alternative<SetFixedColorCommand>(command) ||
               std::holds_alternative<SetCudaModeCommand>(command) ||
               std::holds_alternative<ResizeCommand>(command);
    }

    bool replaceInCurrentSegment(EngineCommand&& command) {
        if (!isCoalescable(command)) {
            return false;
        }

        std::size_t index = tail;
        for (std::size_t inspected = 0U; inspected < size; ++inspected) {
            index = previousIndex(index);
            const EngineCommand& existing = *storage[index];
            if (!isCoalescable(existing)) {
                return false;
            }

            if (existing.index() == command.index()) {
                storage[index] = std::move(command);
                return true;
            }
        }

        return false;
    }

    std::vector<std::optional<EngineCommand>> storage;
    const std::size_t capacity;
    std::size_t head = 0U;
    std::size_t tail = 0U;
    std::size_t size = 0U;
    bool closed = false;
    std::mutex mutex;
};

CommandCoalescer::CommandCoalescer(std::size_t capacity) {
    if (capacity == 0U) {
        throw std::invalid_argument("CommandCoalescer capacity must be greater than zero");
    }

    m_impl = std::make_unique<Impl>(capacity);
}

CommandCoalescer::~CommandCoalescer() {
    close();
}

bool CommandCoalescer::push(EngineCommand command) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->closed) {
        return false;
    }

    if (m_impl->replaceInCurrentSegment(std::move(command))) {
        return true;
    }

    if (m_impl->size == m_impl->capacity) {
        return false;
    }

    m_impl->storage[m_impl->tail].emplace(std::move(command));
    m_impl->tail = m_impl->nextIndex(m_impl->tail);
    ++m_impl->size;
    return true;
}

std::optional<EngineCommand> CommandCoalescer::pop() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->size == 0U) {
        return std::nullopt;
    }

    std::optional<EngineCommand> result(std::move(*m_impl->storage[m_impl->head]));
    m_impl->storage[m_impl->head].reset();
    m_impl->head = m_impl->nextIndex(m_impl->head);
    --m_impl->size;
    return result;
}

std::vector<EngineCommand> CommandCoalescer::popBatch(std::size_t maxCount) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (maxCount == 0U || m_impl->size == 0U) {
        return {};
    }

    const std::size_t count = std::min(maxCount, m_impl->size);
    std::vector<EngineCommand> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.emplace_back(std::move(*m_impl->storage[m_impl->head]));
        m_impl->storage[m_impl->head].reset();
        m_impl->head = m_impl->nextIndex(m_impl->head);
        --m_impl->size;
    }
    return result;
}

void CommandCoalescer::close() noexcept {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->closed = true;
}

} // namespace dzc::tasks