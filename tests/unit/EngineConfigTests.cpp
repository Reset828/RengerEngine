#include <dzc/EngineConfig.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace {

void testEnumValues() {
    static_assert(std::is_same_v<std::underlying_type_t<dzc::RenderBackendType>, std::uint8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<dzc::OptionalFeatureMode>, std::uint8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<dzc::ShadingMode>, std::uint8_t>);

    assert(dzc::RenderBackendType::OpenGL != dzc::RenderBackendType::Vulkan);
    assert(dzc::OptionalFeatureMode::Off != dzc::OptionalFeatureMode::On);
    assert(dzc::OptionalFeatureMode::On != dzc::OptionalFeatureMode::Auto);
    assert(dzc::ShadingMode::OriginalColor != dzc::ShadingMode::FixedColor);
    assert(dzc::ShadingMode::Height != dzc::ShadingMode::Intensity);
}

void testDefaultValues() {
    const dzc::ThreadConfig threads;
    assert(threads.workerThreads == 0);
    assert(threads.commandRecordingThreads == 0);
    assert(threads.maxConcurrentIoTasks == 2);

    const dzc::MemoryBudgetConfig memory;
    assert(memory.cpuCacheBytes == 0);
    assert(memory.gpuCacheBytes == 0);

    const dzc::CacheConfig cache;
    assert(cache.enabled);
    assert(cache.directory.empty());

    const dzc::EngineConfig config;
    assert(config.backend == dzc::RenderBackendType::OpenGL);
    assert(config.cudaMode == dzc::OptionalFeatureMode::Auto);
    assert(config.threads.workerThreads == 0);
    assert(config.threads.commandRecordingThreads == 0);
    assert(config.threads.maxConcurrentIoTasks == 2);
    assert(config.memory.cpuCacheBytes == 0);
    assert(config.memory.gpuCacheBytes == 0);
    assert(config.cache.enabled);
    assert(config.cache.directory.empty());
    assert(config.commandQueueCapacity == 1024);
    assert(config.eventQueueCapacity == 1024);
    assert(config.hasValidQueueCapacities());
}

void testAutomaticValueSemantics() {
    const dzc::EngineConfig config;
    assert(config.threads.workerThreads == 0);
    assert(config.threads.commandRecordingThreads == 0);
    assert(config.memory.cpuCacheBytes == 0);
    assert(config.memory.gpuCacheBytes == 0);
}

void testQueueCapacityValidation() {
    dzc::EngineConfig config;
    config.commandQueueCapacity = 0;
    assert(!config.hasValidQueueCapacities());

    config.commandQueueCapacity = 1;
    config.eventQueueCapacity = 0;
    assert(!config.hasValidQueueCapacities());

    config.eventQueueCapacity = 1;
    assert(config.hasValidQueueCapacities());
}

void testPublicTypeBoundaries() {
    static_assert(std::is_trivially_copyable_v<dzc::ThreadConfig>);
    static_assert(std::is_trivially_copyable_v<dzc::MemoryBudgetConfig>);
}

} // namespace

int main() {
    testEnumValues();
    testDefaultValues();
    testAutomaticValueSemantics();
    testQueueCapacityValidation();
    testPublicTypeBoundaries();
    return 0;
}