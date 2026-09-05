#include "SettingsController.h"

#include "CommandLineOptions.h"

#include <QColor>
#include <QSettings>

#include <array>
#include <cmath>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kOpenFailedErrorCode = 1U;
constexpr std::uint32_t kWriteFailedErrorCode = 3U;

const char* backendName(RenderBackendType value) noexcept {
    return value == RenderBackendType::Vulkan ? "vulkan" : "opengl";
}

const char* cudaName(OptionalFeatureMode value) noexcept {
    switch (value) {
    case OptionalFeatureMode::Off:
        return "off";
    case OptionalFeatureMode::On:
        return "on";
    case OptionalFeatureMode::Auto:
        return "auto";
    }
    return "auto";
}

const char* appLogLevelName(diagnostics::LogLevel value) noexcept {
    switch (value) {
    case diagnostics::LogLevel::Trace:
        return "trace";
    case diagnostics::LogLevel::Debug:
        return "debug";
    case diagnostics::LogLevel::Info:
        return "info";
    case diagnostics::LogLevel::Warn:
        return "warn";
    case diagnostics::LogLevel::Error:
        return "error";
    }
    return "info";
}

const char* shadingName(ShadingMode value) noexcept {
    switch (value) {
    case ShadingMode::OriginalColor:
        return "original";
    case ShadingMode::FixedColor:
        return "fixed";
    case ShadingMode::Height:
        return "height";
    case ShadingMode::Intensity:
        return "intensity";
    }
    return "original";
}

std::optional<ShadingMode> parseShading(std::string_view value) {
    if (value == "original") {
        return ShadingMode::OriginalColor;
    }
    if (value == "fixed") {
        return ShadingMode::FixedColor;
    }
    if (value == "height") {
        return ShadingMode::Height;
    }
    if (value == "intensity") {
        return ShadingMode::Intensity;
    }
    return std::nullopt;
}

std::optional<ColorRgba> parseColor(const QString& value) {
    if (value.size() != 9 || !value.startsWith(QStringLiteral("#"))) {
        return std::nullopt;
    }
    const QColor color(value);
    if (!color.isValid()) {
        return std::nullopt;
    }
    return ColorRgba{
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()),
        static_cast<float>(color.alphaF())};
}
QString colorName(const ColorRgba& value) {
    QColor color;
    color.setRgbF(value.red, value.green, value.blue, value.alpha);
    return color.name(QColor::HexArgb).toUpper();
}

void addInvalidWarning(
    SettingsLoadResult& result,
    const char* key,
    const QString& value,
    std::string_view expected);
std::string toUtf8(const QString& value);

void loadRenderSettings(QSettings& settings, SettingsLoadResult& result) {
    if (settings.contains("render/pointSize")) {
        const QString value = settings.value("render/pointSize").toString();
        bool ok = false;
        const double parsed = value.toDouble(&ok);
        if (!ok || !std::isfinite(parsed) || parsed < 1.0 || parsed > 64.0) {
            addInvalidWarning(result, "render/pointSize", value, "a finite value in [1, 64]");
        } else {
            result.config.pointSize = static_cast<float>(parsed);
        }
    }

    if (settings.contains("render/shadingMode")) {
        const QString value = settings.value("render/shadingMode").toString();
        const auto parsed = parseShading(toUtf8(value));
        if (!parsed.has_value()) {
            addInvalidWarning(result, "render/shadingMode", value, "original, fixed, height, or intensity");
        } else {
            result.config.shadingMode = *parsed;
        }
    }

    const std::array<std::pair<const char*, ColorRgba*>, 2> colors{{
        {"render/fixedColor", &result.config.fixedColor},
        {"render/backgroundColor", &result.config.backgroundColor}}};
    for (const auto& entry : colors) {
        if (!settings.contains(entry.first)) {
            continue;
        }
        const QString value = settings.value(entry.first).toString();
        const auto parsed = parseColor(value);
        if (!parsed.has_value()) {
            addInvalidWarning(result, entry.first, value, "an RGBA color in #AARRGGBB format");
        } else {
            *entry.second = *parsed;
        }
    }
}

