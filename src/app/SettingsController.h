#pragma once

#include "ApplicationConfig.h"

#include <dzc/Result.h>

#include <QString>

namespace dzc {

class SettingsController final {
public:
    // Loads AppConfig from an explicit UTF-16/UTF-8-compatible INI path.
    static Result<SettingsLoadResult> load(const QString& iniPath);

    // Saves the supported AppConfig fields to an explicit INI path.
    static Result<void> save(const QString& iniPath, const AppConfig& config);

    // Loads settings and applies only explicitly supplied command-line options.
    static Result<SettingsLoadResult> loadWithCommandLine(
        const QString& iniPath,
        int argc,
        const char* const* argv);
};

} // namespace dzc