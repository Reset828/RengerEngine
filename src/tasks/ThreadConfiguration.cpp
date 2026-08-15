#include "tasks/ThreadConfiguration.h"

#include <algorithm>

namespace dzc::tasks {
namespace {

constexpr std::uint32_t kFallbackHardwareConcurrency = 4U;
constexpr std::uint32_t kMinimumAutomaticThreads = 2U;
constexpr std::uint32_t kMinimumConfiguredThreads = 1U;
constexpr std::uint32_t kMaximumThreads = 8U;
constexpr std::uint32_t kDefaultIoTasks = 2U;

std::uint32_t clampAutomatic(std::uint32_t value) noexcept {
    return std::min(
        std::max(value, kMinimumAutomaticThreads),
        kMaximumThreads);
}

std::uint32_t clampConfigured(std::uint32_t value) noexcept {
    return std::min(
        std::max(value, kMinimumConfiguredThreads),
        kMaximumThreads);
}

} // namespace

ResolvedThreadConfig ThreadConfiguration::resolve(
    const dzc::ThreadConfig& requested,
    std::uint32_t hardwareConcurrency) noexcept {
    const std::uint32_t effectiveHardwareConcurrency =
        hardwareConcurrency == 0U ? kFallbackHardwareConcurrency : hardwareConcurrency;

    const std::uint32_t automaticPhase1 = clampAutomatic(
        effectiveHardwareConcurrency - 1U);
    const std::uint32_t automaticPhase2 = clampAutomatic(
        effectiveHardwareConcurrency / 2U);

    ResolvedThreadConfig resolved;
    resolved.phase1WorkerThreads = requested.workerThreads == 0U
        ? automaticPhase1
        : clampConfigured(requested.workerThreads);
    resolved.phase2RecordingThreads = requested.commandRecordingThreads == 0U
        ? automaticPhase2
        : clampConfigured(requested.commandRecordingThreads);
    resolved.maxConcurrentIoTasks = requested.maxConcurrentIoTasks == 0U
        ? kDefaultIoTasks
        : clampConfigured(requested.maxConcurrentIoTasks);
    return resolved;
}

} // namespace dzc::tasks