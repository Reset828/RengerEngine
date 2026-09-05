#include "MainWindow.h"
#include "EngineUiAdapter.h"
#include "SettingsController.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>

#include <cassert>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

class FakePort final : public dzc::IEngineUiPort {
public:
    dzc::Result<void> enqueueCommand(dzc::EngineCommand command) override {
        if (failure.has_value()) {
            return dzc::Result<void>::failure(*failure);
        }
        commands.push_back(std::move(command));
        return dzc::Result<void>::success();
    }

    std::shared_ptr<const dzc::EngineSnapshot> getSnapshot() const override { return snapshot; }

    std::vector<dzc::EngineEvent> pollEvents() override { return {}; }

    std::vector<dzc::EngineCommand> commands;
    std::shared_ptr<const dzc::EngineSnapshot> snapshot = [] {
        auto value = std::make_shared<dzc::EngineSnapshot>();
        value->cudaAvailable = true;
        return std::shared_ptr<const dzc::EngineSnapshot>(std::move(value));
    }();
    std::optional<dzc::Error> failure;
};

dzc::Error testError() {
    return dzc::Error{dzc::ErrorDomain::General, 9U, "user summary", "diagnostic detail", "FakePort"};
}

QDoubleSpinBox* pointSize(const dzc::MainWindow& window) {
    return window.findChild<QDoubleSpinBox*>(QStringLiteral("pointSizeControl"));
}

QComboBox* shading(const dzc::MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("shadingModeControl"));
}

QComboBox* cuda(const dzc::MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("cudaModeControl"));
}

QPushButton* fixedColor(const dzc::MainWindow& window) {
    return window.findChild<QPushButton*>(QStringLiteral("fixedColorButton"));
}

QPushButton* backgroundColor(const dzc::MainWindow& window) {
    return window.findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
}

void clearStandardSettings() {
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Dzc"), QStringLiteral("Dzc-RenderEngine"));
    settings.clear();
    settings.sync();
}

void testControlsAndCommands() {
    clearStandardSettings();
    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);

    assert(pointSize(window)->minimum() == 1.0);
    assert(pointSize(window)->maximum() == 64.0);
    assert(pointSize(window)->singleStep() == 1.0);
    assert(shading(window)->count() == 4);
    assert(cuda(window)->count() == 3);

    pointSize(window)->setValue(8.0);
    assert(std::holds_alternative<dzc::SetPointSizeCommand>(port.commands.back()));
    assert(std::get<dzc::SetPointSizeCommand>(port.commands.back()).pixels == 8.0F);

    shading(window)->setCurrentIndex(shading(window)->findData(static_cast<int>(dzc::ShadingMode::Intensity)));
    assert(std::get<dzc::SetShadingModeCommand>(port.commands.back()).mode == dzc::ShadingMode::Intensity);

    window.submitFixedColor(QColor(0x12, 0x34, 0x56, 0x78));
    const auto fixed = std::get<dzc::SetFixedColorCommand>(port.commands.back()).color;
    assert(std::fabs(fixed.red - 0x12 / 255.0F) < 0.01F);
    assert(fixedColor(window)->property("rgba").toString() == QStringLiteral("#78123456"));

    window.submitBackgroundColor(QColor(0xAB, 0xCD, 0xEF, 0x90));
    const auto background = std::get<dzc::SetBackgroundColorCommand>(port.commands.back()).color;
    assert(std::fabs(background.blue - 0xEF / 255.0F) < 0.01F);
    assert(backgroundColor(window)->property("rgba").toString() == QStringLiteral("#90ABCDEF"));

    cuda(window)->setCurrentIndex(cuda(window)->findData(static_cast<int>(dzc::OptionalFeatureMode::On)));
    assert(std::get<dzc::SetCudaModeCommand>(port.commands.back()).mode == dzc::OptionalFeatureMode::On);
}

