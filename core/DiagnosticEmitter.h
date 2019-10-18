#ifndef DIAGNOSTICEMITTER_H
#define DIAGNOSTICEMITTER_H

#include <QObject>

class DiagnosticEmitterBase
{
public:
    virtual ~DiagnosticEmitterBase(){}
    virtual void pushNode(QString coarseDescription)    {pathList.push_back(coarseDescription);}
    virtual void attachDescriptiveName(QString name)    {pathList.back() = pathList.back().arg(name);}
    virtual void popNode()                              {pathList.pop_back();}
    // optional text is for strings that:
    // 1. may not be rendered correctly (e.g. contains illegal characters)
    // 2. may be too long
    virtual void info   (QString category, QString text, QString optionalText = QString()) = 0;
    virtual void warning(QString category, QString text, QString optionalText = QString()) = 0;
    virtual void error  (QString category, QString text, QString optionalText = QString()) = 0;
protected:
    QStringList pathList;
};

// just for testing purpose; we will have GUI oriented implementation later on
class ConsoleDiagnosticEmitter: public DiagnosticEmitterBase
{
public:
    virtual ~ConsoleDiagnosticEmitter() override {}
    virtual void info   (QString category, QString text, QString optionalText = QString()) override;
    virtual void warning(QString category, QString text, QString optionalText = QString()) override;
    virtual void error  (QString category, QString text, QString optionalText = QString()) override;
};

/*
class DiagnosticEmitter : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticEmitter(QObject *parent = nullptr);

signals:

public slots:
};
*/

#endif // DIAGNOSTICEMITTER_H
