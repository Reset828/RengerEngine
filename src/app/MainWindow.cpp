#include "MainWindow.h"

#include "EngineUiAdapter.h"
#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
#include "OpenGLRenderWidget.h"
#endif
#include "SettingsController.h"
#include "LogPanelModel.h"
#include "StatusPresenter.h"


#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
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
    QDockWidget* statusDock{nullptr};
    QWidget* statusPanel{nullptr};
    LogPanelModel logModel;
    bool logDirty{false};
    QWidget* renderViewPlaceholder{nullptr};
    QPushButton* openDatasetButton{nullptr};
    QPushButton* cancelLoadingButton{nullptr};
    QLabel* loadingStatusLabel{nullptr};
    QProgressBar* loadingProgressBar{nullptr};
    QLabel* totalPointsLabel{nullptr};
    QLabel* visiblePointsLabel{nullptr};
    QPlainTextEdit* logText{nullptr};
    QDoubleSpinBox* pointSizeControl{nullptr};
    QComboBox* shadingModeControl{nullptr};
    QPushButton* fixedColorButton{nullptr};
    QPushButton* backgroundColorButton{nullptr};
    QComboBox* cudaModeControl{nullptr};
    AppConfig config;
    std::optional<DatasetId> activeDatasetId;
    std::optional<DatasetId> lastSettledDatasetId;
    bool loadInFlight{false};
    void appendLog(const EngineEvent& event);
    void syncLog();
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
    auto* layout = new QFormLayout(panel);

    auto* pointSize = new QDoubleSpinBox(panel);
    pointSize->setObjectName(QStringLiteral("pointSizeControl"));
    pointSize->setRange(1.0, 64.0);
    pointSize->setSingleStep(1.0);
    pointSize->setDecimals(1);
    layout->addRow(QStringLiteral("Point Size"), pointSize);

    auto* shading = new QComboBox(panel);
    shading->setObjectName(QStringLiteral("shadingModeControl"));
    shading->addItem(QStringLiteral("OriginalColor"), static_cast<int>(ShadingMode::OriginalColor));
    shading->addItem(QStringLiteral("FixedColor"), static_cast<int>(ShadingMode::FixedColor));
    shading->addItem(QStringLiteral("Height"), static_cast<int>(ShadingMode::Height));
    shading->addItem(QStringLiteral("Intensity"), static_cast<int>(ShadingMode::Intensity));
    layout->addRow(QStringLiteral("Shading Mode"), shading);

    auto* fixed = new QPushButton(panel);
    fixed->setObjectName(QStringLiteral("fixedColorButton"));
    layout->addRow(QStringLiteral("Fixed Color"), fixed);

    auto* background = new QPushButton(panel);
    background->setObjectName(QStringLiteral("backgroundColorButton"));
    layout->addRow(QStringLiteral("Background Color"), background);

    auto* cuda = new QComboBox(panel);
    cuda->setObjectName(QStringLiteral("cudaModeControl"));
    cuda->addItem(QStringLiteral("Off"), static_cast<int>(OptionalFeatureMode::Off));
    cuda->addItem(QStringLiteral("On"), static_cast<int>(OptionalFeatureMode::On));
    cuda->addItem(QStringLiteral("Auto"), static_cast<int>(OptionalFeatureMode::Auto));
    layout->addRow(QStringLiteral("CUDA"), cuda);

    layout->addRow(QStringLiteral("Camera Parameters"), createValueLabel(panel, QStringLiteral("Not available"), "cameraParametersPlaceholder"));
    return panel;
}

struct StatusField final {
    const char* objectName;
    const char* label;
    QString StatusPresentation::*value;
};

const StatusField kStatusFields[] = {
    {"statusBackend", "Backend", &StatusPresentation::backend},
    {"statusFps", "FPS", &StatusPresentation::framesPerSecond},
    {"statusDatasetState", "Dataset State", &StatusPresentation::datasetState},
    {"statusLoadProgress", "Load Progress", &StatusPresentation::loadProgress},
    {"statusTotalPoints", "Total Points", &StatusPresentation::totalPoints},
    {"statusVisiblePoints", "Visible Points", &StatusPresentation::visiblePoints},
    {"statusTotalChunks", "Total Chunks", &StatusPresentation::totalChunks},
    {"statusVisibleChunks", "Visible Chunks", &StatusPresentation::visibleChunks},
    {"statusCpuResidency", "CPU Residency", &StatusPresentation::cpuResidency},
    {"statusCpuBudget", "CPU Budget", &StatusPresentation::cpuBudget},
    {"statusGpuResidency", "GPU Residency", &StatusPresentation::gpuResidency},
    {"statusGpuBudget", "GPU Budget", &StatusPresentation::gpuBudget},
    {"statusCudaAvailable", "CUDA Available", &StatusPresentation::cudaAvailable},
    {"statusCudaMode", "CUDA Mode", &StatusPresentation::cudaMode},
    {"statusCudaEnabled", "CUDA Enabled", &StatusPresentation::cudaEnabled},
    {"statusCurrentError", "Current Error/Warning", &StatusPresentation::currentError}
};

