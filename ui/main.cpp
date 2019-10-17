#include "ui/MainWindow.h"
#include "core/CLIDriver.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName( "SUPP Development Team" );
    QCoreApplication::setApplicationName( "supp" );
    testerEntry();
    return 0;
    /*
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
    */
}