void testSnapshotSyncAndCapabilities() {
    clearStandardSettings();
    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    const std::size_t before = port.commands.size();

    auto snapshot = std::make_shared<dzc::EngineSnapshot>();
    snapshot->cudaAvailable = true;
    snapshot->cudaMode = dzc::OptionalFeatureMode::On;
    snapshot->pointSize = 12.0F;
    snapshot->shadingMode = dzc::ShadingMode::Height;
    snapshot->fixedColor = dzc::ColorRgba{1.0F, 0.0F, 0.0F, 0.5F};
    snapshot->backgroundColor = dzc::ColorRgba{0.0F, 1.0F, 0.0F, 1.0F};
    snapshot->dataset.hasRgb = false;
    snapshot->dataset.hasIntensity = false;
    port.snapshot = snapshot;
    window.refreshEngineState();

    assert(port.commands.size() == before);
    assert(pointSize(window)->value() == 12.0);
    assert(shading(window)->currentData().toInt() == static_cast<int>(dzc::ShadingMode::Height));
    assert(cuda(window)->currentData().toInt() == static_cast<int>(dzc::OptionalFeatureMode::On));
    assert(!shading(window)->itemData(shading(window)->findData(static_cast<int>(dzc::ShadingMode::OriginalColor)), Qt::UserRole - 1).toBool());
    assert(!shading(window)->itemData(shading(window)->findData(static_cast<int>(dzc::ShadingMode::Intensity)), Qt::UserRole - 1).toBool());

    snapshot->dataset.hasRgb.reset();
    snapshot->dataset.hasIntensity.reset();
    snapshot->cudaAvailable = false;
    window.refreshEngineState();
    assert(shading(window)->itemData(shading(window)->findData(static_cast<int>(dzc::ShadingMode::OriginalColor)), Qt::UserRole - 1).toBool());
    assert(shading(window)->itemData(shading(window)->findData(static_cast<int>(dzc::ShadingMode::Intensity)), Qt::UserRole - 1).toBool());
    assert(!cuda(window)->isEnabled());
}

void testFailureRollsBack() {
    clearStandardSettings();
    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    port.failure = testError();
    pointSize(window)->setValue(9.0);
    assert(pointSize(window)->value() == 1.0);
    assert(window.statusBar()->currentMessage().contains(QStringLiteral("user summary")));
    assert(window.findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"))->toPlainText().contains(QStringLiteral("diagnostic detail")));
}

void testSettingsRoundTripAndInvalidDefaults() {
    QTemporaryDir directory;
    assert(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("render.ini"));
    dzc::AppConfig expected;
    expected.pointSize = 16.5F;
    expected.shadingMode = dzc::ShadingMode::FixedColor;
    expected.fixedColor = dzc::ColorRgba{0.1F, 0.2F, 0.3F, 0.4F};
    expected.backgroundColor = dzc::ColorRgba{0.8F, 0.7F, 0.6F, 0.5F};
    expected.engineConfig.cudaMode = dzc::OptionalFeatureMode::On;
    assert(dzc::SettingsController::save(path, expected).hasValue());
    const auto loaded = dzc::SettingsController::load(path);
    assert(loaded.hasValue());
    assert(std::fabs(loaded.value().config.pointSize - expected.pointSize) < 0.001F);
    assert(loaded.value().config.shadingMode == expected.shadingMode);
    assert(loaded.value().config.engineConfig.cudaMode == expected.engineConfig.cudaMode);
    assert(std::fabs(loaded.value().config.fixedColor.alpha - expected.fixedColor.alpha) < 0.01F);

    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("render/pointSize"), QStringLiteral("65"));
    settings.setValue(QStringLiteral("render/shadingMode"), QStringLiteral("invalid"));
    settings.setValue(QStringLiteral("render/fixedColor"), QStringLiteral("not-a-color"));
    settings.sync();
    const auto invalid = dzc::SettingsController::load(path);
    assert(invalid.hasValue());
    assert(invalid.value().config.pointSize == 1.0F);
    assert(invalid.value().config.shadingMode == dzc::ShadingMode::OriginalColor);
    assert(invalid.value().config.fixedColor == dzc::ColorRgba{});
    assert(invalid.value().warnings.size() == 3U);
}

void testStartupRestoreSubmitsCommands() {
    QTemporaryDir directory;
    assert(directory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    clearStandardSettings();
    dzc::AppConfig expected;
    expected.pointSize = 7.0F;
    expected.shadingMode = dzc::ShadingMode::Height;
    expected.engineConfig.cudaMode = dzc::OptionalFeatureMode::Off;
    assert(dzc::SettingsController::saveStandard(expected).hasValue());

    FakePort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    assert(port.commands.size() == 5U);
    assert(std::get<dzc::SetPointSizeCommand>(port.commands[0]).pixels == 7.0F);
    assert(std::get<dzc::SetShadingModeCommand>(port.commands[1]).mode == dzc::ShadingMode::Height);
    assert(std::get<dzc::SetCudaModeCommand>(port.commands[4]).mode == dzc::OptionalFeatureMode::Off);
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    assert(settingsDirectory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    clearStandardSettings();
    testControlsAndCommands();
    testSnapshotSyncAndCapabilities();
    testFailureRollsBack();
    testSettingsRoundTripAndInvalidDefaults();
    testStartupRestoreSubmitsCommands();
    return 0;
}