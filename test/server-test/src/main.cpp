#include <QApplication>
#include "mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    ServerTestWindow window;
    window.show();

    return app.exec();
}
