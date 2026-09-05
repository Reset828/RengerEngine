#pragma once

#include "../diagnostics/LogTypes.h"

#include <dzc/EngineConfig.h>
#include <dzc/EngineTypes.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dzc {

struct AppConfig final {
    EngineConfig engineConfig;
    diagnostics::LogLevel logLevel{diagnostics::LogLevel::Info};
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    ColorRgba backgroundColor;
};

struct AppConfigOverrides final {
    std::optional<RenderBackendType> backend;
    std::optional<OptionalFeatureMode> cudaMode;
    std::optional<diagnostics::LogLevel> logLevel;
    std::optional<std::string> cacheDirectory;
    std::optional<std::uint64_t> gpuMemoryBudget;
    std::optional<std::uint64_t> cpuCacheBudget;
};

struct SettingsLoadResult final {
    AppConfig config;
    std::vector<std::string> warnings;
};

} // namespace dzc