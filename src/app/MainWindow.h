#pragma once

#include <QMainWindow>
#include <QString>

#include <memory>

namespace dzc {

class EngineUiAdapter;

class MainWindow final : public QMainWindow {
public:
    // Creates the static application window without an Engine adapter.
    explicit MainWindow(QWidget* parent = nullptr);

    // Creates the application window over an externally owned Engine adapter.
    explicit MainWindow(EngineUiAdapter* adapter, QWidget* parent = nullptr);

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

    // Reads the latest Snapshot and pending Events and refreshes the dataset UI.
    void refreshEngineState();

private:
    void openDatasetDialog();
    void cancelDatasetLoading();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
