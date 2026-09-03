#include "app/CommandLineOptions.h"

#include <dzc/EngineConfig.h>
#include <dzc/Error.h>

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace {

using dzc::CommandLineOptions;

struct Arguments final {
    explicit Arguments(std::initializer_list<const char*> values) : values(values) {
        pointers.reserve(this->values.size());
        for (const char* value : this->values) {
            pointers.push_back(value);
        }
    }

    int argc() const { return static_cast<int>(pointers.size()); }
    const char* const* argv() const { return pointers.data(); }

    std::vector<const char*> values;
    std::vector<const char*> pointers;
};

CommandLineOptions::Parsed parseOptions(std::initializer_list<const char*> values) {
    const Arguments arguments(values);
    const auto result = CommandLineOptions::parseOptions(arguments.argc(), arguments.argv());
    assert(result.hasValue());
    return result.value();
}

dzc::EngineConfig parseConfig(std::initializer_list<const char*> values) {
    const Arguments arguments(values);
    const auto result = CommandLineOptions::parse(arguments.argc(), arguments.argv());
    assert(result.hasValue());
    return result.value();
}

void assertInvalid(std::initializer_list<const char*> values, const char* expectedText) {
    const Arguments arguments(values);
    const auto result = CommandLineOptions::parseOptions(arguments.argc(), arguments.argv());
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Configuration);
    assert(result.error().code == 1U);
    assert(result.error().userMessage.find(expectedText) != std::string::npos);
    assert(result.error().diagnosticMessage.find("Usage:") != std::string::npos);
    assert(result.error().context == "CommandLineOptions::parse");
}

void testDefaults() {
    const auto parsed = parseOptions({"dzc_app"});
    const auto& config = parsed.engineConfig;
    assert(config.backend == dzc::RenderBackendType::OpenGL);
    assert(config.cudaMode == dzc::OptionalFeatureMode::Auto);
    assert(parsed.logLevel == dzc::diagnostics::LogLevel::Info);
    assert(config.threads.workerThreads == 0U);
    assert(config.threads.commandRecordingThreads == 0U);
    assert(config.threads.maxConcurrentIoTasks == 2U);
    assert(config.memory.cpuCacheBytes == 0U);
    assert(config.memory.gpuCacheBytes == 0U);
    assert(config.cache.enabled);
    assert(config.cache.directory.empty());
    assert(config.commandQueueCapacity == 1024U);
    assert(config.eventQueueCapacity == 1024U);
}

void testAllSupportedOptions() {
    const auto parsed = parseOptions({
        "dzc_app",
        "--backend=vulkan",
        "--cuda=off",
        "--log-level=debug",
        "--cache-directory=C:/数据/cache",
        "--gpu-memory-budget=18446744073709551615",
        "--cpu-cache-budget=4096"});
    const auto& config = parsed.engineConfig;
    assert(config.backend == dzc::RenderBackendType::Vulkan);
    assert(config.cudaMode == dzc::OptionalFeatureMode::Off);
    assert(parsed.logLevel == dzc::diagnostics::LogLevel::Debug);
    assert(config.cache.directory == "C:/数据/cache");
    assert(config.memory.gpuCacheBytes == std::numeric_limits<std::uint64_t>::max());
    assert(config.memory.cpuCacheBytes == 4096U);
    assert(config.cache.enabled);
}

void testBackendAndCudaValues() {
    assert(parseConfig({"dzc_app", "--backend=opengl"}).backend ==
           dzc::RenderBackendType::OpenGL);
    assert(parseConfig({"dzc_app", "--backend=vulkan"}).backend ==
           dzc::RenderBackendType::Vulkan);
    assert(parseConfig({"dzc_app", "--cuda=on"}).cudaMode ==
           dzc::OptionalFeatureMode::On);
    assert(parseConfig({"dzc_app", "--cuda=off"}).cudaMode ==
           dzc::OptionalFeatureMode::Off);
    assert(parseConfig({"dzc_app", "--cuda=auto"}).cudaMode ==
           dzc::OptionalFeatureMode::Auto);
}

void testLogLevels() {
    assert(parseOptions({"dzc_app", "--log-level=trace"}).logLevel ==
           dzc::diagnostics::LogLevel::Trace);
    assert(parseOptions({"dzc_app", "--log-level=debug"}).logLevel ==
           dzc::diagnostics::LogLevel::Debug);
    assert(parseOptions({"dzc_app", "--log-level=info"}).logLevel ==
           dzc::diagnostics::LogLevel::Info);
    assert(parseOptions({"dzc_app", "--log-level=warn"}).logLevel ==
           dzc::diagnostics::LogLevel::Warn);
    assert(parseOptions({"dzc_app", "--log-level=error"}).logLevel ==
           dzc::diagnostics::LogLevel::Error);
}

void testZeroBudgets() {
    const auto config = parseConfig({
        "dzc_app",
        "--cpu-cache-budget=0",
        "--gpu-memory-budget=0"});
    assert(config.memory.cpuCacheBytes == 0U);
    assert(config.memory.gpuCacheBytes == 0U);
}

void testUsage() {
    const auto text = CommandLineOptions::usage("sample.exe");
    assert(text.find("Usage: sample.exe [options]") != std::string::npos);
    assert(text.find("--backend=opengl|vulkan") != std::string::npos);
    assert(text.find("--log-level=trace|debug|info|warn|error") != std::string::npos);
}

void testInvalidValues() {
    assertInvalid({"dzc_app", "--backend=metal"}, "--backend");
    assertInvalid({"dzc_app", "--cuda=maybe"}, "--cuda");
    assertInvalid({"dzc_app", "--log-level=NOTICE"}, "--log-level");
    assertInvalid({"dzc_app", "--cpu-cache-budget="}, "requires a non-empty value");
    assertInvalid({"dzc_app", "--cpu-cache-budget=-1"}, "unsigned decimal");
    assertInvalid({"dzc_app", "--gpu-memory-budget=12MiB"}, "unsigned decimal");
    assertInvalid({"dzc_app", "--gpu-memory-budget=18446744073709551616"}, "uint64");
    assertInvalid({"dzc_app", "--cache-directory="}, "requires a non-empty value");
    assertInvalid({"dzc_app", "--backend"}, "--name=value");
    assertInvalid({"dzc_app", "--unknown=value"}, "unknown option");
    assertInvalid({"dzc_app", "input.pcd"}, "positional arguments");
}

void testDuplicateOptions() {
    assertInvalid({"dzc_app", "--backend=opengl", "--backend=vulkan"}, "duplicate option");
    assertInvalid({"dzc_app", "--cuda=auto", "--cuda=off"}, "duplicate option");
    assertInvalid({"dzc_app", "--log-level=info", "--log-level=error"}, "duplicate option");
    assertInvalid({"dzc_app", "--cache-directory=a", "--cache-directory=b"}, "duplicate option");
    assertInvalid({"dzc_app", "--cpu-cache-budget=1", "--cpu-cache-budget=2"}, "duplicate option");
    assertInvalid({"dzc_app", "--gpu-memory-budget=1", "--gpu-memory-budget=2"}, "duplicate option");
}

} // namespace

int main() {
    testDefaults();
    testAllSupportedOptions();
    testBackendAndCudaValues();
    testLogLevels();
    testZeroBudgets();
    testUsage();
    testInvalidValues();
    testDuplicateOptions();
    return 0;
}