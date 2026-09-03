#include <QApplication>
#include <QMainWindow>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Dzc-RenderEngine"));
    window.show();

    return application.exec();
}