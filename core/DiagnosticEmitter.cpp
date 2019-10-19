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

void ConsoleDiagnosticEmitter::diagnosticHandle(Diag::ID id, const QList<QVariant>& data)
{
    Q_UNUSED(data)
    testDump(pathList, QStringLiteral("Diagnostic"), Diag::getString(id), QString(), QString());
}

/*
DiagnosticEmitter::DiagnosticEmitter(QObject *parent) : QObject(parent)
{

}
*/
