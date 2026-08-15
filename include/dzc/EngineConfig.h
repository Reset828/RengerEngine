#pragma once

#include <cstdint>
#include <string>

namespace dzc {

enum class RenderBackendType : std::uint8_t {
    OpenGL,
    Vulkan
};

enum class OptionalFeatureMode : std::uint8_t {
    Off,
    On,
    Auto
};

enum class ShadingMode : std::uint8_t {
    OriginalColor,
    FixedColor,
    Height,
    Intensity
};

struct ThreadConfig final {
    std::uint32_t workerThreads{0};
    std::uint32_t commandRecordingThreads{0};
    std::uint32_t maxConcurrentIoTasks{2};
};

struct MemoryBudgetConfig final {
    std::uint64_t cpuCacheBytes{0};
    std::uint64_t gpuCacheBytes{0};
};

struct CacheConfig final {
    bool enabled{true};
    std::string directory;
};

struct EngineConfig final {
    RenderBackendType backend{RenderBackendType::OpenGL};
    OptionalFeatureMode cudaMode{OptionalFeatureMode::Auto};
    ThreadConfig threads;
    MemoryBudgetConfig memory;
    CacheConfig cache;
    std::uint32_t commandQueueCapacity{1024};
    std::uint32_t eventQueueCapacity{1024};

    // Returns whether both bounded queue capacities are nonzero.
    bool hasValidQueueCapacities() const noexcept {
        return commandQueueCapacity > 0 && eventQueueCapacity > 0;
    }
};

} // namespace dzc