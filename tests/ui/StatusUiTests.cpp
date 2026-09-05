#include "MainWindow.h"
#include "EngineUiAdapter.h"
#include "LogPanelModel.h"
#include "StatusPresenter.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>

#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakePort final : public dzc::IEngineUiPort {
public:
    dzc::Result<void> enqueueCommand(dzc::EngineCommand command) override {
        commands.push_back(std::move(command));
        return dzc::Result<void>::success();
    }
    std::shared_ptr<const dzc::EngineSnapshot> getSnapshot() const override { return snapshot; }
    std::vector<dzc::EngineEvent> pollEvents() override {
        auto result = std::move(events);
        events.clear();
        return result;
    }
    dzc::Result<void> init(
        const dzc::EngineConfig&,
        std::unique_ptr<dzc::IRenderBackend>,
        std::unique_ptr<dzc::IComputeBackend>) override {
        return dzc::Result<void>::success();
    }

    dzc::Result<void> update(const dzc::FrameInput&) override {
        return dzc::Result<void>::success();
    }

    dzc::Result<void> render() override {
        return dzc::Result<void>::success();
    }

    dzc::Result<void> resize(const dzc::RenderSize&) override {
        return dzc::Result<void>::success();
    }

    void shutdown() noexcept override {}
    std::vector<dzc::EngineCommand> commands;
    std::shared_ptr<const dzc::EngineSnapshot> snapshot;
    std::vector<dzc::EngineEvent> events;
};

QLabel* label(const QWidget& parent, const char* name) {
    return parent.findChild<QLabel*>(QString::fromLatin1(name));
}

void testPresenterDefaultsAndSnapshot() {
    const auto empty = dzc::StatusPresenter::format({});
    assert(empty.backend == QStringLiteral("Unknown"));
    assert(empty.framesPerSecond == QStringLiteral("0.00"));
    assert(empty.loadProgress == QStringLiteral("0%"));
    assert(empty.currentError == QStringLiteral("None"));

    auto snapshot = std::make_shared<dzc::EngineSnapshot>();
    snapshot->backend = dzc::RenderBackendType::Vulkan;
    snapshot->performance.framesPerSecond = 59.876;
    snapshot->dataset.state = dzc::DatasetState::Building;
    snapshot->dataset.progress = 1.5;
    snapshot->dataset.totalPointCount = 1000000000ULL;
    snapshot->dataset.visiblePointCount = 500000000ULL;
    snapshot->dataset.chunkCount = 100U;
    snapshot->dataset.visibleChunkCount = 25U;
    snapshot->memory.cpuResidentBytes = 1234U;
    snapshot->memory.cpuBudgetBytes = 5678U;
    snapshot->memory.gpuResidentBytes = 99U;
    snapshot->memory.gpuBudgetBytes = 1000U;
    snapshot->cudaAvailable = true;
    snapshot->cudaEnabled = true;
    snapshot->cudaMode = dzc::OptionalFeatureMode::On;
    snapshot->mostRecentError = dzc::Error{dzc::ErrorDomain::General, 7U, "User warning", "diagnostic", "test"};
    const auto result = dzc::StatusPresenter::format(snapshot);
    assert(result.backend == QStringLiteral("Vulkan"));
    assert(result.framesPerSecond == QStringLiteral("59.88"));
    assert(result.datasetState == QStringLiteral("Building"));
    assert(result.loadProgress == QStringLiteral("100%"));
    assert(result.totalPoints == QStringLiteral("1000000000"));
    assert(result.visibleChunks == QStringLiteral("25"));
    assert(result.cudaMode == QStringLiteral("On"));
    assert(result.currentError == QStringLiteral("User warning"));
}

void testLogModelSeverityAndBoundedHistory() {
    dzc::LogPanelModel model;
    assert(model.text() == QStringLiteral("No log entries."));
    assert(model.append(dzc::MessageEvent{dzc::EventSeverity::Info, "hello", {}}));
    assert(model.append(dzc::MessageEvent{dzc::EventSeverity::Warning, "warn", {}}));
    assert(model.append(dzc::ErrorEvent{dzc::EventSeverity::FatalError,
        dzc::Error{dzc::ErrorDomain::General, 9U, "user", "diagnostic", "context"}, {}}));
    assert(model.append(dzc::FeatureDegradedEvent{"CUDA", "unavailable"}));
    assert(!model.append(dzc::DatasetLoadedEvent{dzc::DatasetId{1U}}));
    assert(model.text().contains(QStringLiteral("[Info] hello")));
    assert(model.text().contains(QStringLiteral("[Warning] warn")));
    assert(model.text().contains(QStringLiteral("[FatalError] user | diagnostic [context]")));
    assert(model.text().contains(QStringLiteral("[Warning] CUDA: unavailable")));
    for (int i = 0; i < 1100; ++i) {
        model.append(dzc::MessageEvent{dzc::EventSeverity::Info, "entry", {}});
    }
    assert(model.size() == 1000U);
}

void testMainWindowSnapshotAndEvents() {
    FakePort port;
    port.snapshot = std::make_shared<dzc::EngineSnapshot>();
    port.snapshot->performance.framesPerSecond = 12.345;
    port.snapshot->dataset.totalPointCount = 42U;
    port.snapshot->dataset.state = dzc::DatasetState::Ready;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    assert(window.findChild<QDockWidget*>(QStringLiteral("statusDock")) != nullptr);
    window.refreshEngineState();
    assert(label(window, "statusBackend")->text() == QStringLiteral("Backend: OpenGL"));
    assert(label(window, "statusFps")->text() == QStringLiteral("FPS: 12.35"));
    assert(label(window, "statusTotalPoints")->text() == QStringLiteral("Total Points: 42"));
    assert(label(window, "statusDatasetState")->text() == QStringLiteral("Dataset State: Ready"));
    port.events.emplace_back(dzc::MessageEvent{dzc::EventSeverity::Warning, "degraded", {}});
    window.refreshEngineState();
    assert(window.findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"))->toPlainText().contains(QStringLiteral("[Warning] degraded")));
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QTemporaryDir settingsDirectory;
    assert(settingsDirectory.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    testPresenterDefaultsAndSnapshot();
    testLogModelSeverityAndBoundedHistory();
    testMainWindowSnapshotAndEvents();
    return 0;
}
