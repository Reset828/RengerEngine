#include "MainWindow.h"

#include "EngineUiAdapter.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QString>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace dzc {

struct MainWindow::Impl final {
    EngineUiAdapter* adapter{nullptr};
    QMenu* fileMenu{nullptr};
    QMenu* viewMenu{nullptr};
    QMenu* helpMenu{nullptr};
    QAction* openDatasetAction{nullptr};
    QAction* exitAction{nullptr};
    QAction* resetViewAction{nullptr};
    QAction* aboutAction{nullptr};
    QDockWidget* datasetDock{nullptr};
    QDockWidget* renderParametersDock{nullptr};
    QDockWidget* logDock{nullptr};
    QWidget* renderViewPlaceholder{nullptr};
    QPushButton* openDatasetButton{nullptr};
    QPushButton* cancelLoadingButton{nullptr};
    QLabel* loadingStatusLabel{nullptr};
    QProgressBar* loadingProgressBar{nullptr};
    QLabel* totalPointsLabel{nullptr};
    QLabel* visiblePointsLabel{nullptr};
    QPlainTextEdit* logText{nullptr};
    std::optional<DatasetId> activeDatasetId;
    std::optional<DatasetId> lastSettledDatasetId;
    bool loadInFlight{false};
};

namespace {

constexpr char kFileDialogFilter[] =
    "Point Cloud Data (*.pcd *.PCD);;Polygon File Format (*.ply *.PLY)";

QLabel* createValueLabel(QWidget* parent, const QString& text, const char* objectName) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QString::fromLatin1(objectName));
    return label;
}

QWidget* createDatasetPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("datasetPanel"));
    auto* layout = new QVBoxLayout(panel);

    auto* openButton = new QPushButton(QStringLiteral("Open Dataset"), panel);
    openButton->setObjectName(QStringLiteral("openDatasetButton"));
    layout->addWidget(openButton);

    auto* cancelButton = new QPushButton(QStringLiteral("Cancel Loading"), panel);
    cancelButton->setObjectName(QStringLiteral("cancelLoadingButton"));
    cancelButton->setEnabled(false);
    layout->addWidget(cancelButton);

    layout->addWidget(createValueLabel(panel, QStringLiteral("Status: Not started"), "loadingStatusLabel"));

    auto* progress = new QProgressBar(panel);
    progress->setObjectName(QStringLiteral("loadingProgressBar"));
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setFormat(QStringLiteral("Not started"));
    layout->addWidget(progress);

    layout->addWidget(createValueLabel(panel, QStringLiteral("Total points: 0"), "totalPointsLabel"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("Visible points: 0"), "visiblePointsLabel"));
    layout->addStretch(1);
    return panel;
}

QWidget* createRenderParametersPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("renderParametersPanel"));
    auto* layout = new QVBoxLayout(panel);

    layout->addWidget(createValueLabel(panel, QStringLiteral("Point Size: -"), "pointSizePlaceholder"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("Shading Mode: -"), "shadingModePlaceholder"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("Fixed Color: -"), "fixedColorPlaceholder"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("Background Color: -"), "backgroundColorPlaceholder"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("CUDA: -"), "cudaPlaceholder"));
    layout->addWidget(createValueLabel(panel, QStringLiteral("Camera Parameters: -"), "cameraParametersPlaceholder"));
    layout->addStretch(1);
    return panel;
}

QWidget* createLogPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("logPanel"));
    auto* layout = new QVBoxLayout(panel);
    auto* logText = new QPlainTextEdit(panel);
    logText->setObjectName(QStringLiteral("logTextPlaceholder"));
    logText->setReadOnly(true);
    logText->setPlainText(QStringLiteral("No log entries."));
    layout->addWidget(logText);
    return panel;
}

QAction* createAction(QObject* parent, const QString& text, const char* objectName) {
    auto* action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(objectName));
    return action;
}

QString errorSummary(const Error& error) {
    const std::string& message = error.userMessage.empty() ? error.diagnosticMessage : error.userMessage;
    return QString::fromUtf8(message.data(), static_cast<int>(message.size()));
}

