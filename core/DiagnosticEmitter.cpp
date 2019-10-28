#include "core/DiagnosticEmitter.h"

#include <QDebug>

DiagnosticPathNode::DiagnosticPathNode(DiagnosticEmitterBase& d, const QString& pathName)
    : d(d), prev(d.head), pathName(pathName), hierarchyIndex(d.hierarchyCount)
{
    d.head = this;
    d.hierarchyCount += 1;
}

void DiagnosticPathNode::release()
{
    Q_ASSERT(hierarchyIndex >= 0);
    Q_ASSERT(d.hierarchyCount == hierarchyIndex + 1);
    d.head = prev;
    d.hierarchyCount = hierarchyIndex;
    hierarchyIndex = -1;
}

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
    QStringList pathList;
    auto ptr = currentHead();
    while(ptr){
        QString name = ptr->getPathName();
        QString detail = ptr->getDetailedName();
        if(!detail.isEmpty()){
            name.append(" (");
            name.append(detail);
            name.append(')');
        }
        pathList.push_front(name);
        ptr = ptr->getPrev();
    }
    testDump(pathList, QStringLiteral("Diagnostic"), Diag::getString(id), QString(), QString());
}

/*
DiagnosticEmitter::DiagnosticEmitter(QObject *parent) : QObject(parent)
{

}
*/
