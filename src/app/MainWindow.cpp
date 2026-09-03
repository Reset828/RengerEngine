#include "MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
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

#include <memory>

namespace dzc {

struct MainWindow::Impl final {
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
};

namespace {

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
    layout->addWidget(cancelButton);

    layout->addWidget(createValueLabel(panel, QStringLiteral("Status: Ready"), "loadingStatusLabel"));

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

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_impl(std::make_unique<Impl>()) {
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

    m_impl->fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    m_impl->fileMenu->setObjectName(QStringLiteral("fileMenu"));
    m_impl->viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    m_impl->viewMenu->setObjectName(QStringLiteral("viewMenu"));
    m_impl->helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    m_impl->helpMenu->setObjectName(QStringLiteral("helpMenu"));

    m_impl->openDatasetAction = createAction(this, QStringLiteral("Open Dataset..."), "openDatasetAction");
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

    statusBar()->showMessage(QStringLiteral("Ready"));
}

MainWindow::~MainWindow() = default;

} // namespace dzc
