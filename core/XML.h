#ifndef XML_H
#define XML_H

#include <QIODevice>

class DiagnosticEmitterBase;
class IRRootType;
class IRRootInstance;

bool writeToXML(const IRRootInstance& ir, DiagnosticEmitterBase& diagnostic, QIODevice* dest);
IRRootInstance* readFromXML(const IRRootType& ty, DiagnosticEmitterBase& diagnostic, QIODevice* src);

#endif // XML_H
