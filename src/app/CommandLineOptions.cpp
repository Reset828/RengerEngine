#include "CommandLineOptions.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueErrorCode = 1U;

Result<CommandLineOptions::Parsed> invalidOptions(
    std::string_view programName,
    std::string reason) {
    const std::string usageText = CommandLineOptions::usage(programName);
    return Result<CommandLineOptions::Parsed>::failure(Error{
        ErrorDomain::Configuration,
        kInvalidValueErrorCode,
        "Invalid command-line configuration: " + reason,
        reason + "\n\n" + usageText,
        "CommandLineOptions::parse"});
}

Result<std::uint64_t> parseUnsignedDecimal(
    std::string_view value,
    std::string_view optionName,
    std::string_view programName) {
    if (value.empty()) {
        return Result<std::uint64_t>::failure(Error{
            ErrorDomain::Configuration,
            kInvalidValueErrorCode,
            "Invalid command-line configuration: " + std::string(optionName) +
                " requires a non-empty decimal byte value",
            std::string(optionName) + " has an empty value\n\n" +
                CommandLineOptions::usage(programName),
            "CommandLineOptions::parse"});
    }

    std::uint64_t parsed = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            const std::string reason =
                std::string(optionName) + " must be an unsigned decimal byte value";
            return Result<std::uint64_t>::failure(Error{
                ErrorDomain::Configuration,
                kInvalidValueErrorCode,
                "Invalid command-line configuration: " + reason,
                reason + " (received '" + std::string(value) + "')\n\n" +
                    CommandLineOptions::usage(programName),
                "CommandLineOptions::parse"});
        }

        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            const std::string reason =
                std::string(optionName) + " is outside the uint64 byte range";
            return Result<std::uint64_t>::failure(Error{
                ErrorDomain::Configuration,
                kInvalidValueErrorCode,
                "Invalid command-line configuration: " + reason,
                reason + " (received '" + std::string(value) + "')\n\n" +
                    CommandLineOptions::usage(programName),
                "CommandLineOptions::parse"});
        }
        parsed = parsed * 10U + digit;
    }

    return Result<std::uint64_t>::success(parsed);
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
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
    const std::string programName =
        argc > 0 && argv != nullptr && argv[0] != nullptr ? argv[0] : "dzc_app";

    if (argc < 0 || (argc > 0 && argv == nullptr)) {
        return invalidOptions(programName, "invalid argument array");
    }

    Parsed parsed;
    bool backendSeen = false;
    bool cudaSeen = false;
    bool logLevelSeen = false;
    bool cacheDirectorySeen = false;
    bool gpuBudgetSeen = false;
    bool cpuBudgetSeen = false;

    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return invalidOptions(programName, "argument is null");
        }

        const std::string_view argument(argv[index]);
        if (!startsWith(argument, "--")) {
            return invalidOptions(
                programName,
                "positional arguments are not supported: " + std::string(argument));
        }

        const std::size_t equalsPosition = argument.find('=');
        if (equalsPosition == std::string_view::npos || equalsPosition <= 2U) {
            return invalidOptions(
                programName,
                "option must use --name=value syntax: " + std::string(argument));
        }

        const std::string_view name = argument.substr(2U, equalsPosition - 2U);
        const std::string_view value = argument.substr(equalsPosition + 1U);
        if (value.empty()) {
            return invalidOptions(
                programName,
                "option requires a non-empty value: " + std::string(name));
        }

        if (name == "backend") {
            if (backendSeen) {
                return invalidOptions(programName, "duplicate option: --backend");
            }
            backendSeen = true;
            if (value == "opengl") {
                parsed.engineConfig.backend = RenderBackendType::OpenGL;
            } else if (value == "vulkan") {
                parsed.engineConfig.backend = RenderBackendType::Vulkan;
            } else {
                return invalidOptions(
                    programName,
                    "--backend accepts only opengl or vulkan");
            }
        } else if (name == "cuda") {
            if (cudaSeen) {
                return invalidOptions(programName, "duplicate option: --cuda");
            }
            cudaSeen = true;
            if (value == "off") {
                parsed.engineConfig.cudaMode = OptionalFeatureMode::Off;
            } else if (value == "on") {
                parsed.engineConfig.cudaMode = OptionalFeatureMode::On;
            } else if (value == "auto") {
                parsed.engineConfig.cudaMode = OptionalFeatureMode::Auto;
            } else {
                return invalidOptions(
                    programName,
                    "--cuda accepts only on, off, or auto");
            }
        } else if (name == "log-level") {
            if (logLevelSeen) {
                return invalidOptions(programName, "duplicate option: --log-level");
            }
            logLevelSeen = true;
            if (value == "trace") {
                parsed.logLevel = diagnostics::LogLevel::Trace;
            } else if (value == "debug") {
                parsed.logLevel = diagnostics::LogLevel::Debug;
            } else if (value == "info") {
                parsed.logLevel = diagnostics::LogLevel::Info;
            } else if (value == "warn") {
                parsed.logLevel = diagnostics::LogLevel::Warn;
            } else if (value == "error") {
                parsed.logLevel = diagnostics::LogLevel::Error;
            } else {
                return invalidOptions(
                    programName,
                    "--log-level accepts only trace, debug, info, warn, or error");
            }
        } else if (name == "cache-directory") {
            if (cacheDirectorySeen) {
                return invalidOptions(programName, "duplicate option: --cache-directory");
            }
            cacheDirectorySeen = true;
            parsed.engineConfig.cache.directory = std::string(value);
        } else if (name == "gpu-memory-budget") {
            if (gpuBudgetSeen) {
                return invalidOptions(programName, "duplicate option: --gpu-memory-budget");
            }
            gpuBudgetSeen = true;
            const Result<std::uint64_t> budget =
                parseUnsignedDecimal(value, "--gpu-memory-budget", programName);
            if (!budget.hasValue()) {
                return Result<Parsed>::failure(budget.error());
            }
            parsed.engineConfig.memory.gpuCacheBytes = budget.value();
        } else if (name == "cpu-cache-budget") {
            if (cpuBudgetSeen) {
                return invalidOptions(programName, "duplicate option: --cpu-cache-budget");
            }
            cpuBudgetSeen = true;
            const Result<std::uint64_t> budget =
                parseUnsignedDecimal(value, "--cpu-cache-budget", programName);
            if (!budget.hasValue()) {
                return Result<Parsed>::failure(budget.error());
            }
            parsed.engineConfig.memory.cpuCacheBytes = budget.value();
        } else {
            return invalidOptions(programName, "unknown option: --" + std::string(name));
        }
    }

    return Result<Parsed>::success(std::move(parsed));
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