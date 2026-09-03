#pragma once

#include <QMainWindow>

#include <memory>

namespace dzc {

class MainWindow final : public QMainWindow {
public:
    // Creates the application main window and its static UI layout.
    explicit MainWindow(QWidget* parent = nullptr);

    // Releases the private UI implementation.
    ~MainWindow() override;

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace dzc
