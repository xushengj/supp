#include "core/XML.h"

#include "core/Value.h"
#include "core/IR.h"
#include "core/DiagnosticEmitter.h"
#include "core/Parser.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <memory>

namespace{
const QString STR_NAME = QStringLiteral("Name");
const QString STR_INDEX = QStringLiteral("ID");
const QString STR_PARSER = QStringLiteral("Parser");
const QString STR_EXPR_START = QStringLiteral("ExprStart");
const QString STR_EXPR_END = QStringLiteral("ExprEnd");
const QString STR_ROOTNODE_NAME = QStringLiteral("RootNodeName");
const QString STR_MATCHPAIR_LIST = QStringLiteral("MatchPairList");
const QString STR_MATCHPAIR = QStringLiteral("MatchPair");
const QString STR_MATCHPAIR_START = QStringLiteral("Start");
const QString STR_MATCHPAIR_END = QStringLiteral("End");
const QString STR_IGNORE_LIST = QStringLiteral("IgnoreList");
const QString STR_IGNORE = QStringLiteral("Ignore");
const QString STR_PARSERNODE_LIST = QStringLiteral("ParserNodeList");
const QString STR_PARSERNODE = QStringLiteral("ParserNode");
const QString STR_PARAMETER_LIST = QStringLiteral("ParameterList");
const QString STR_PARAMETER = QStringLiteral("Parameter");
const QString STR_PATTERN_LIST = QStringLiteral("PatternList");
const QString STR_PATTERN = QStringLiteral("Pattern");
const QString STR_PATTERNSTRING = QStringLiteral("PatternString");
const QString STR_PRIORITY_OVERRIDE = QStringLiteral("PriorityOverride");
const QString STR_VALUE_OVERWRITE_LIST = QStringLiteral("ValueOverwriteList");
const QString STR_OVERWRITE = QStringLiteral("Overwrite");
const QString STR_CHILD_LIST = QStringLiteral("ChildList");
const QString STR_CHILD = QStringLiteral("Child");
const QString STR_EXIT_PATTERN_LIST = QStringLiteral("ExitPatternList");
const QString STR_EXIT_PATTERN = QStringLiteral("ExitPattern");
const QString STR_TO_IRNODE = QStringLiteral("ToIRNode");
const QString STR_VALUE_TRANSFORM_LIST = QStringLiteral("ValueTransformList");
const QString STR_VALUE_TRANSFORM = QStringLiteral("Transform");
const QString STR_IRNODE_PARAM = QStringLiteral("DestinationIRNodeParameter");

void writeAsElement(QXmlStreamWriter& xml, const QString& name, const QString& value)
{
    xml.writeStartElement(name);
    xml.writeCharacters(value);
    xml.writeEndElement();
}

void writeAsElement(QXmlStreamWriter& xml, const QString& name, int id, const QString& value)
{
    xml.writeStartElement(name);
    xml.writeAttribute(STR_INDEX, QString::number(id));
    xml.writeCharacters(value);
    xml.writeEndElement();
}

} // end of anonymous namespace