std::optional<RenderBackendType> parseBackend(std::string_view value) {
    if (value == "opengl") {
        return RenderBackendType::OpenGL;
    }
    if (value == "vulkan") {
        return RenderBackendType::Vulkan;
    }
    return std::nullopt;
}

std::optional<OptionalFeatureMode> parseCuda(std::string_view value) {
    if (value == "off") {
        return OptionalFeatureMode::Off;
    }
    if (value == "on") {
        return OptionalFeatureMode::On;
    }
    if (value == "auto") {
        return OptionalFeatureMode::Auto;
    }
    return std::nullopt;
}

std::optional<diagnostics::LogLevel> parseLogLevel(std::string_view value) {
    if (value == "trace") {
        return diagnostics::LogLevel::Trace;
    }
    if (value == "debug") {
        return diagnostics::LogLevel::Debug;
    }
    if (value == "info") {
        return diagnostics::LogLevel::Info;
    }
    if (value == "warn") {
        return diagnostics::LogLevel::Warn;
    }
    if (value == "error") {
        return diagnostics::LogLevel::Error;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> parseUnsignedDecimal(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t result = 0;
    for (const char character : value) {
        if (character < '0' || character > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return std::nullopt;
        }
        result = result * 10U + digit;
    }
    return result;
}

std::string toUtf8(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

Error fileError(
    const QString& iniPath,
    std::uint32_t code,
    std::string operation,
    QSettings::Status status) {
    const std::string path = toUtf8(iniPath);
    const std::string reason = operation + " failed for INI file '" + path +
        "' (QSettings status=" + std::to_string(static_cast<int>(status)) + ")";
    return Error{
        ErrorDomain::FileIo,
        code,
        reason,
        reason,
        operation == "load" ? "SettingsController::load" : "SettingsController::save"};
}

void addInvalidWarning(
    SettingsLoadResult& result,
    const char* key,
    const QString& value,
    std::string_view expected) {
    result.warnings.push_back(
        std::string("invalid INI value for ") + key + ": expected " +
        std::string(expected) + ", received '" + toUtf8(value) + "'; using default");
}

void loadBudget(
    QSettings& settings,
    SettingsLoadResult& result,
    const char* key,
    std::uint64_t& destination) {
    if (!settings.contains(key)) {
        return;
    }
    const QString value = settings.value(key).toString();
    const std::string utf8 = toUtf8(value);
    const auto parsed = parseUnsignedDecimal(utf8);
    if (!parsed.has_value()) {
        addInvalidWarning(result, key, value, "an unsigned decimal uint64 byte value");
        return;
    }
    destination = *parsed;
}

void applyCommandLineOverrides(AppConfig& config, const AppConfigOverrides& overrides) {
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

Result<SettingsLoadResult> SettingsController::load(const QString& iniPath) {
    if (iniPath.isEmpty()) {
        return Result<SettingsLoadResult>::failure(
            fileError(iniPath, kOpenFailedErrorCode, "load", QSettings::AccessError));
    }

    QSettings settings(iniPath, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        return Result<SettingsLoadResult>::failure(
            fileError(iniPath, kOpenFailedErrorCode, "load", settings.status()));
    }

    SettingsLoadResult result;
    const QString backendValue = settings.value("engine/backend").toString();
    if (settings.contains("engine/backend")) {
        const auto parsed = parseBackend(toUtf8(backendValue));
        if (parsed.has_value()) {
            result.config.engineConfig.backend = *parsed;
        } else {
            addInvalidWarning(result, "engine/backend", backendValue, "opengl or vulkan");
        }
    }

    const QString cudaValue = settings.value("engine/cuda").toString();
    if (settings.contains("engine/cuda")) {
        const auto parsed = parseCuda(toUtf8(cudaValue));
        if (parsed.has_value()) {
            result.config.engineConfig.cudaMode = *parsed;
        } else {
            addInvalidWarning(result, "engine/cuda", cudaValue, "off, on, or auto");
        }
    }

    const QString logLevelValue = settings.value("engine/logLevel").toString();
    if (settings.contains("engine/logLevel")) {
        const auto parsed = parseLogLevel(toUtf8(logLevelValue));
        if (parsed.has_value()) {
            result.config.logLevel = *parsed;
        } else {
            addInvalidWarning(
                result,
                "engine/logLevel",
                logLevelValue,
                "trace, debug, info, warn, or error");
        }
    }

    if (settings.contains("cache/directory")) {
        result.config.engineConfig.cache.directory = toUtf8(settings.value("cache/directory").toString());
    }

    loadBudget(
        settings,
        result,
        "memory/gpuCacheBytes",
        result.config.engineConfig.memory.gpuCacheBytes);
    loadBudget(
        settings,
        result,
        "memory/cpuCacheBytes",
        result.config.engineConfig.memory.cpuCacheBytes);

    loadRenderSettings(settings, result);

    return Result<SettingsLoadResult>::success(std::move(result));
}

QString SettingsController::standardPath() {
    const QSettings settings(
        QSettings::IniFormat,
        QSettings::UserScope,
        QStringLiteral("Dzc"),
        QStringLiteral("Dzc-RenderEngine"));
    return settings.fileName();
}

Result<SettingsLoadResult> SettingsController::loadStandard() {
    return load(standardPath());
}

Result<void> SettingsController::saveStandard(const AppConfig& config) {
    return save(standardPath(), config);
}

Result<void> SettingsController::save(const QString& iniPath, const AppConfig& config) {
    if (iniPath.isEmpty()) {
        return Result<void>::failure(
            fileError(iniPath, kWriteFailedErrorCode, "save", QSettings::AccessError));
    }

    QSettings settings(iniPath, QSettings::IniFormat);
    settings.setValue("engine/backend", QString::fromLatin1(backendName(config.engineConfig.backend)));
    settings.setValue("engine/cuda", QString::fromLatin1(cudaName(config.engineConfig.cudaMode)));
    settings.setValue("engine/logLevel", QString::fromLatin1(appLogLevelName(config.logLevel)));
    settings.setValue("render/pointSize", QString::number(static_cast<double>(config.pointSize), 'g', 9));
    settings.setValue("render/shadingMode", QString::fromLatin1(shadingName(config.shadingMode)));
    settings.setValue("render/fixedColor", colorName(config.fixedColor));
    settings.setValue("render/backgroundColor", colorName(config.backgroundColor));
    const QString cacheDirectory = config.engineConfig.cache.directory.empty()
        ? QString()
        : QString::fromUtf8(
              config.engineConfig.cache.directory.data(),
              static_cast<int>(config.engineConfig.cache.directory.size()));
    settings.setValue("cache/directory", cacheDirectory);
    settings.setValue(
        "memory/gpuCacheBytes",
        QString::number(static_cast<qulonglong>(config.engineConfig.memory.gpuCacheBytes)));
    settings.setValue(
        "memory/cpuCacheBytes",
        QString::number(static_cast<qulonglong>(config.engineConfig.memory.cpuCacheBytes)));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return Result<void>::failure(
            fileError(iniPath, kWriteFailedErrorCode, "save", settings.status()));
    }
    return Result<void>::success();
}

Result<SettingsLoadResult> SettingsController::loadWithCommandLine(
    const QString& iniPath,
    int argc,
    const char* const* argv) {
    Result<SettingsLoadResult> loaded = load(iniPath);
    if (!loaded.hasValue()) {
        return loaded;
    }

    const Result<AppConfigOverrides> overrides = CommandLineOptions::parseOverrides(argc, argv);
    if (!overrides.hasValue()) {
        return Result<SettingsLoadResult>::failure(overrides.error());
    }
    applyCommandLineOverrides(loaded.value().config, overrides.value());
    return loaded;
}

} // namespace dzc
