#include <QApplication>
#include <QMainWindow>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Dzc-RenderEngine"));
    window.show();

    if (!window.isVisible()) {
        return 1;
    }

    QTimer::singleShot(0, &application, &QCoreApplication::quit);
    return application.exec();
}