#include "CommandLineOptions.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueErrorCode = 1U;

Error invalidError(std::string_view programName, std::string reason) {
    const std::string usageText = CommandLineOptions::usage(programName);
    return Error{
        ErrorDomain::Configuration,
        kInvalidValueErrorCode,
        "Invalid command-line configuration: " + reason,
        reason + "\n\n" + usageText,
        "CommandLineOptions::parse"};
}

template <typename T>
Result<T> invalidOptions(std::string_view programName, std::string reason) {
    return Result<T>::failure(invalidError(programName, std::move(reason)));
}

Result<std::uint64_t> parseUnsignedDecimal(
    std::string_view value,
    std::string_view optionName,
    std::string_view programName) {
    if (value.empty()) {
        return Result<std::uint64_t>::failure(invalidError(
            programName,
            std::string(optionName) + " requires a non-empty decimal byte value"));
    }

    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            const std::string reason =
                std::string(optionName) + " must be an unsigned decimal byte value";
            return Result<std::uint64_t>::failure(invalidError(
                programName,
                reason + " (received '" + std::string(value) + "')"));
        }

        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            const std::string reason =
                std::string(optionName) + " is outside the uint64 byte range";
            return Result<std::uint64_t>::failure(invalidError(
                programName,
                reason + " (received '" + std::string(value) + "')"));
        }
        parsed = parsed * 10U + digit;
    }

    return Result<std::uint64_t>::success(parsed);
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

void applyOverrides(AppConfig& config, const AppConfigOverrides& overrides) {
    if (overrides.backend.has_value()) {
        config.engineConfig.backend = *overrides.backend;
    }
    if (overrides.cudaMode.has_value()) {
        config.engineConfig.cudaMode = *overrides.cudaMode;
    }
    if (overrides.logLevel.has_value()) {
        config.logLevel = *overrides.logLevel;
    }
    if (overrides.cacheDirectory.has_value()) {
        config.engineConfig.cache.directory = *overrides.cacheDirectory;
    }
    if (overrides.gpuMemoryBudget.has_value()) {
        config.engineConfig.memory.gpuCacheBytes = *overrides.gpuMemoryBudget;
    }
    if (overrides.cpuCacheBudget.has_value()) {
        config.engineConfig.memory.cpuCacheBytes = *overrides.cpuCacheBudget;
    }
}

} // namespace

Result<EngineConfig> CommandLineOptions::parse(int argc, const char* const* argv) {
    const Result<Parsed> parsed = parseOptions(argc, argv);
    if (!parsed.hasValue()) {
        return Result<EngineConfig>::failure(parsed.error());
    }
    return Result<EngineConfig>::success(parsed.value().engineConfig);
}

Result<CommandLineOptions::Parsed> CommandLineOptions::parseOptions(
    int argc,
    const char* const* argv) {
    const Result<AppConfigOverrides> overrides = parseOverrides(argc, argv);
    if (!overrides.hasValue()) {
        return Result<Parsed>::failure(overrides.error());
    }

    Parsed parsed;
    applyOverrides(parsed, overrides.value());
    return Result<Parsed>::success(std::move(parsed));
}

