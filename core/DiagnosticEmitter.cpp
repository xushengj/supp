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
    QString optionalText;
    for(const auto& item : data){
        switch(static_cast<QMetaType::Type>(item.type())){
        default:{
            int ty = item.userType();
            if(ty == qMetaTypeId<ValueTypeWrapper>()){
                optionalText.append("[ValueType: ");
                ValueType vty = item.value<ValueTypeWrapper>().ty;
                optionalText.append(getTypeNameString(vty));
                optionalText.append(']');
            }else if(ty == qMetaTypeId<StringDiagnosticRecord>()){
                StringDiagnosticRecord record = item.value<StringDiagnosticRecord>();
                optionalText.append("[StringDiagnostic: str=\"");
                optionalText.append(record.str);
                optionalText.append("\", info=\"");
                optionalText.append(record.str.mid(record.infoStart, record.infoEnd - record.infoStart));
                optionalText.append("\"(");
                optionalText.append(QString::number(record.infoStart));
                optionalText.append(',');
                optionalText.append(QString::number(record.infoEnd));
                optionalText.append("), err=\"");
                optionalText.append(record.str.mid(record.errorStart, record.errorEnd - record.errorStart));
                optionalText.append("\"(");
                optionalText.append(QString::number(record.errorStart));
                optionalText.append(',');
                optionalText.append(QString::number(record.errorEnd));
                optionalText.append(")]");
            }else{
                optionalText.append("[UnknownType]");
            }
        }break;
        case QMetaType::Type::Int:{
            optionalText.append("[int: ");
            optionalText.append(QString::number(item.toInt()));
            optionalText.append("]");
        }break;
        case QMetaType::Type::QString:{
            optionalText.append("[string: ");
            optionalText.append(item.toString());
            optionalText.append("]");
        }break;
        case QMetaType::Type::QStringList:{
            optionalText.append("[stringlist: ");
            QStringList list = item.toStringList();
            for(const auto& str : list){
                optionalText.append(' ');
                optionalText.append('"');
                optionalText.append(str);
                optionalText.append('"');
            }
            optionalText.append("]");
        }break;
        }
    }
    testDump(pathList, QStringLiteral("Diagnostic"), Diag::getString(id), QString(), optionalText);
}

/*
DiagnosticEmitter::DiagnosticEmitter(QObject *parent) : QObject(parent)
{

}
*/