void updateStatusPanel(QWidget* panel, const std::shared_ptr<const EngineSnapshot>& snapshot) {
    const StatusPresentation presentation = StatusPresenter::format(snapshot);
    for (const auto& field : kStatusFields) {
        auto* label = panel->findChild<QLabel*>(QString::fromLatin1(field.objectName));
        label->setText(QString::fromLatin1(field.label) + QStringLiteral(": ") + presentation.*(field.value));
    }
}

QWidget* createStatusPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("statusPanel"));
    auto* layout = new QVBoxLayout(panel);
    for (const auto& field : kStatusFields) {
        auto* label = createValueLabel(panel, {}, field.objectName);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        if (field.value == &StatusPresentation::cpuResidency || field.value == &StatusPresentation::cpuBudget ||
            field.value == &StatusPresentation::gpuResidency || field.value == &StatusPresentation::gpuBudget) {
            label->setToolTip(QStringLiteral("Bytes (decimal)"));
        }
        layout->addWidget(label);
    }
    layout->addStretch(1);
    updateStatusPanel(panel, {});
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

int progressPercent(double progress) {
    if (!std::isfinite(progress)) {
        return 0;
    }
    return static_cast<int>(std::lround(std::clamp(progress, 0.0, 1.0) * 100.0));
}

bool isValidDatasetId(DatasetId id) noexcept {
    return id.value != 0U;
}

ShadingMode shadingModeFromIndex(const QComboBox* control) {
    return static_cast<ShadingMode>(control->currentData().toInt());
}

OptionalFeatureMode cudaModeFromIndex(const QComboBox* control) {
    return static_cast<OptionalFeatureMode>(control->currentData().toInt());
}

int shadingIndex(const QComboBox* control, ShadingMode mode) {
    return control->findData(static_cast<int>(mode));
}

int cudaIndex(const QComboBox* control, OptionalFeatureMode mode) {
    return control->findData(static_cast<int>(mode));
}

QColor toQColor(const ColorRgba& value) {
    QColor color;
    color.setRgbF(value.red, value.green, value.blue, value.alpha);
    return color;
}

void updateColorButton(QPushButton* button, const QColor& color) {
    const QString value = color.name(QColor::HexArgb).toUpper();
    button->setText(value);
    button->setProperty("rgba", value);
    button->setToolTip(value);
    button->setStyleSheet(QStringLiteral("background-color: %1;").arg(value));
}

void setComboItemEnabled(QComboBox* control, int index, bool enabled) {
    if (index >= 0) {
        control->setItemData(index, enabled, Qt::UserRole - 1);
    }
}

} // namespace

void MainWindow::Impl::appendLog(const EngineEvent& event) {
    if (logModel.append(event)) {
        logDirty = true;
        syncLog();
    }
}

void MainWindow::Impl::syncLog() {
    if (!logDirty) {
        return;
    }
    auto* scrollBar = logText->verticalScrollBar();
    const bool followTail = scrollBar->value() == scrollBar->maximum();
    const int previousPosition = scrollBar->value();
    logText->setPlainText(logModel.text());
    scrollBar->setValue(followTail ? scrollBar->maximum() : previousPosition);
    logDirty = false;
}

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(nullptr, parent) {}

MainWindow::MainWindow(EngineUiAdapter* adapter, QWidget* parent)
    : MainWindow(adapter, nullptr, parent) {}

