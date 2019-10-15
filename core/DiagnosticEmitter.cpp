#include "core/DiagnosticEmitter.h"

#include <QDebug>

namespace{
// just for test purpose
void testDump(
        const QStringList& path,
        const QString& msgType,
        const QString& msgCategory,
        const QString& msg,
        const QString& optionalText)
{
    QString finalmsg;
    finalmsg.append(msgType);
    finalmsg.append(" [");
    finalmsg.append(msgCategory);
    finalmsg.append("]: ");
    finalmsg.append(msg);
    finalmsg.append("\nPath: ");
    finalmsg.append(path.join(QString()));
    if(!optionalText.isEmpty()){
        finalmsg.append("\nAdditional Info: ");
        finalmsg.append(optionalText);
    }
    qWarning() << finalmsg;
}
}// anonymous namespace

void ConsoleDiagnosticEmitter::info   (QString category, QString text, QString optionalText)
{
    testDump(pathList, QStringLiteral("info"), category, text, optionalText);
}
void ConsoleDiagnosticEmitter::warning(QString category, QString text, QString optionalText)
{
    testDump(pathList, QStringLiteral("warning"), category, text, optionalText);
}
void ConsoleDiagnosticEmitter::error  (QString category, QString text, QString optionalText)
{
    testDump(pathList, QStringLiteral("error"), category, text, optionalText);
}

/*
DiagnosticEmitter::DiagnosticEmitter(QObject *parent) : QObject(parent)
{

}
*/
