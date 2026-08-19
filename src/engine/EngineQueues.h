#pragma once

#include <dzc/EngineEvent.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace dzc {

// Private Engine event queue policy. It preserves a recoverable loss event
// when a critical event cannot enter the bounded FIFO queue.
class EngineEventQueue final {
public:
    explicit EngineEventQueue(std::size_t capacity);
    ~EngineEventQueue();

    EngineEventQueue(const EngineEventQueue&) = delete;
    EngineEventQueue& operator=(const EngineEventQueue&) = delete;
    EngineEventQueue(EngineEventQueue&&) = delete;
    EngineEventQueue& operator=(EngineEventQueue&&) = delete;

    // Returns true when the event was accepted or progress was coalesced.
    // A false result means the event was dropped or the queue is closed.
    bool tryPush(EngineEvent event);
    std::vector<EngineEvent> poll();
    void close() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
