#include "core/XML.h"

#include "core/Value.h"
#include "core/IR.h"
#include "core/DiagnosticEmitter.h"
#include "util/ADT.h"

#include <QDebug>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <memory>

#define XML_INDENT_SPACE 2

namespace{
const QString STR_XML_IRROOTINST = QStringLiteral("IRInstance");
const QString STR_XML_IRROOTINST_TYPENAME = QStringLiteral("TypeName");
const QString STR_XML_IRNODEINST = QStringLiteral("Node");
const QString STR_XML_IRNODEINST_INDEX = QStringLiteral("ID");
const QString STR_XML_IRNODEINST_TYPENAME = QStringLiteral("TypeName");
const QString STR_XML_IRNODEINST_PARAM = QStringLiteral("Parameter");
const QString STR_XML_IRNODEINST_PARAM_NAME = QStringLiteral("Name");
const QString STR_XML_IRNODEINST_PARAM_TYPE = QStringLiteral("Type");

const QString XML_TY_VOID       = QStringLiteral("Void");
const QString XML_TY_INT64      = QStringLiteral("Int64");
const QString XML_TY_STRING     = QStringLiteral("String");
const QString XML_TY_NODEPTR    = QStringLiteral("NodePtr");
const QString XML_TY_VALUEPTR   = QStringLiteral("ValuePtr");
QString getValueTypeName(ValueType ty)
{
    switch(ty){
    case ValueType::Void:       return XML_TY_VOID;
    case ValueType::Int64:      return XML_TY_INT64;
    case ValueType::String:     return XML_TY_STRING;
    case ValueType::NodePtr:    return XML_TY_NODEPTR;
    case ValueType::ValuePtr:   return XML_TY_VALUEPTR;
    }
    Q_UNREACHABLE();
}
std::pair<bool,ValueType> getValueTypeFromName(QStringRef name)
{
    if(name == XML_TY_STRING)
        return std::make_pair(true, ValueType::String);
    if(name == XML_TY_INT64)
        return std::make_pair(true, ValueType::Int64);
    if(name == XML_TY_NODEPTR)
        return std::make_pair(true, ValueType::NodePtr);
    if(name == XML_TY_VALUEPTR)
        return std::make_pair(true, ValueType::ValuePtr);
    if(name == XML_TY_VOID)
        return std::make_pair(true, ValueType::Void);
    return std::make_pair(false, ValueType::Void);
}
}

namespace {
void writeToXML_IRNode(QXmlStreamWriter& xml, const IRRootInstance& ir, int nodeIndex)
{
    const auto& nodeInst = ir.getNode(nodeIndex);
    int tyIndex = nodeInst.getTypeIndex();
    const auto& nodeTy = ir.getType().getNodeType(tyIndex);
    xml.writeStartElement(STR_XML_IRNODEINST);
    xml.writeAttribute(STR_XML_IRNODEINST_TYPENAME, nodeTy.getName());
    xml.writeAttribute(STR_XML_IRNODEINST_INDEX, QString::number(nodeIndex));
    for(int i = 0, n = nodeTy.getNumParameter(); i < n; ++i){
        const QString& paramName = nodeTy.getParameterName(i);
        xml.writeStartElement(STR_XML_IRNODEINST_PARAM);
        xml.writeAttribute(STR_XML_IRNODEINST_PARAM_NAME, paramName);
        ValueType valTy = nodeTy.getParameterType(i);
        xml.writeAttribute(STR_XML_IRNODEINST_PARAM_TYPE, getValueTypeName(valTy));
        const QVariant& val = nodeInst.getParameter(i);
        switch(valTy){
        case ValueType::String:
            xml.writeCharacters(val.toString());
            break;
        case ValueType::Int64:
            xml.writeCharacters(QString::number(val.toLongLong()));
            break;
        default:
            Q_UNREACHABLE();
        }
        xml.writeEndElement(); // parameter
    }
    if(nodeInst.getNumChildNode() > 0){
        for(int i = 0, n = nodeInst.getNumChildNode(); i < n; ++i){
            writeToXML_IRNode(xml, ir, nodeInst.getChildNodeByOrder(i));
        }
    }

    xml.writeEndElement(); // node instance
}
}

