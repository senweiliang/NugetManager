#include "NugetManager.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    NugetManager w;
    w.show();
    return a.exec();
}
