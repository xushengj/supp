#ifndef OUTPUTHANDLERBASE_H
#define OUTPUTHANDLERBASE_H

#include <QtGlobal>
#include <QBuffer>
#include <QTextCodec>
#include <QTextEncoder>

#include "core/Value.h"

struct OutputDescriptor{
    // the base type of output
    // for now we only support text
    enum OutputBaseType{
        Text
    };
    struct TextOutputInfo{
        QString mimeType;   //!< the mime type that goes to clipboard; empty for text/plain
        QString codecName;  //!< the name of output codec
    };

    OutputBaseType baseTy;
    TextOutputInfo textInfo;
};

class OutputHandlerBase
{
public:
    virtual ~OutputHandlerBase(){}
    /**
     * @brief getAllowedOutputTypeList sets the type that can be accepted by output
     * @param tys the wb list for types being accepted
     */
    virtual void getAllowedOutputTypeList(QList<ValueType>& tys) const = 0;

    virtual bool isOutputGoodSoFar() {return true;}

    // it is the execution context's duty not to call addOutput with wrong type
    // return true if the output data is good, false otherwise
    virtual bool addOutput(const QString& data) {Q_UNUSED(data) Q_ASSERT(0); return false;}

};

class TextOutputHandler : public OutputHandlerBase
{
public:
    TextOutputHandler(const QByteArray& codecName);
    virtual ~TextOutputHandler() override;

    /**
     * @brief getResult stop the output and get output
     * @return reference to QByteArray output
     */
    const QByteArray& getResult();

    virtual void getAllowedOutputTypeList(QList<ValueType>& tys) const override;
    virtual bool isOutputGoodSoFar() override;
    virtual bool addOutput(const QString& data) override;

private:
    QBuffer buffer;
    QTextEncoder* encoder;
};

#endif // OUTPUTHANDLERBASE_H
