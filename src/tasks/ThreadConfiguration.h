#pragma once

#include <cstdint>

#include <dzc/EngineConfig.h>

namespace dzc::tasks {

struct ResolvedThreadConfig final {
    std::uint32_t phase1WorkerThreads = 0U;
    std::uint32_t phase2RecordingThreads = 0U;
    std::uint32_t maxConcurrentIoTasks = 2U;
};

class ThreadConfiguration final {
public:
    static ResolvedThreadConfig resolve(
        const dzc::ThreadConfig& requested,
        std::uint32_t hardwareConcurrency) noexcept;
};

} // namespace dzc::tasks