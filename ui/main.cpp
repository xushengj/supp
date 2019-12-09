#include "ui/MainWindow.h"
#include "core/CLIDriver.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName( "SUPP Development Team" );
    QCoreApplication::setApplicationName( "supp" );
#if 1
    testerEntry();
    return 0;
#else
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
#endif
}