void XML::writeIRInstance(const IRRootInstance& ir, QIODevice* dest)
{
    Q_ASSERT(ir.validated());

    QXmlStreamWriter xml(dest);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(XML_INDENT_SPACE);
    xml.writeStartDocument();
    const auto& rootTy = ir.getType();
    xml.writeStartElement(STR_XML_IRROOTINST);
    xml.writeAttribute(STR_XML_IRROOTINST_TYPENAME, rootTy.getName());
    writeToXML_IRNode(xml, ir, 0);
    xml.writeEndElement(); // root instance
    xml.writeEndDocument();
}

namespace{
bool readFromXML_IRNodeInstance(QXmlStreamReader& xml, DiagnosticEmitterBase& diagnostic,
                                const IRRootType& ty, IRRootInstance& root, int parentIndex, int& nodeIndex){
    Q_ASSERT(xml.isStartElement());
    if(Q_UNLIKELY(xml.name() != STR_XML_IRNODEINST)){
        diagnostic(Diag::Error_XML_UnexpectedElement,
                   static_cast<int>(xml.lineNumber()),
                   static_cast<int>(xml.columnNumber()),
                   STR_XML_IRNODEINST, xml.name().toString());
        return false;
    }
    QString tyName;
    for(const auto& attr : xml.attributes()){
        if(attr.name() == STR_XML_IRNODEINST_TYPENAME){
            tyName = attr.value().toString();
        }else if(attr.name() == STR_XML_IRNODEINST_INDEX){
            // do nothing; this is ignored and is always recomputed
        }else{
            diagnostic(Diag::Warn_XML_UnexpectedAttribute,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       STR_XML_IRNODEINST, attr.name().toString(), attr.value().toString());
        }
    }
    DiagnosticPathNode pathNode(diagnostic, QCoreApplication::tr("Node %1").arg(nodeIndex));
    int nodeTyIndex = ty.getNodeTypeIndex(tyName);
    if(Q_UNLIKELY(nodeTyIndex == -1)){
        diagnostic(Diag::Error_XML_UnknownIRNodeType,
                   static_cast<int>(xml.lineNumber()),
                   static_cast<int>(xml.columnNumber()),
                   tyName);
        return false;
    }
    pathNode.setDetailedName(tyName);
    const IRNodeType& nodeTy = ty.getNodeType(nodeTyIndex);
    int numParams = nodeTy.getNumParameter();
    QList<QVariant> args;
    args.reserve(numParams);
    for(int i = 0; i < numParams; ++i){
        args.push_back(QVariant());
    }
    RunTimeSizeArray<bool> isArgSet(static_cast<std::size_t>(numParams), false);

    // deal with all parameters
    bool isEndElementFound = false;
    while(!xml.atEnd()){
        xml.readNext();
        if(Q_UNLIKELY(xml.hasError())){
            diagnostic(Diag::Error_XML_InvalidXML,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       xml.errorString());
            return false;
        }
        if(xml.isComment() || xml.isCharacters())
            continue;
        if(xml.isEndElement()){
            isEndElementFound = true;
            break;
        }
        if(Q_UNLIKELY(!xml.isStartElement())){
            diagnostic(Diag::Error_XML_ExpectingStartElement,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       xml.tokenString());
            return false;
        }
        QStringRef elementName = xml.name();
        if(elementName == STR_XML_IRNODEINST){
            break;
        }

        if(Q_UNLIKELY(elementName != STR_XML_IRNODEINST_PARAM)){
            diagnostic(Diag::Error_XML_UnexpectedElement,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       STR_XML_IRNODEINST_PARAM, elementName.toString());
            return false;
        }

        // deal with parameter here
        QString paramName;
        ValueType paramType = ValueType::Void;
        bool isParamTypeSet = false;
        for(const auto& attr : xml.attributes()){
            if(attr.name() == STR_XML_IRNODEINST_PARAM_NAME){
                paramName = attr.value().toString();
            }else if(attr.name() == STR_XML_IRNODEINST_PARAM_TYPE){
                auto p = getValueTypeFromName(attr.value());
                if(Q_UNLIKELY(!p.first)){
                    diagnostic(Diag::Error_XML_UnknownValueType,
                               static_cast<int>(xml.lineNumber()),
                               static_cast<int>(xml.columnNumber()),
                               paramType);
                    return false;
                }
                paramType = p.second;
                isParamTypeSet = true;
            }else{
                diagnostic(Diag::Warn_XML_UnexpectedAttribute,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()),
                           STR_XML_IRNODEINST_PARAM, attr.name().toString(), attr.value().toString());
            }
        }
        if(Q_UNLIKELY(paramName.isEmpty())){
            diagnostic(Diag::Error_XML_IRNode_Param_MissingName,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()));
            return false;
        }
        int paramIndex = nodeTy.getParameterIndex(paramName);
        if(Q_UNLIKELY(paramIndex == -1)){
            diagnostic(Diag::Error_XML_IRNode_Param_UnknownParam,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       paramName);
            return false;
        }
        if(Q_UNLIKELY(!isParamTypeSet)){
            diagnostic(Diag::Error_XML_IRNode_Param_MissingType,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()));
            return false;
        }
        ValueType expectedTy = nodeTy.getParameterType(paramIndex);
        if(Q_UNLIKELY(paramType != expectedTy)){
            diagnostic(Diag::Error_XML_IRNode_Param_TypeMismatch,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       paramName, expectedTy, paramType);
            return false;
        }
        xml.readNext();
        // if the string is empty, we may have EndElement right after StartElement
        QString paramData;
        if(xml.isCharacters()){
            paramData = xml.text().toString();
            xml.readNext();
        }
        if(Q_UNLIKELY(!xml.isEndElement())){
            diagnostic(Diag::Error_XML_IRNode_Param_ExpectEndElement,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       paramName);
            return false;
        }
        QVariant data;
        switch(paramType){
        case ValueType::String:{
            data = paramData;
        }break;
        case ValueType::Int64:{
            bool isGood = false;
            data = paramData.toLongLong(&isGood);
            if(Q_UNLIKELY(!isGood)){
                diagnostic(Diag::Error_XML_IRNode_Param_InvalidValue,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()),
                           paramName,
                           paramType,
                           paramData);
                return false;
            }
        }break;
        default:
            Q_UNREACHABLE();
        }

        // write back parameter
        bool isArgSetBefore = isArgSet.at(paramIndex);
        isArgSet.at(paramIndex) = true;
        if(Q_UNLIKELY(isArgSetBefore)){
            diagnostic(Diag::Error_XML_IRNode_Param_MultipleValue,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       paramName);
            return false;
        }
        args[paramIndex] = data;
    }

    // check if all parameters are initialized
    for(int i = 0; i < numParams; ++i){
        if(!isArgSet.at(i)){
            // generate a warning and default initialize it
            diagnostic(Diag::Warn_XML_IRNode_MissingParameter,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()));
            switch(nodeTy.getParameterType(i)){
            case ValueType::String:{
                args[i] = QString();
            }break;
            case ValueType::Int64:{
                args[i] = static_cast<qlonglong>(0);
            }break;
            default:
                Q_UNREACHABLE();
            }
        }
    }

    if(parentIndex != -1){
        root.getNode(parentIndex).addChildNode(nodeIndex);
    }
    int currentNodeIndex = root.addNode(nodeTyIndex);
    Q_ASSERT(currentNodeIndex == nodeIndex);
    nodeIndex += 1;

    IRNodeInstance& inst = root.getNode(currentNodeIndex);
    inst.setParent(parentIndex);
    inst.setParameters(args);

    // skip first readNext(); that is done when trying to find end of parameters
    bool isFirstReadAfterParameter = true;
    if(!isEndElementFound){
        // deal with all child elements
        while(!xml.atEnd()){
            if(isFirstReadAfterParameter){
                isFirstReadAfterParameter = false;
            }else{
                xml.readNext();
            }
            if(Q_UNLIKELY(xml.hasError())){
                diagnostic(Diag::Error_XML_InvalidXML,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()),
                           xml.errorString());
                return false;
            }
            if(xml.isComment() || xml.isCharacters())
                continue;
            if(xml.isEndElement()){
                isEndElementFound = true;
                break;
            }
            if(Q_UNLIKELY(!xml.isStartElement())){
                diagnostic(Diag::Error_XML_ExpectingStartElement,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()),
                           xml.tokenString());
                return false;
            }
            if(Q_UNLIKELY(xml.name() == STR_XML_IRNODEINST_PARAM)){
                diagnostic(Diag::Error_XML_IRNode_ParamAfterChildNode,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()));
                return false;
            }
            // child node
            bool isChildGood = readFromXML_IRNodeInstance(xml, diagnostic, ty, root, currentNodeIndex, nodeIndex);
            if(Q_UNLIKELY(!isChildGood))
                return false;
        }
    }
    return true;
}
}
IRRootInstance* XML::readIRInstance(const IRRootType &ty, DiagnosticEmitterBase& diagnostic, QIODevice* src)
{
    Q_ASSERT(ty.validated());

    QXmlStreamReader xml(src);
    std::unique_ptr<IRRootInstance> ptr;

    // loop till the StartElement of IRRootInstance
    while(!xml.atEnd()){
        xml.readNext();
        if(xml.hasError())
            break;
        if(!xml.isStartElement())
            continue;
        if(Q_UNLIKELY(xml.name() != STR_XML_IRROOTINST)){
            diagnostic(Diag::Error_XML_UnexpectedElement,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       STR_XML_IRROOTINST, xml.name().toString());
            return nullptr;
        }
        bool isTypeNameFound = false;
        for(const auto& attr : xml.attributes()){
            if(Q_LIKELY(attr.name() == STR_XML_IRROOTINST_TYPENAME)){
                auto tyName = attr.value();
                if(tyName != ty.getName()){
                    diagnostic(Diag::Warn_XML_MismatchedIRTypeName,
                               static_cast<int>(xml.lineNumber()),
                               static_cast<int>(xml.columnNumber()),
                               ty.getName(), tyName.toString());
                }
                isTypeNameFound = true;
            }else{
                diagnostic(Diag::Warn_XML_UnexpectedAttribute,
                           static_cast<int>(xml.lineNumber()),
                           static_cast<int>(xml.columnNumber()),
                           STR_XML_IRROOTINST, attr.name().toString(), attr.value().toString());
            }
        }
        ptr.reset(new IRRootInstance(ty));
        break;
    }

    if(Q_UNLIKELY(!ptr)){
        // something is wrong
        if(xml.hasError()){
            diagnostic(Diag::Error_XML_InvalidXML,
                       static_cast<int>(xml.lineNumber()),
                       static_cast<int>(xml.columnNumber()),
                       xml.errorString());
            return nullptr;
        }
        // otherwise we did not find any element (including IRRootInstance)
        diagnostic(Diag::Error_XML_ExpectingIRRootInstance,
                   static_cast<int>(xml.lineNumber()),
                   static_cast<int>(xml.columnNumber()));
        return nullptr;
    }

    // seek to the root node
    while(!xml.atEnd()){
         if(xml.readNext() == QXmlStreamReader::StartElement)
             break;
    }

    // make sure we are indeed at QXmlStreamReader::StartElement instead of an error
    if(Q_UNLIKELY(xml.hasError())){
        diagnostic(Diag::Error_XML_InvalidXML,
                   static_cast<int>(xml.lineNumber()),
                   static_cast<int>(xml.columnNumber()),
                   xml.errorString());
        return nullptr;
    }
    Q_ASSERT(xml.isStartElement());

    DiagnosticPathNode pathNode(diagnostic, QCoreApplication::tr("IR Root"));

    int nodeIndex = 0;
    bool isRootGood = readFromXML_IRNodeInstance(xml, diagnostic, ty, *ptr, -1, nodeIndex);
    if(!isRootGood){
        return nullptr;
    }

    while(!xml.atEnd()){
        if(xml.readNext() == QXmlStreamReader::EndElement)
            break;
        if(xml.isComment())
            continue;
    }

    if(Q_UNLIKELY(xml.hasError())){
        diagnostic(Diag::Error_XML_InvalidXML,
                   static_cast<int>(xml.lineNumber()),
                   static_cast<int>(xml.columnNumber()),
                   xml.errorString());
        return nullptr;
    }

    if(Q_UNLIKELY(!(ptr->validate(diagnostic)))){
        return nullptr;
    }

    return ptr.release();
}
