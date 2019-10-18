#include "ui/DocumentWidget.h"

#include "ui/PlainTextDocumentWidget.h"

DocumentWidget* DocumentWidget::createInstance(QString filePath)
{
    return new PlainTextDocumentWidget(filePath, nullptr);
}

QString DocumentWidget::getTabDisplayName() const
{
    QString name;

    if(isDirty())
        name.append('*');

    if(isReadOnly()){
        name.append(tr("[R]"));
        name.append(' ');
    }

    name.append(getFileName());

    return name;
}

QString DocumentWidget::getTitleDisplayName() const
{
    QString absolutePath = getAbsoluteFilePath();
    if(absolutePath.isEmpty())
        return getTabDisplayName();

    QString name;

    if(isDirty())
        name.append('*');

    if(isReadOnly()){
        name.append(tr("[R]"));
        name.append(' ');
    }

    name.append(absolutePath);
    return name;
}

void DocumentWidget::checkFileUpdate()
{
    fileRecheck();
}