QString errorDiagnostic(const Error& error) {
    QString result = QString::fromUtf8(error.diagnosticMessage.data(), static_cast<int>(error.diagnosticMessage.size()));
    if (!error.context.empty()) {
        if (!result.isEmpty()) {
            result += QStringLiteral(" ");
        }
        result += QStringLiteral("[") + QString::fromUtf8(error.context.data(), static_cast<int>(error.context.size())) + QStringLiteral("]");
    }
    return result;
}

int progressPercent(double progress) {
    if (!std::isfinite(progress)) {
        return 0;
    }
    return static_cast<int>(std::lround(std::clamp(progress, 0.0, 1.0) * 100.0));
}

bool isValidDatasetId(DatasetId id) noexcept {
    return id.value != 0U;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(nullptr, parent) {}

MainWindow::MainWindow(EngineUiAdapter* adapter, QWidget* parent)
    : QMainWindow(parent),
      m_impl(std::make_unique<Impl>()) {
    m_impl->adapter = adapter;

    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Dzc-RenderEngine"));

    m_impl->renderViewPlaceholder = new QWidget(this);
    m_impl->renderViewPlaceholder->setObjectName(QStringLiteral("renderViewPlaceholder"));
    auto* viewLayout = new QVBoxLayout(m_impl->renderViewPlaceholder);
    auto* viewLabel = new QLabel(QStringLiteral("Render view placeholder"), m_impl->renderViewPlaceholder);
    viewLabel->setObjectName(QStringLiteral("renderViewPlaceholderLabel"));
    viewLabel->setAlignment(Qt::AlignCenter);
    viewLayout->addWidget(viewLabel);
    setCentralWidget(m_impl->renderViewPlaceholder);

    m_impl->datasetDock = new QDockWidget(QStringLiteral("Dataset"), this);
    m_impl->datasetDock->setObjectName(QStringLiteral("datasetDock"));
    m_impl->datasetDock->setWidget(createDatasetPanel(m_impl->datasetDock));
    addDockWidget(Qt::LeftDockWidgetArea, m_impl->datasetDock);

    m_impl->renderParametersDock = new QDockWidget(QStringLiteral("Render Parameters"), this);
    m_impl->renderParametersDock->setObjectName(QStringLiteral("renderParametersDock"));
    m_impl->renderParametersDock->setWidget(createRenderParametersPanel(m_impl->renderParametersDock));
    addDockWidget(Qt::RightDockWidgetArea, m_impl->renderParametersDock);

    m_impl->logDock = new QDockWidget(QStringLiteral("Log"), this);
    m_impl->logDock->setObjectName(QStringLiteral("logDock"));
    m_impl->logDock->setWidget(createLogPanel(m_impl->logDock));
    addDockWidget(Qt::BottomDockWidgetArea, m_impl->logDock);

    m_impl->openDatasetButton = m_impl->datasetDock->findChild<QPushButton*>(QStringLiteral("openDatasetButton"));
    m_impl->cancelLoadingButton = m_impl->datasetDock->findChild<QPushButton*>(QStringLiteral("cancelLoadingButton"));
    m_impl->loadingStatusLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("loadingStatusLabel"));
    m_impl->loadingProgressBar = m_impl->datasetDock->findChild<QProgressBar*>(QStringLiteral("loadingProgressBar"));
    m_impl->totalPointsLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("totalPointsLabel"));
    m_impl->visiblePointsLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("visiblePointsLabel"));
    m_impl->logText = m_impl->logDock->findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"));

    m_impl->fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    m_impl->fileMenu->setObjectName(QStringLiteral("fileMenu"));
    m_impl->viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    m_impl->viewMenu->setObjectName(QStringLiteral("viewMenu"));
    m_impl->helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    m_impl->helpMenu->setObjectName(QStringLiteral("helpMenu"));

    m_impl->openDatasetAction = createAction(this, QStringLiteral("Open Dataset..."), "openDatasetAction");
    m_impl->openDatasetAction->setProperty("fileDialogFilter", datasetFileDialogFilter());
    m_impl->exitAction = createAction(this, QStringLiteral("Exit"), "exitAction");
    m_impl->resetViewAction = createAction(this, QStringLiteral("Reset View"), "resetViewAction");
    m_impl->aboutAction = createAction(this, QStringLiteral("About"), "aboutAction");

    m_impl->fileMenu->addAction(m_impl->openDatasetAction);
    m_impl->fileMenu->addAction(m_impl->exitAction);
    m_impl->viewMenu->addAction(m_impl->resetViewAction);
    m_impl->viewMenu->addSeparator();

    auto* datasetToggleAction = m_impl->datasetDock->toggleViewAction();
    datasetToggleAction->setObjectName(QStringLiteral("toggleDatasetDockAction"));
    auto* parametersToggleAction = m_impl->renderParametersDock->toggleViewAction();
    parametersToggleAction->setObjectName(QStringLiteral("toggleRenderParametersDockAction"));
    auto* logToggleAction = m_impl->logDock->toggleViewAction();
    logToggleAction->setObjectName(QStringLiteral("toggleLogDockAction"));
    m_impl->viewMenu->addAction(datasetToggleAction);
    m_impl->viewMenu->addAction(parametersToggleAction);
    m_impl->viewMenu->addAction(logToggleAction);
    m_impl->helpMenu->addAction(m_impl->aboutAction);

    connect(m_impl->openDatasetButton, &QPushButton::clicked, this, [this] { openDatasetDialog(); });
    connect(m_impl->openDatasetAction, &QAction::triggered, this, [this] { openDatasetDialog(); });
    connect(m_impl->cancelLoadingButton, &QPushButton::clicked, this, [this] { cancelDatasetLoading(); });
    connect(m_impl->exitAction, &QAction::triggered, this, &QWidget::close);

    if (m_impl->adapter == nullptr) {
        m_impl->openDatasetButton->setEnabled(false);
        m_impl->openDatasetAction->setEnabled(false);
        statusBar()->showMessage(QStringLiteral("Engine unavailable"));
    } else {
        statusBar()->showMessage(QStringLiteral("Ready"));
    }
}

