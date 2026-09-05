#pragma once

#include "ApplicationConfig.h"

#include <dzc/Result.h>

#include <QString>

namespace dzc {

class SettingsController final {
public:
    // Loads AppConfig from an explicit UTF-16/UTF-8-compatible INI path.
    static Result<SettingsLoadResult> load(const QString& iniPath);

    // Returns the standard per-user INI path for Dzc-RenderEngine.
    static QString standardPath();

    // Loads AppConfig from the standard per-user INI path.
    static Result<SettingsLoadResult> loadStandard();

    // Saves the supported AppConfig fields to an explicit INI path.
    static Result<void> save(const QString& iniPath, const AppConfig& config);

    // Saves AppConfig to the standard per-user INI path.
    static Result<void> saveStandard(const AppConfig& config);

    // Loads settings and applies only explicitly supplied command-line options.
    static Result<SettingsLoadResult> loadWithCommandLine(
        const QString& iniPath,
        int argc,
        const char* const* argv);
};

} // namespace dzc