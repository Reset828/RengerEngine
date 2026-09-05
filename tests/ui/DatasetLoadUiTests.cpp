#include "MainWindow.h"
#include "EngineUiAdapter.h"

#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QAction>
#include <QVariant>
#include <QSettings>
#include <QTemporaryDir>

#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

class FakeEngineUiPort final : public dzc::IEngineUiPort {
public:
    dzc::Result<void> enqueueCommand(dzc::EngineCommand command) override {
        if (failure.has_value()) {
            return dzc::Result<void>::failure(*failure);
        }
        commands.push_back(std::move(command));
        return dzc::Result<void>::success();
    }

    std::shared_ptr<const dzc::EngineSnapshot> getSnapshot() const override {
        return snapshot;
    }

    std::vector<dzc::EngineEvent> pollEvents() override {
        std::vector<dzc::EngineEvent> result = std::move(events);
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
    std::shared_ptr<const dzc::EngineSnapshot> snapshot = std::make_shared<dzc::EngineSnapshot>();
    std::vector<dzc::EngineEvent> events;
    std::optional<dzc::Error> failure;
};

QPushButton* button(const dzc::MainWindow& window, const QString& name) {
    return window.findChild<QPushButton*>(name);
}

QLabel* label(const dzc::MainWindow& window, const QString& name) {
    return window.findChild<QLabel*>(name);
}

QProgressBar* progress(const dzc::MainWindow& window) {
    return window.findChild<QProgressBar*>(QStringLiteral("loadingProgressBar"));
}

void testFilterAndLoadCommand() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);

    const QString filter = dzc::MainWindow::datasetFileDialogFilter();
    assert(filter.contains(QStringLiteral("*.pcd")));
    assert(filter.contains(QStringLiteral("*.PCD")));
    assert(filter.contains(QStringLiteral("*.ply")));
    assert(filter.contains(QStringLiteral("*.PLY")));
    assert(!filter.contains(QStringLiteral("*.*")));
    assert(window.findChild<QAction*>(QStringLiteral("openDatasetAction"))
               ->property("fileDialogFilter") == filter);

    const char utf8Path[] = "\xE6\xB5\x8B\xE8\xAF\x95\xE6\x95\xB0\xE6\x8D\xAE.pcd";
    window.submitDatasetPath(QString::fromUtf8(utf8Path));
    assert(port.commands.size() == 1U);
    const auto* command = std::get_if<dzc::LoadDatasetCommand>(&port.commands.back());
    assert(command != nullptr);
    assert(command->path == std::string(utf8Path));
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Loading"));
    assert(!button(window, QStringLiteral("openDatasetButton"))->isEnabled());
    assert(!button(window, QStringLiteral("cancelLoadingButton"))->isEnabled());
}

void testSnapshotProgressAndCancel() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    window.submitDatasetPath(QStringLiteral("cloud.ply"));

    auto snapshot = std::make_shared<dzc::EngineSnapshot>();
    snapshot->dataset.id = dzc::DatasetId{42U};
    snapshot->dataset.state = dzc::DatasetState::Opening;
    snapshot->dataset.progress = 0.25;
    snapshot->dataset.totalPointCount = 1000U;
    snapshot->dataset.visiblePointCount = 250U;
    port.snapshot = snapshot;
    window.refreshEngineState();
    assert(button(window, QStringLiteral("cancelLoadingButton"))->isEnabled());
    assert(progress(window)->value() == 25);
    port.events.emplace_back(dzc::DatasetProgressEvent{dzc::DatasetId{42U}, 50U, 100U});
    window.refreshEngineState();
    assert(progress(window)->value() == 50);
    assert(label(window, QStringLiteral("totalPointsLabel"))->text() == QStringLiteral("Total points: 1000"));
    assert(label(window, QStringLiteral("visiblePointsLabel"))->text() == QStringLiteral("Visible points: 250"));

    button(window, QStringLiteral("cancelLoadingButton"))->click();
    assert(port.commands.size() == 2U);
    const auto* cancel = std::get_if<dzc::CancelDatasetLoadCommand>(&port.commands.back());
    assert(cancel != nullptr);
    assert(cancel->datasetId == dzc::DatasetId{42U});
    assert(!button(window, QStringLiteral("cancelLoadingButton"))->isEnabled());

    port.events.emplace_back(dzc::DatasetLoadCancelledEvent{dzc::DatasetId{42U}});
    window.refreshEngineState();
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Cancelled"));
    assert(button(window, QStringLiteral("openDatasetButton"))->isEnabled());
    assert(!button(window, QStringLiteral("cancelLoadingButton"))->isEnabled());

    snapshot->dataset.state = dzc::DatasetState::Ready;
    snapshot->dataset.progress = 1.0;
    port.snapshot = snapshot;
    window.submitDatasetPath(QStringLiteral("again.pcd"));
    assert(port.commands.size() == 3U);
    assert(std::holds_alternative<dzc::LoadDatasetCommand>(port.commands.back()));
    window.refreshEngineState();
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Loading"));
    assert(!button(window, QStringLiteral("openDatasetButton"))->isEnabled());
}

void testCompletedAndFailedStates() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    window.submitDatasetPath(QStringLiteral("cloud.pcd"));

    auto snapshot = std::make_shared<dzc::EngineSnapshot>();
    snapshot->dataset.id = dzc::DatasetId{7U};
    snapshot->dataset.state = dzc::DatasetState::Ready;
    snapshot->dataset.progress = 1.0;
    port.snapshot = snapshot;
    window.refreshEngineState();
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Completed"));
    assert(progress(window)->value() == 100);
    assert(button(window, QStringLiteral("openDatasetButton"))->isEnabled());

    window.submitDatasetPath(QStringLiteral("broken.ply"));
    port.events.emplace_back(dzc::ErrorEvent{
        dzc::EventSeverity::RecoverableError,
        dzc::Error{dzc::ErrorDomain::DataFormat, 9U, "Invalid PLY", "header is truncated", "DatasetReader"},
        dzc::EventContext{dzc::DatasetId{0U}, {}, {}, {}}});
    window.refreshEngineState();
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Failed"));
    assert(window.findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"))
               ->toPlainText().contains(QStringLiteral("header is truncated")));
    assert(button(window, QStringLiteral("openDatasetButton"))->isEnabled());
}

void testSubmitFailureAndEmptyPath() {
    FakeEngineUiPort port;
    dzc::EngineUiAdapter adapter(port);
    dzc::MainWindow window(&adapter);
    window.submitDatasetPath(QString{});
    assert(port.commands.empty());

    port.failure = dzc::Error{dzc::ErrorDomain::Task, 3U, "Queue full", "command queue is full", "Fake"};
    window.submitDatasetPath(QStringLiteral("cloud.pcd"));
    assert(port.commands.empty());
    assert(label(window, QStringLiteral("loadingStatusLabel"))->text() == QStringLiteral("Status: Failed"));
    assert(window.findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"))
               ->toPlainText().contains(QStringLiteral("command queue is full")));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    testFilterAndLoadCommand();
    testSnapshotProgressAndCancel();
    testCompletedAndFailedStates();
    testSubmitFailureAndEmptyPath();
    return 0;
}
