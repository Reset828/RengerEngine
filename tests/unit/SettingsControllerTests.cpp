#include "app/SettingsController.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

std::vector<const char*> argvFor(const std::vector<std::string>& arguments) {
    std::vector<const char*> pointers;
    pointers.reserve(arguments.size());
    for (const auto& argument : arguments) {
        pointers.push_back(argument.c_str());
    }
    return pointers;
}

dzc::AppConfig makeConfig() {
    dzc::AppConfig config;
    config.engineConfig.backend = dzc::RenderBackendType::Vulkan;
    config.engineConfig.cudaMode = dzc::OptionalFeatureMode::Off;
    config.logLevel = dzc::diagnostics::LogLevel::Debug;
    config.pointSize = 13.5F;
    config.shadingMode = dzc::ShadingMode::Intensity;
    config.fixedColor = dzc::ColorRgba{0.1F, 0.2F, 0.3F, 0.4F};
    config.backgroundColor = dzc::ColorRgba{0.8F, 0.7F, 0.6F, 0.5F};
    config.engineConfig.cache.directory = "C:/数据/cache";
    config.engineConfig.memory.gpuCacheBytes = 123456U;
    config.engineConfig.memory.cpuCacheBytes = 987654U;
    return config;
}

void assertConfigEqual(const dzc::AppConfig& actual, const dzc::AppConfig& expected) {
    assert(actual.engineConfig.backend == expected.engineConfig.backend);
    assert(actual.engineConfig.cudaMode == expected.engineConfig.cudaMode);
    assert(actual.logLevel == expected.logLevel);
    assert(actual.pointSize == expected.pointSize);
    assert(actual.shadingMode == expected.shadingMode);
    assert(actual.fixedColor == expected.fixedColor);
    assert(actual.backgroundColor == expected.backgroundColor);
    assert(actual.engineConfig.cache.directory == expected.engineConfig.cache.directory);
    assert(actual.engineConfig.memory.gpuCacheBytes == expected.engineConfig.memory.gpuCacheBytes);
    assert(actual.engineConfig.memory.cpuCacheBytes == expected.engineConfig.memory.cpuCacheBytes);
    assert(actual.engineConfig.cache.enabled == expected.engineConfig.cache.enabled);
    assert(actual.engineConfig.threads.workerThreads == expected.engineConfig.threads.workerThreads);
    assert(actual.engineConfig.threads.commandRecordingThreads == expected.engineConfig.threads.commandRecordingThreads);
    assert(actual.engineConfig.threads.maxConcurrentIoTasks == expected.engineConfig.threads.maxConcurrentIoTasks);
    assert(actual.engineConfig.commandQueueCapacity == expected.engineConfig.commandQueueCapacity);
    assert(actual.engineConfig.eventQueueCapacity == expected.engineConfig.eventQueueCapacity);
}

void testMissingFileUsesDefaults() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("missing.ini");

    const auto result = dzc::SettingsController::load(path);
    assert(result.hasValue());
    assert(result.value().warnings.empty());
    assertConfigEqual(result.value().config, dzc::AppConfig{});
}

void testRoundTrip() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("settings.ini");
    const dzc::AppConfig expected = makeConfig();

    const auto saveResult = dzc::SettingsController::save(path, expected);
    assert(saveResult.hasValue());

    const auto loadResult = dzc::SettingsController::load(path);
    assert(loadResult.hasValue());
    assert(loadResult.value().warnings.empty());
    assertConfigEqual(loadResult.value().config, expected);

    QSettings settings(path, QSettings::IniFormat);
    assert(settings.value("engine/backend").toString() == "vulkan");
    assert(settings.value("engine/cuda").toString() == "off");
    assert(settings.value("engine/logLevel").toString() == "debug");
    assert(settings.value("render/pointSize").toString() == "13.5");
    assert(settings.value("render/shadingMode").toString() == "intensity");
    assert(settings.value("render/fixedColor").toString() == "#661A334D");
    assert(settings.value("render/backgroundColor").toString() == "#80CCB299");
    assert(settings.value("memory/gpuCacheBytes").toString() == "123456");
    assert(settings.value("memory/cpuCacheBytes").toString() == "987654");
}

