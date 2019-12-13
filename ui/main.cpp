#include "ui/MainWindow.h"
#include "core/CLIDriver.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName( "SUPP Development Team" );
    QCoreApplication::setApplicationName( "supp" );
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);

    QCommandLineParser parser;
    QCommandLineOption testOption(QStringList() << "t" << "test", QCoreApplication::tr("Run self test"));
    parser.addOption(testOption);
    parser.process(a);

    bool isTest = parser.isSet(testOption);
    if(isTest){
        // any test (assertion) failure will trigger abort
        testerEntry();
        return 0;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