void XML::writeParser(const ParserPolicy& p, QXmlStreamWriter& xml)
{
    xml.writeStartElement(STR_PARSER);
    xml.writeAttribute(STR_NAME, p.name);

    // other attributes written as element
    writeAsElement(xml, STR_EXPR_START,     p.exprStartMark);
    writeAsElement(xml, STR_EXPR_END,       p.exprEndMark);
    writeAsElement(xml, STR_ROOTNODE_NAME,  p.rootParserNodeName);

    // match pairs
    xml.writeStartElement(STR_MATCHPAIR_LIST);
    for(int i = 0, n = p.matchPairs.size(); i < n; ++i){
        const auto& m = p.matchPairs.at(i);
        xml.writeStartElement(STR_MATCHPAIR);
        xml.writeAttribute(STR_NAME, m.name);
        xml.writeAttribute(STR_INDEX, QString::number(i));
        for(const auto& start : m.startEquivalentSet){
            writeAsElement(xml, STR_MATCHPAIR_START, start);
        }
        for(const auto& end : m.endEquivalentSet){
            writeAsElement(xml, STR_MATCHPAIR_END, end);
        }
        xml.writeEndElement(); // MatchPair element
    }
    xml.writeEndElement(); // MatchPairList element

    // ignore list
    xml.writeStartElement(STR_IGNORE_LIST);
    for(const auto& ignore : p.ignoreList){
        writeAsElement(xml, STR_IGNORE, ignore);
    }
    xml.writeEndElement(); // IgnoreList element

    // parser node list
    xml.writeStartElement(STR_PARSERNODE_LIST);
    for(int i = 0, n = p.nodes.size(); i < n; ++i){
        const auto& node = p.nodes.at(i);
        xml.writeStartElement(STR_PARSERNODE);
        xml.writeAttribute(STR_NAME, node.name);
        xml.writeAttribute(STR_INDEX, QString::number(i));

        // node parameters
        xml.writeStartElement(STR_PARAMETER_LIST);
        for(int i = 0, n = node.parameterNameList.size(); i < n; ++i){
            xml.writeStartElement(STR_PARAMETER);
            xml.writeAttribute(STR_NAME, node.parameterNameList.at(i));
            xml.writeAttribute(STR_INDEX, QString::number(i));
            xml.writeEndElement(); // Parameter element
        }
        xml.writeEndElement(); // ParameterList element

        // patterns
        xml.writeStartElement(STR_PATTERN_LIST);
        for(int i = 0, n = node.patterns.size(); i < n; ++i){
            const auto& p = node.patterns.at(i);
            xml.writeStartElement(STR_PATTERN);
            xml.writeAttribute(STR_INDEX, QString::number(i));

            // pattern string
            writeAsElement(xml, STR_PATTERNSTRING, p.patternString);

            // priority override
            writeAsElement(xml, STR_PRIORITY_OVERRIDE, QString::number(p.priorityScore));

            xml.writeStartElement(STR_VALUE_OVERWRITE_LIST);
            for(int i = 0, n = p.valueOverwriteList.size(); i < n; ++i){
                const auto& record = p.valueOverwriteList.at(i);
                xml.writeStartElement(STR_OVERWRITE);
                xml.writeAttribute(STR_PARAMETER, record.paramName);
                xml.writeCharacters(record.valueExpr);
                xml.writeEndElement();
            }
            xml.writeEndElement(); // ValueOverwriteList element

            xml.writeEndElement(); // Pattern element
        }
        xml.writeEndElement(); // PatternList element

        xml.writeStartElement(STR_CHILD_LIST);
        for(int i = 0, n = node.childNodeNameList.size(); i < n; ++i){
            writeAsElement(xml, STR_CHILD, i, node.childNodeNameList.at(i));
        }
        xml.writeEndElement(); // ChildList element

        xml.writeStartElement(STR_EXIT_PATTERN_LIST);
        for(int i = 0, n = node.earlyExitPatterns.size(); i < n; ++i){
            writeAsElement(xml, STR_EXIT_PATTERN, node.earlyExitPatterns.at(i));
        }
        xml.writeEndElement(); // ExitPatternList element

        writeAsElement(xml, STR_TO_IRNODE, node.combineToNodeTypeName);

        xml.writeStartElement(STR_VALUE_TRANSFORM_LIST);
        for(auto iter = node.combinedNodeParams.begin(), iterEnd = node.combinedNodeParams.end(); iter != iterEnd; ++iter){
            xml.writeStartElement(STR_VALUE_TRANSFORM);
            writeAsElement(xml, STR_IRNODE_PARAM, iter.key());
            for(int i = 0, n = iter.value().size(); i < n; ++i){
                writeAsElement(xml, STR_IRNODE_PARAM, i, iter.value().at(i));
            }
            xml.writeEndElement(); // ValueTransform element
        }
        xml.writeEndElement(); // ValueTransformList element

        xml.writeEndElement(); // ParserNode element
    }
    xml.writeEndElement(); // ParserNodeList element

    xml.writeEndElement(); // Parser element
}

Parser* XML::readParser(const IRRootType& ty, DiagnosticEmitterBase& diagnostic, QXmlStreamReader& xml)
{
    Q_UNUSED(ty)
    Q_UNUSED(diagnostic)
    Q_UNUSED(xml)
    return nullptr;
}