MainWindow::~MainWindow() = default;

QString MainWindow::datasetFileDialogFilter() {
    return QString::fromLatin1(kFileDialogFilter);
}

void MainWindow::openDatasetDialog() {
    if (m_impl->adapter == nullptr) {
        statusBar()->showMessage(QStringLiteral("Engine unavailable"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Dataset"),
        QString(),
        datasetFileDialogFilter());
    if (!path.isEmpty()) {
        submitDatasetPath(path);
    }
}

void MainWindow::submitDatasetPath(const QString& path) {
    if (m_impl->adapter == nullptr) {
        statusBar()->showMessage(QStringLiteral("Engine unavailable"));
        return;
    }
    if (path.isEmpty()) {
        return;
    }

    const Result<void> result = m_impl->adapter->loadDataset(path);
    if (!result.hasValue()) {
        m_impl->loadInFlight = false;
        m_impl->activeDatasetId.reset();
        m_impl->cancelLoadingButton->setEnabled(false);
        m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Failed"));
        m_impl->loadingProgressBar->setFormat(QStringLiteral("Failed"));
        statusBar()->showMessage(QStringLiteral("Load failed: ") + errorSummary(result.error()));
        m_impl->logText->appendPlainText(errorDiagnostic(result.error()));
        return;
    }

    m_impl->loadInFlight = true;
    m_impl->activeDatasetId.reset();
    m_impl->openDatasetButton->setEnabled(false);
    m_impl->openDatasetAction->setEnabled(false);
    m_impl->cancelLoadingButton->setEnabled(false);
    m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Loading"));
    m_impl->loadingProgressBar->setValue(0);
    m_impl->loadingProgressBar->setFormat(QStringLiteral("Loading 0%"));
    statusBar()->showMessage(QStringLiteral("Loading dataset..."));
}

void MainWindow::cancelDatasetLoading() {
    if (m_impl->adapter == nullptr || !m_impl->loadInFlight || !m_impl->activeDatasetId.has_value()) {
        statusBar()->showMessage(QStringLiteral("No cancellable dataset load"));
        return;
    }

    const Result<void> result = m_impl->adapter->cancelDatasetLoad(*m_impl->activeDatasetId);
    if (!result.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Cancel failed: ") + errorSummary(result.error()));
        m_impl->logText->appendPlainText(errorDiagnostic(result.error()));
        return;
    }

    m_impl->cancelLoadingButton->setEnabled(false);
    m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Loading"));
    m_impl->loadingProgressBar->setFormat(QStringLiteral("Cancelling"));
    statusBar()->showMessage(QStringLiteral("Cancelling dataset load..."));
}

void MainWindow::refreshEngineState() {
    if (m_impl->adapter == nullptr) {
        return;
    }

    const std::shared_ptr<const EngineSnapshot> snapshot = m_impl->adapter->currentSnapshot();
    if (snapshot != nullptr) {
        m_impl->totalPointsLabel->setText(
            QStringLiteral("Total points: ") + QString::number(static_cast<qulonglong>(snapshot->dataset.totalPointCount)));
        m_impl->visiblePointsLabel->setText(
            QStringLiteral("Visible points: ") + QString::number(static_cast<qulonglong>(snapshot->dataset.visiblePointCount)));

        if (m_impl->loadInFlight) {
            const int percent = progressPercent(snapshot->dataset.progress);
            m_impl->loadingProgressBar->setValue(percent);

            switch (snapshot->dataset.state) {
            case DatasetState::Opening:
            case DatasetState::Building:
                if (isValidDatasetId(snapshot->dataset.id)) {
                    m_impl->activeDatasetId = snapshot->dataset.id;
                }
                m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Loading"));
                m_impl->loadingProgressBar->setFormat(QStringLiteral("Loading %1%").arg(percent));
                m_impl->cancelLoadingButton->setEnabled(m_impl->activeDatasetId.has_value());
                break;
            case DatasetState::Cancelling:
                m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Loading"));
                m_impl->loadingProgressBar->setFormat(QStringLiteral("Cancelling"));
                m_impl->cancelLoadingButton->setEnabled(false);
                break;
            case DatasetState::Ready:
                if (!isValidDatasetId(snapshot->dataset.id) ||
                    (m_impl->activeDatasetId.has_value() && snapshot->dataset.id != *m_impl->activeDatasetId) ||
                    (!m_impl->activeDatasetId.has_value() && m_impl->lastSettledDatasetId.has_value() &&
                     snapshot->dataset.id == *m_impl->lastSettledDatasetId)) {
                    break;
                }
                m_impl->activeDatasetId = snapshot->dataset.id;
                m_impl->lastSettledDatasetId = snapshot->dataset.id;
                m_impl->loadInFlight = false;
                m_impl->cancelLoadingButton->setEnabled(false);
                m_impl->openDatasetButton->setEnabled(true);
                m_impl->openDatasetAction->setEnabled(true);
                m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Completed"));
                m_impl->loadingProgressBar->setValue(100);
                m_impl->loadingProgressBar->setFormat(QStringLiteral("Completed"));
                statusBar()->showMessage(QStringLiteral("Dataset loaded"));
                break;
            case DatasetState::Error:
                if (!isValidDatasetId(snapshot->dataset.id) ||
                    (m_impl->activeDatasetId.has_value() && snapshot->dataset.id != *m_impl->activeDatasetId) ||
                    (!m_impl->activeDatasetId.has_value() && m_impl->lastSettledDatasetId.has_value() &&
                     snapshot->dataset.id == *m_impl->lastSettledDatasetId)) {
                    break;
                }
                m_impl->activeDatasetId = snapshot->dataset.id;
                m_impl->lastSettledDatasetId = snapshot->dataset.id;
                m_impl->loadInFlight = false;
                m_impl->cancelLoadingButton->setEnabled(false);
                m_impl->openDatasetButton->setEnabled(true);
                m_impl->openDatasetAction->setEnabled(true);
                m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Failed"));
                m_impl->loadingProgressBar->setFormat(QStringLiteral("Failed"));
                if (snapshot->mostRecentError.has_value()) {
                    statusBar()->showMessage(QStringLiteral("Load failed: ") + errorSummary(*snapshot->mostRecentError));
                    m_impl->logText->appendPlainText(errorDiagnostic(*snapshot->mostRecentError));
                }
                break;
            case DatasetState::None:
                break;
            }
        }
    }

    for (const EngineEvent& event : m_impl->adapter->pollEvents()) {
        std::visit([this](const auto& value) {
            using Event = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Event, DatasetProgressEvent>) {
                if (!m_impl->loadInFlight) {
                    return;
                }
                if (!m_impl->activeDatasetId.has_value()) {
                    m_impl->activeDatasetId = value.datasetId;
                }
                if (*m_impl->activeDatasetId != value.datasetId) {
                    return;
                }
                const double progress = value.totalUnits == 0U
                    ? 0.0
                    : static_cast<double>(value.completedUnits) / static_cast<double>(value.totalUnits);
                const int percent = progressPercent(progress);
                m_impl->loadingProgressBar->setValue(percent);
                m_impl->loadingProgressBar->setFormat(QStringLiteral("Loading %1%").arg(percent));
            } else if constexpr (std::is_same_v<Event, DatasetLoadedEvent>) {
                if (m_impl->loadInFlight && (!m_impl->activeDatasetId.has_value() || *m_impl->activeDatasetId == value.datasetId)) {
                    m_impl->activeDatasetId = value.datasetId;
                    m_impl->lastSettledDatasetId = value.datasetId;
                    m_impl->loadInFlight = false;
                    m_impl->cancelLoadingButton->setEnabled(false);
                    m_impl->openDatasetButton->setEnabled(true);
                    m_impl->openDatasetAction->setEnabled(true);
                    m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Completed"));
                    m_impl->loadingProgressBar->setValue(100);
                    m_impl->loadingProgressBar->setFormat(QStringLiteral("Completed"));
                    statusBar()->showMessage(QStringLiteral("Dataset loaded"));
                }
            } else if constexpr (std::is_same_v<Event, DatasetLoadCancelledEvent>) {
                if (m_impl->loadInFlight && (!m_impl->activeDatasetId.has_value() || *m_impl->activeDatasetId == value.datasetId)) {
                    m_impl->activeDatasetId = value.datasetId;
                    m_impl->lastSettledDatasetId = value.datasetId;
                    m_impl->loadInFlight = false;
                    m_impl->cancelLoadingButton->setEnabled(false);
                    m_impl->openDatasetButton->setEnabled(true);
                    m_impl->openDatasetAction->setEnabled(true);
                    m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Cancelled"));
                    m_impl->loadingProgressBar->setFormat(QStringLiteral("Cancelled"));
                    statusBar()->showMessage(QStringLiteral("Dataset load cancelled"));
                }
            } else if constexpr (std::is_same_v<Event, ErrorEvent>) {
                if (m_impl->loadInFlight &&
                    (!isValidDatasetId(value.context.datasetId) ||
                     !m_impl->activeDatasetId.has_value() || *m_impl->activeDatasetId == value.context.datasetId)) {
                    m_impl->activeDatasetId = value.context.datasetId;
                    if (isValidDatasetId(value.context.datasetId)) {
                        m_impl->lastSettledDatasetId = value.context.datasetId;
                    }
                    m_impl->loadInFlight = false;
                    m_impl->cancelLoadingButton->setEnabled(false);
                    m_impl->openDatasetButton->setEnabled(true);
                    m_impl->openDatasetAction->setEnabled(true);
                    m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Failed"));
                    m_impl->loadingProgressBar->setFormat(QStringLiteral("Failed"));
                    statusBar()->showMessage(QStringLiteral("Load failed: ") + errorSummary(value.error));
                }
                m_impl->logText->appendPlainText(errorDiagnostic(value.error));
            } else if constexpr (std::is_same_v<Event, MessageEvent>) {
                m_impl->logText->appendPlainText(QString::fromUtf8(value.message.data(), static_cast<int>(value.message.size())));
            } else if constexpr (std::is_same_v<Event, FeatureDegradedEvent>) {
                m_impl->logText->appendPlainText(
                    QString::fromUtf8(value.feature.data(), static_cast<int>(value.feature.size())) + QStringLiteral(": ") +
                    QString::fromUtf8(value.reason.data(), static_cast<int>(value.reason.size())));
            }
        }, event);
    }
}

} // namespace dzc