Result<AppConfigOverrides> CommandLineOptions::parseOverrides(
    int argc,
    const char* const* argv) {
    const std::string programName =
        argc > 0 && argv != nullptr && argv[0] != nullptr ? argv[0] : "dzc_app";

    if (argc < 0 || (argc > 0 && argv == nullptr)) {
        return invalidOptions<AppConfigOverrides>(programName, "invalid argument array");
    }

    AppConfigOverrides overrides;
    bool backendSeen = false;
    bool cudaSeen = false;
    bool logLevelSeen = false;
    bool cacheDirectorySeen = false;
    bool gpuBudgetSeen = false;
    bool cpuBudgetSeen = false;

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return invalidOptions<AppConfigOverrides>(programName, "argument is null");
        }

        const std::string_view argument(argv[index]);
        if (!startsWith(argument, "--")) {
            return invalidOptions<AppConfigOverrides>(
                programName,
                "positional arguments are not supported: " + std::string(argument));
        }

        const std::size_t equalsPosition = argument.find('=');
        if (equalsPosition == std::string_view::npos || equalsPosition <= 2U) {
            return invalidOptions<AppConfigOverrides>(
                programName,
                "option must use --name=value syntax: " + std::string(argument));
        }

        const std::string_view name = argument.substr(2U, equalsPosition - 2U);
        const std::string_view value = argument.substr(equalsPosition + 1U);
        if (value.empty()) {
            return invalidOptions<AppConfigOverrides>(
                programName,
                "option requires a non-empty value: " + std::string(name));
        }

        if (name == "backend") {
            if (backendSeen) {
                return invalidOptions<AppConfigOverrides>(programName, "duplicate option: --backend");
            }
            backendSeen = true;
            if (value == "opengl") {
                overrides.backend = RenderBackendType::OpenGL;
            } else if (value == "vulkan") {
                overrides.backend = RenderBackendType::Vulkan;
            } else {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "--backend accepts only opengl or vulkan");
            }
        } else if (name == "cuda") {
            if (cudaSeen) {
                return invalidOptions<AppConfigOverrides>(programName, "duplicate option: --cuda");
            }
            cudaSeen = true;
            if (value == "off") {
                overrides.cudaMode = OptionalFeatureMode::Off;
            } else if (value == "on") {
                overrides.cudaMode = OptionalFeatureMode::On;
            } else if (value == "auto") {
                overrides.cudaMode = OptionalFeatureMode::Auto;
            } else {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "--cuda accepts only on, off, or auto");
            }
        } else if (name == "log-level") {
            if (logLevelSeen) {
                return invalidOptions<AppConfigOverrides>(programName, "duplicate option: --log-level");
            }
            logLevelSeen = true;
            if (value == "trace") {
                overrides.logLevel = diagnostics::LogLevel::Trace;
            } else if (value == "debug") {
                overrides.logLevel = diagnostics::LogLevel::Debug;
            } else if (value == "info") {
                overrides.logLevel = diagnostics::LogLevel::Info;
            } else if (value == "warn") {
                overrides.logLevel = diagnostics::LogLevel::Warn;
            } else if (value == "error") {
                overrides.logLevel = diagnostics::LogLevel::Error;
            } else {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "--log-level accepts only trace, debug, info, warn, or error");
            }
        } else if (name == "cache-directory") {
            if (cacheDirectorySeen) {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "duplicate option: --cache-directory");
            }
            cacheDirectorySeen = true;
            overrides.cacheDirectory = std::string(value);
        } else if (name == "gpu-memory-budget") {
            if (gpuBudgetSeen) {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "duplicate option: --gpu-memory-budget");
            }
            gpuBudgetSeen = true;
            const Result<std::uint64_t> budget =
                parseUnsignedDecimal(value, "--gpu-memory-budget", programName);
            if (!budget.hasValue()) {
                return Result<AppConfigOverrides>::failure(budget.error());
            }
            overrides.gpuMemoryBudget = budget.value();
        } else if (name == "cpu-cache-budget") {
            if (cpuBudgetSeen) {
                return invalidOptions<AppConfigOverrides>(
                    programName,
                    "duplicate option: --cpu-cache-budget");
            }
            cpuBudgetSeen = true;
            const Result<std::uint64_t> budget =
                parseUnsignedDecimal(value, "--cpu-cache-budget", programName);
            if (!budget.hasValue()) {
                return Result<AppConfigOverrides>::failure(budget.error());
            }
            overrides.cpuCacheBudget = budget.value();
        } else {
            return invalidOptions<AppConfigOverrides>(
                programName,
                "unknown option: --" + std::string(name));
        }
    }

    return Result<AppConfigOverrides>::success(std::move(overrides));
}

std::string CommandLineOptions::usage(std::string_view programName) {
    const std::string program = programName.empty() ? "dzc_app" : std::string(programName);
    return "Usage: " + program + " [options]\n"
           "Options:\n"
           "  --backend=opengl|vulkan\n"
           "  --cuda=on|off|auto\n"
           "  --log-level=trace|debug|info|warn|error\n"
           "  --cache-directory=<UTF-8 path>\n"
           "  --gpu-memory-budget=<decimal bytes>\n"
           "  --cpu-cache-budget=<decimal bytes>";
}

} // namespace dzc