void testEmptyCacheDirectoryIsValid() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("empty-cache.ini");

    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("cache/directory", QString());
    settings.sync();
    assert(settings.status() == QSettings::NoError);

    const auto result = dzc::SettingsController::load(path);
    assert(result.hasValue());
    assert(result.value().config.engineConfig.cache.directory.empty());
    assert(result.value().warnings.empty());
}

void testInvalidValuesFallbackWithWarnings() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("invalid.ini");

    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("engine/backend", "metal");
    settings.setValue("engine/cuda", "sometimes");
    settings.setValue("engine/logLevel", "NOTICE");
    settings.setValue("render/pointSize", "65");
    settings.setValue("render/shadingMode", "invalid");
    settings.setValue("render/fixedColor", "invalid");
    settings.setValue("memory/gpuCacheBytes", "-1");
    settings.setValue("memory/cpuCacheBytes", "18446744073709551616");
    settings.sync();
    assert(settings.status() == QSettings::NoError);

    const auto result = dzc::SettingsController::load(path);
    assert(result.hasValue());
    assert(result.value().config.engineConfig.backend == dzc::RenderBackendType::OpenGL);
    assert(result.value().config.engineConfig.cudaMode == dzc::OptionalFeatureMode::Auto);
    assert(result.value().config.logLevel == dzc::diagnostics::LogLevel::Info);
    assert(result.value().config.engineConfig.memory.gpuCacheBytes == 0U);
    assert(result.value().config.engineConfig.memory.cpuCacheBytes == 0U);
    assert(result.value().warnings.size() == 8U);
}

void testCommandLineOverridesOnlyExplicitValues() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("precedence.ini");
    const dzc::AppConfig settingsConfig = makeConfig();
    assert(dzc::SettingsController::save(path, settingsConfig).hasValue());

    const std::vector<std::string> arguments{"dzc_app", "--log-level=trace"};
    const auto pointers = argvFor(arguments);
    const auto result = dzc::SettingsController::loadWithCommandLine(
        path, static_cast<int>(pointers.size()), pointers.data());
    assert(result.hasValue());
    assert(result.value().config.engineConfig.backend == dzc::RenderBackendType::Vulkan);
    assert(result.value().config.engineConfig.cudaMode == dzc::OptionalFeatureMode::Off);
    assert(result.value().config.engineConfig.memory.gpuCacheBytes == 123456U);
    assert(result.value().config.logLevel == dzc::diagnostics::LogLevel::Trace);

    const std::vector<std::string> backendArguments{"dzc_app", "--backend=opengl"};
    const auto backendPointers = argvFor(backendArguments);
    const auto backendResult = dzc::SettingsController::loadWithCommandLine(
        path, static_cast<int>(backendPointers.size()), backendPointers.data());
    assert(backendResult.hasValue());
    assert(backendResult.value().config.engineConfig.backend == dzc::RenderBackendType::OpenGL);
}

void testInvalidCommandLinePropagatesError() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath("invalid-command.ini");
    const std::vector<std::string> arguments{"dzc_app", "--backend=metal"};
    const auto pointers = argvFor(arguments);

    const auto result = dzc::SettingsController::loadWithCommandLine(
        path, static_cast<int>(pointers.size()), pointers.data());
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::Configuration);
    assert(result.error().code == 1U);
    assert(result.error().context == "CommandLineOptions::parse");
}

void testInvalidPathsReportFileIo() {
    const auto loadResult = dzc::SettingsController::load(QString());
    assert(!loadResult.hasValue());
    assert(loadResult.error().domain == dzc::ErrorDomain::FileIo);
    assert(loadResult.error().code == 1U);

    const auto saveResult = dzc::SettingsController::save(QString(), dzc::AppConfig{});
    assert(!saveResult.hasValue());
    assert(saveResult.error().domain == dzc::ErrorDomain::FileIo);
    assert(saveResult.error().code == 3U);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    testMissingFileUsesDefaults();
    testRoundTrip();
    testEmptyCacheDirectoryIsValid();
    testInvalidValuesFallbackWithWarnings();
    testCommandLineOverridesOnlyExplicitValues();
    testInvalidCommandLinePropagatesError();
    testInvalidPathsReportFileIo();
    return 0;
}