MainWindow::MainWindow(EngineUiAdapter* adapter, OpenGLRenderWidget* renderWidget, QWidget* parent)
    : QMainWindow(parent),
      m_impl(std::make_unique<Impl>()) {
    m_impl->adapter = adapter;

    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Dzc-RenderEngine"));

#if defined(DZC_HAS_OPENGL_RENDER_WIDGET)
    if (renderWidget != nullptr) {
        renderWidget->setParent(this);
        m_impl->renderViewPlaceholder = renderWidget;
        setCentralWidget(renderWidget);
        renderWidget->setRefreshCallback([this] { refreshEngineState(); });
    } else {
#else
    Q_UNUSED(renderWidget);
    {
#endif
        m_impl->renderViewPlaceholder = new QWidget(this);
        m_impl->renderViewPlaceholder->setObjectName(QStringLiteral("renderViewPlaceholder"));
        auto* viewLayout = new QVBoxLayout(m_impl->renderViewPlaceholder);
        auto* viewLabel = new QLabel(QStringLiteral("Render view placeholder"), m_impl->renderViewPlaceholder);
        viewLabel->setObjectName(QStringLiteral("renderViewPlaceholderLabel"));
        viewLabel->setAlignment(Qt::AlignCenter);
        viewLayout->addWidget(viewLabel);
        setCentralWidget(m_impl->renderViewPlaceholder);
    }

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

    m_impl->statusDock = new QDockWidget(QStringLiteral("Status"), this);
    m_impl->statusDock->setObjectName(QStringLiteral("statusDock"));
    auto* statusScrollArea = new QScrollArea(m_impl->statusDock);
    statusScrollArea->setObjectName(QStringLiteral("statusScrollArea"));
    statusScrollArea->setWidgetResizable(true);
    m_impl->statusPanel = createStatusPanel(statusScrollArea);
    statusScrollArea->setWidget(m_impl->statusPanel);
    m_impl->statusDock->setWidget(statusScrollArea);
    addDockWidget(Qt::RightDockWidgetArea, m_impl->statusDock);

    m_impl->openDatasetButton = m_impl->datasetDock->findChild<QPushButton*>(QStringLiteral("openDatasetButton"));
    m_impl->cancelLoadingButton = m_impl->datasetDock->findChild<QPushButton*>(QStringLiteral("cancelLoadingButton"));
    m_impl->loadingStatusLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("loadingStatusLabel"));
    m_impl->loadingProgressBar = m_impl->datasetDock->findChild<QProgressBar*>(QStringLiteral("loadingProgressBar"));
    m_impl->totalPointsLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("totalPointsLabel"));
    m_impl->visiblePointsLabel = m_impl->datasetDock->findChild<QLabel*>(QStringLiteral("visiblePointsLabel"));
    m_impl->logText = m_impl->logDock->findChild<QPlainTextEdit*>(QStringLiteral("logTextPlaceholder"));
    m_impl->pointSizeControl = m_impl->renderParametersDock->findChild<QDoubleSpinBox*>(QStringLiteral("pointSizeControl"));
    m_impl->shadingModeControl = m_impl->renderParametersDock->findChild<QComboBox*>(QStringLiteral("shadingModeControl"));
    m_impl->fixedColorButton = m_impl->renderParametersDock->findChild<QPushButton*>(QStringLiteral("fixedColorButton"));
    m_impl->backgroundColorButton = m_impl->renderParametersDock->findChild<QPushButton*>(QStringLiteral("backgroundColorButton"));
    m_impl->cudaModeControl = m_impl->renderParametersDock->findChild<QComboBox*>(QStringLiteral("cudaModeControl"));

    updateColorButton(m_impl->fixedColorButton, toQColor(m_impl->config.fixedColor));
    updateColorButton(m_impl->backgroundColorButton, toQColor(m_impl->config.backgroundColor));
    m_impl->pointSizeControl->setValue(m_impl->config.pointSize);
    m_impl->shadingModeControl->setCurrentIndex(shadingIndex(m_impl->shadingModeControl, m_impl->config.shadingMode));
    m_impl->cudaModeControl->setCurrentIndex(cudaIndex(m_impl->cudaModeControl, m_impl->config.engineConfig.cudaMode));

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
    auto* statusToggleAction = m_impl->statusDock->toggleViewAction();
    statusToggleAction->setObjectName(QStringLiteral("toggleStatusDockAction"));
    m_impl->viewMenu->addAction(statusToggleAction);
    m_impl->helpMenu->addAction(m_impl->aboutAction);

    connect(m_impl->openDatasetButton, &QPushButton::clicked, this, [this] { openDatasetDialog(); });
    connect(m_impl->openDatasetAction, &QAction::triggered, this, [this] { openDatasetDialog(); });
    connect(m_impl->resetViewAction, &QAction::triggered, this, [this] {
        if (m_impl->adapter == nullptr) {
            statusBar()->showMessage(QStringLiteral("Engine unavailable"));
            return;
        }
        const Result<void> result = m_impl->adapter->resetView();
        if (!result.hasValue()) {
            statusBar()->showMessage(QStringLiteral("Reset view failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
        }
    });
    connect(m_impl->cancelLoadingButton, &QPushButton::clicked, this, [this] { cancelDatasetLoading(); });
    connect(m_impl->pointSizeControl, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        submitPointSize(static_cast<float>(value));
    });
    connect(m_impl->shadingModeControl, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        submitShadingMode(shadingModeFromIndex(m_impl->shadingModeControl));
    });
    connect(m_impl->fixedColorButton, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(toQColor(m_impl->config.fixedColor), this, QStringLiteral("Fixed Color"), QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            submitFixedColor(selected);
        }
    });
    connect(m_impl->backgroundColorButton, &QPushButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(toQColor(m_impl->config.backgroundColor), this, QStringLiteral("Background Color"), QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            submitBackgroundColor(selected);
        }
    });
    connect(m_impl->cudaModeControl, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        submitCudaMode(cudaModeFromIndex(m_impl->cudaModeControl));
    });
    connect(m_impl->exitAction, &QAction::triggered, this, &QWidget::close);

    const Result<SettingsLoadResult> loadedSettings = SettingsController::loadStandard();
    if (loadedSettings.hasValue()) {
        m_impl->config = loadedSettings.value().config;
        for (const std::string& warning : loadedSettings.value().warnings) {
            m_impl->appendLog(MessageEvent{EventSeverity::Warning, warning, {}});
        }
        {
            const QSignalBlocker pointBlocker(m_impl->pointSizeControl);
            const QSignalBlocker shadingBlocker(m_impl->shadingModeControl);
            const QSignalBlocker cudaBlocker(m_impl->cudaModeControl);
            m_impl->pointSizeControl->setValue(m_impl->config.pointSize);
            m_impl->shadingModeControl->setCurrentIndex(shadingIndex(m_impl->shadingModeControl, m_impl->config.shadingMode));
            m_impl->cudaModeControl->setCurrentIndex(cudaIndex(m_impl->cudaModeControl, m_impl->config.engineConfig.cudaMode));
        }
        updateColorButton(m_impl->fixedColorButton, toQColor(m_impl->config.fixedColor));
        updateColorButton(m_impl->backgroundColorButton, toQColor(m_impl->config.backgroundColor));
    } else {
        statusBar()->showMessage(QStringLiteral("Settings load failed: ") + errorSummary(loadedSettings.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, loadedSettings.error(), {}});
    }

    if (m_impl->adapter == nullptr) {
        m_impl->openDatasetButton->setEnabled(false);
        m_impl->openDatasetAction->setEnabled(false);
        statusBar()->showMessage(QStringLiteral("Engine unavailable"));
    } else {
        statusBar()->showMessage(QStringLiteral("Ready"));
        // Restore only settings explicitly present in the standard file; no Engine is created here.
        const QSettings settings(SettingsController::standardPath(), QSettings::IniFormat);
        if (settings.contains(QStringLiteral("render/pointSize"))) {
            submitPointSize(m_impl->config.pointSize);
        }
        if (settings.contains(QStringLiteral("render/shadingMode"))) {
            submitShadingMode(m_impl->config.shadingMode);
        }
        if (settings.contains(QStringLiteral("render/fixedColor"))) {
            submitFixedColor(toQColor(m_impl->config.fixedColor));
        }
        if (settings.contains(QStringLiteral("render/backgroundColor"))) {
            submitBackgroundColor(toQColor(m_impl->config.backgroundColor));
        }
        if (settings.contains(QStringLiteral("engine/cuda"))) {
            submitCudaMode(m_impl->config.engineConfig.cudaMode);
        }
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
        m_impl->openDatasetButton->setEnabled(true);
        m_impl->openDatasetAction->setEnabled(true);
        m_impl->loadingStatusLabel->setText(QStringLiteral("Status: Failed"));
        m_impl->loadingProgressBar->setFormat(QStringLiteral("Failed"));
        statusBar()->showMessage(QStringLiteral("Load failed: ") + errorSummary(result.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
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

void MainWindow::submitPointSize(float pixels) {
    if (!std::isfinite(pixels) || pixels < 1.0F || pixels > 64.0F) {
        statusBar()->showMessage(QStringLiteral("Point size must be between 1 and 64"));
        return;
    }
    const float previous = m_impl->config.pointSize;
    if (m_impl->adapter != nullptr) {
        const Result<void> result = m_impl->adapter->setPointSize(pixels);
        if (!result.hasValue()) {
            const QSignalBlocker blocker(m_impl->pointSizeControl);
            m_impl->pointSizeControl->setValue(previous);
            statusBar()->showMessage(QStringLiteral("Point size failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
            return;
        }
    }
    m_impl->config.pointSize = pixels;
    {
        const QSignalBlocker blocker(m_impl->pointSizeControl);
        m_impl->pointSizeControl->setValue(pixels);
    }
    const Result<void> saved = SettingsController::saveStandard(m_impl->config);
    if (!saved.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Point size applied; settings write failed: ") + errorSummary(saved.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, saved.error(), {}});
    }
}

void MainWindow::submitShadingMode(ShadingMode mode) {
    const ShadingMode previous = m_impl->config.shadingMode;
    if (m_impl->adapter != nullptr) {
        const Result<void> result = m_impl->adapter->setShadingMode(mode);
        if (!result.hasValue()) {
            const QSignalBlocker blocker(m_impl->shadingModeControl);
            m_impl->shadingModeControl->setCurrentIndex(shadingIndex(m_impl->shadingModeControl, previous));
            statusBar()->showMessage(QStringLiteral("Shading mode failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
            return;
        }
    }
    m_impl->config.shadingMode = mode;
    {
        const QSignalBlocker blocker(m_impl->shadingModeControl);
        m_impl->shadingModeControl->setCurrentIndex(shadingIndex(m_impl->shadingModeControl, mode));
    }
    const Result<void> saved = SettingsController::saveStandard(m_impl->config);
    if (!saved.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Shading mode applied; settings write failed: ") + errorSummary(saved.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, saved.error(), {}});
    }
}

void MainWindow::submitFixedColor(const QColor& color) {
    if (!color.isValid()) {
        statusBar()->showMessage(QStringLiteral("Fixed color is invalid"));
        return;
    }
    const ColorRgba previous = m_impl->config.fixedColor;
    if (m_impl->adapter != nullptr) {
        const Result<void> result = m_impl->adapter->setFixedColor(color);
        if (!result.hasValue()) {
            updateColorButton(m_impl->fixedColorButton, toQColor(previous));
            statusBar()->showMessage(QStringLiteral("Fixed color failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
            return;
        }
    }
    m_impl->config.fixedColor = ColorRgba{
        static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()), static_cast<float>(color.alphaF())};
    updateColorButton(m_impl->fixedColorButton, color);
    const Result<void> saved = SettingsController::saveStandard(m_impl->config);
    if (!saved.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Fixed color applied; settings write failed: ") + errorSummary(saved.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, saved.error(), {}});
    }
}

void MainWindow::submitBackgroundColor(const QColor& color) {
    if (!color.isValid()) {
        statusBar()->showMessage(QStringLiteral("Background color is invalid"));
        return;
    }
    const ColorRgba previous = m_impl->config.backgroundColor;
    if (m_impl->adapter != nullptr) {
        const Result<void> result = m_impl->adapter->setBackgroundColor(color);
        if (!result.hasValue()) {
            updateColorButton(m_impl->backgroundColorButton, toQColor(previous));
            statusBar()->showMessage(QStringLiteral("Background color failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
            return;
        }
    }
    m_impl->config.backgroundColor = ColorRgba{
        static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()), static_cast<float>(color.alphaF())};
    updateColorButton(m_impl->backgroundColorButton, color);
    const Result<void> saved = SettingsController::saveStandard(m_impl->config);
    if (!saved.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Background color applied; settings write failed: ") + errorSummary(saved.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, saved.error(), {}});
    }
}

void MainWindow::submitCudaMode(OptionalFeatureMode mode) {
    const OptionalFeatureMode previous = m_impl->config.engineConfig.cudaMode;
    if (m_impl->adapter != nullptr) {
        const Result<void> result = m_impl->adapter->setCudaMode(mode);
        if (!result.hasValue()) {
            const QSignalBlocker blocker(m_impl->cudaModeControl);
            m_impl->cudaModeControl->setCurrentIndex(cudaIndex(m_impl->cudaModeControl, previous));
            statusBar()->showMessage(QStringLiteral("CUDA mode failed: ") + errorSummary(result.error()));
            m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
            return;
        }
    }
    m_impl->config.engineConfig.cudaMode = mode;
    {
        const QSignalBlocker blocker(m_impl->cudaModeControl);
        m_impl->cudaModeControl->setCurrentIndex(cudaIndex(m_impl->cudaModeControl, mode));
    }
    const Result<void> saved = SettingsController::saveStandard(m_impl->config);
    if (!saved.hasValue()) {
        statusBar()->showMessage(QStringLiteral("CUDA mode applied; settings write failed: ") + errorSummary(saved.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, saved.error(), {}});
    }
}

void MainWindow::cancelDatasetLoading() {
    if (m_impl->adapter == nullptr || !m_impl->loadInFlight || !m_impl->activeDatasetId.has_value()) {
        statusBar()->showMessage(QStringLiteral("No cancellable dataset load"));
        return;
    }

    const Result<void> result = m_impl->adapter->cancelDatasetLoad(*m_impl->activeDatasetId);
    if (!result.hasValue()) {
        statusBar()->showMessage(QStringLiteral("Cancel failed: ") + errorSummary(result.error()));
        m_impl->appendLog(ErrorEvent{EventSeverity::RecoverableError, result.error(), {}});
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
    updateStatusPanel(m_impl->statusPanel, snapshot);
    if (snapshot != nullptr) {
        {
            const QSignalBlocker pointBlocker(m_impl->pointSizeControl);
            const QSignalBlocker shadingBlocker(m_impl->shadingModeControl);
            const QSignalBlocker cudaBlocker(m_impl->cudaModeControl);
            m_impl->pointSizeControl->setValue(std::clamp(snapshot->pointSize, 1.0F, 64.0F));
            const int shadeIndex = shadingIndex(m_impl->shadingModeControl, snapshot->shadingMode);
            if (shadeIndex >= 0) {
                m_impl->shadingModeControl->setCurrentIndex(shadeIndex);
            }
            const int modeIndex = cudaIndex(m_impl->cudaModeControl, snapshot->cudaMode);
            if (modeIndex >= 0) {
                m_impl->cudaModeControl->setCurrentIndex(modeIndex);
            }
        }
        updateColorButton(m_impl->fixedColorButton, toQColor(snapshot->fixedColor));
        updateColorButton(m_impl->backgroundColorButton, toQColor(snapshot->backgroundColor));
        m_impl->config.pointSize = snapshot->pointSize;
        m_impl->config.shadingMode = snapshot->shadingMode;
        m_impl->config.fixedColor = snapshot->fixedColor;
        m_impl->config.backgroundColor = snapshot->backgroundColor;
        m_impl->config.engineConfig.cudaMode = snapshot->cudaMode;

        setComboItemEnabled(
            m_impl->shadingModeControl,
            shadingIndex(m_impl->shadingModeControl, ShadingMode::OriginalColor),
            !snapshot->dataset.hasRgb.has_value() || *snapshot->dataset.hasRgb);
        setComboItemEnabled(
            m_impl->shadingModeControl,
            shadingIndex(m_impl->shadingModeControl, ShadingMode::Intensity),
            !snapshot->dataset.hasIntensity.has_value() || *snapshot->dataset.hasIntensity);
        m_impl->cudaModeControl->setEnabled(snapshot->cudaAvailable);
        m_impl->cudaModeControl->setToolTip(
            snapshot->cudaAvailable ? QStringLiteral("CUDA mode") : QStringLiteral("CUDA unavailable"));

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
                }
                break;
            case DatasetState::None:
                break;
            }
        }
    }

    for (const EngineEvent& event : m_impl->adapter->pollEvents()) {
        if (m_impl->logModel.append(event)) {
            m_impl->logDirty = true;
        }
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
                else {
                    statusBar()->showMessage(errorSummary(value.error));
                }
            } else if constexpr (std::is_same_v<Event, MessageEvent>) {
                if (value.severity != EventSeverity::Info) {
                    statusBar()->showMessage(QString::fromUtf8(value.message.data(), static_cast<int>(value.message.size())));
                }
            } else if constexpr (std::is_same_v<Event, FeatureDegradedEvent>) {
                statusBar()->showMessage(
                    QString::fromUtf8(value.feature.data(), static_cast<int>(value.feature.size())) + QStringLiteral(": ") +
                    QString::fromUtf8(value.reason.data(), static_cast<int>(value.reason.size())));
            }
        }, event);
    }
    m_impl->syncLog();
}

} // namespace dzc
