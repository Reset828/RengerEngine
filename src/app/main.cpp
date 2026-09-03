#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    dzc::MainWindow window;
    window.show();

    return application.exec();
}
