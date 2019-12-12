#ifndef XML_H
#define XML_H

#include <QIODevice>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

class DiagnosticEmitterBase;
class IRRootType;
class IRRootInstance;
class Parser;
class ParserPolicy;

namespace XML{
void writeIRInstance(const IRRootInstance& ir, QIODevice* dest);
IRRootInstance* readIRInstance(const IRRootType& ty, DiagnosticEmitterBase& diagnostic, QIODevice* src);

// xml should be at StartElement and the element is Parser
void writeParser(const ParserPolicy& p, QXmlStreamWriter& xml);
Parser* readParser(const IRRootType& ty, DiagnosticEmitterBase& diagnostic, QXmlStreamReader& xml);
}





#endif // XML_H
