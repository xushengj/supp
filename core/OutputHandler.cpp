#include "core/OutputHandlerBase.h"

TextOutputHandler::TextOutputHandler(const QByteArray& codecName)
    : encoder(QTextCodec::codecForName(codecName)->makeEncoder(QTextCodec::IgnoreHeader|QTextCodec::ConvertInvalidToNull))
{
    Q_ASSERT(encoder);
    buffer.open(QIODevice::WriteOnly);
}

TextOutputHandler::~TextOutputHandler()
{
    delete encoder;
}

void TextOutputHandler::getAllowedOutputTypeList(QList<ValueType>& tys) const
{
    tys.clear();
    tys.push_back(ValueType::String);
}

bool TextOutputHandler::isOutputGoodSoFar()
{
    return encoder->hasFailure();
}

bool TextOutputHandler::addOutput(const QString& data)
{
    QByteArray out = encoder->fromUnicode(data);
    buffer.write(out);
    return encoder->hasFailure();
}

const QByteArray& TextOutputHandler::getResult()
{
    Q_ASSERT(buffer.isOpen());
    buffer.close();
    return buffer.data();
}
