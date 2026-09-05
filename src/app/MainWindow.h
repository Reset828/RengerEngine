#pragma once

#include "dzc/EngineConfig.h"
#include "dzc/EngineTypes.h"

#include <QColor>
#include <QMainWindow>
#include <QString>

#include <memory>

namespace dzc {

class EngineUiAdapter;
class OpenGLRenderWidget;

class MainWindow final : public QMainWindow {
public:
    // Creates the static application window without an Engine adapter.
    explicit MainWindow(QWidget* parent = nullptr);

    // Creates the application window over an externally owned Engine adapter.
    explicit MainWindow(EngineUiAdapter* adapter, QWidget* parent = nullptr);

    // Creates the application window with an externally-owned OpenGL host.
    MainWindow(EngineUiAdapter* adapter, OpenGLRenderWidget* renderWidget, QWidget* parent = nullptr);

    // Releases the private UI implementation.
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

    // Returns the only file types offered by the dataset file dialog.
    static QString datasetFileDialogFilter();

    // Submits a selected dataset path without opening a file dialog.
    void submitDatasetPath(const QString& path);

    // Refreshes status, dataset, parameters and logs from one Snapshot/Event poll.
    void refreshEngineState();

    // Submits a point-size change through the Engine adapter.
    void submitPointSize(float pixels);

    // Submits a shading-mode change through the Engine adapter.
    void submitShadingMode(ShadingMode mode);

    // Submits a fixed-color change through the Engine adapter.
    void submitFixedColor(const QColor& color);

    // Submits a background-color change through the Engine adapter.
    void submitBackgroundColor(const QColor& color);

    // Submits a CUDA-mode change through the Engine adapter.
    void submitCudaMode(OptionalFeatureMode mode);

private:
    void openDatasetDialog();
    void cancelDatasetLoading();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
