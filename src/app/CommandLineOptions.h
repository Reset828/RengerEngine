#pragma once

#include "../diagnostics/LogTypes.h"

#include <dzc/EngineConfig.h>
#include <dzc/Result.h>

#include <string>
#include <string_view>

namespace dzc {

class CommandLineOptions final {
public:
    struct Parsed final {
        EngineConfig engineConfig;
        diagnostics::LogLevel logLevel{diagnostics::LogLevel::Info};
    };

    // Parses supported command-line options into application configuration.
    static Result<EngineConfig> parse(int argc, const char* const* argv);

    // Parses supported options while retaining the application log level.
    static Result<Parsed> parseOptions(int argc, const char* const* argv);

    // Returns usage text for the command-line interface.
    static std::string usage(std::string_view programName);
};

} // namespace dzc