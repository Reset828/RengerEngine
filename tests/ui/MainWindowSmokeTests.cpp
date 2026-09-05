#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDockWidget>
#include <QMenu>
#include <QTimer>
#include <QWidget>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << std::endl;
            std::exit(1);
        }
    };

    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "settings directory invalid");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());

    dzc::MainWindow window;
    require(window.windowTitle() == QStringLiteral("Dzc-RenderEngine"), "window title mismatch");
    require(window.findChild<QWidget*>(QStringLiteral("renderViewPlaceholder")) != nullptr, "render placeholder missing");
    require(window.findChild<QDockWidget*>(QStringLiteral("datasetDock")) != nullptr, "dataset dock missing");
    require(window.findChild<QDockWidget*>(QStringLiteral("renderParametersDock")) != nullptr, "render parameters dock missing");
    require(window.findChild<QDockWidget*>(QStringLiteral("logDock")) != nullptr, "log dock missing");
    require(window.findChild<QDockWidget*>(QStringLiteral("statusDock")) != nullptr, "status dock missing");
    require(window.findChild<QAction*>(QStringLiteral("openDatasetAction")) != nullptr, "open action missing");
    require(window.findChild<QAction*>(QStringLiteral("exitAction")) != nullptr, "exit action missing");
    require(window.findChild<QAction*>(QStringLiteral("resetViewAction")) != nullptr, "reset action missing");
    require(window.findChild<QAction*>(QStringLiteral("aboutAction")) != nullptr, "about action missing");
    require(window.findChild<QMenu*>(QStringLiteral("fileMenu")) != nullptr, "file menu missing");
    require(window.findChild<QMenu*>(QStringLiteral("viewMenu")) != nullptr, "view menu missing");
    require(window.findChild<QMenu*>(QStringLiteral("helpMenu")) != nullptr, "help menu missing");

    window.show();
    application.processEvents();
    require(window.isVisible(), "window is not visible");
    require(window.findChild<QDockWidget*>(QStringLiteral("datasetDock"))->isVisible(), "dataset dock is not visible");
    require(window.findChild<QDockWidget*>(QStringLiteral("renderParametersDock"))->isVisible(), "render parameters dock is not visible");
    require(window.findChild<QDockWidget*>(QStringLiteral("logDock"))->isVisible(), "log dock is not visible");
    require(window.findChild<QDockWidget*>(QStringLiteral("statusDock"))->isVisible(), "status dock is not visible");

    QTimer::singleShot(0, &application, &QCoreApplication::quit);
    const int exitCode = application.exec();
    window.close();
    require(!window.isVisible(), "window did not close");
    return exitCode;
}
