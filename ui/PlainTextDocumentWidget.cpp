#include "ui/PlainTextDocumentWidget.h"

#include "ui/DocumentEdit.h"

#include <QHBoxLayout>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <QTextCodec>

#ifndef FILE_READ_FLAG
#define FILE_READ_FLAG (QIODevice::ReadOnly | QIODevice::Text)
#endif

#ifndef FILE_WRITE_FLAG
#define FILE_WRITE_FLAG (QIODevice::WriteOnly | QIODevice::Text)
#endif

PlainTextDocumentWidget::PlainTextDocumentWidget(QString filePath, QWidget *parent)
    : DocumentWidget(parent),
      ui_editor(nullptr),
      rawFilePath(filePath),
      absoluteFilePath(),
      fileName(),
      lastAccessTimeStamp(),
      lastAccessFileSize(0)
{
    ui_editor = new DocumentEdit(this);
    QHBoxLayout* layout = new QHBoxLayout();
    layout->addWidget(ui_editor);
    layout->setMargin(0);
    setLayout(layout);

    initialize();
    connect(ui_editor, &QPlainTextEdit::textChanged, this, &PlainTextDocumentWidget::setDirtyFlag);
}

bool PlainTextDocumentWidget::initialize()
{
    if(rawFilePath.isEmpty())
        return true;

    QFile file(rawFilePath);
    if(!file.open(FILE_READ_FLAG)){
        // failed to open the file for any reasons
        return false;
    }

    // now we know for sure we can access it
    QFileInfo f(file);
    lastAccessTimeStamp = QDateTime::currentDateTime();
    lastAccessFileSize = f.size();
    readOnlyFlag = !f.isWritable();
    followFlag = true;
    absoluteFilePath = f.absoluteFilePath();
    fileName = f.fileName();

    // do not bother to read file if it is zero sized
    if(f.size() == 0)
        return true;

    setDocumentContent(&file);
    file.close();
    dirtyFlag = false;
    return true;
}

void PlainTextDocumentWidget::setDocumentContent(QIODevice *src)
{
    QTextStream ts(src);
    ts.setCodec(QTextCodec::codecForName("UTF-8"));
    ui_editor->setPlainText(ts.readAll());
}

QByteArray PlainTextDocumentWidget::getDocumentContent()
{
    return ui_editor->toPlainText().toUtf8();
}

void PlainTextDocumentWidget::setDirtyFlag()
{
    if(!dirtyFlag){
        dirtyFlag = true;
        emit stateChanged();
    }
}

void PlainTextDocumentWidget::setReadOnly(bool isReadOnly)
{
    readOnlyFlag = isReadOnly;
    ui_editor->setReadOnly(isReadOnly);
    emit stateChanged();
}

void PlainTextDocumentWidget::fileRecheck()
{
    if(absoluteFilePath.isEmpty()){
        // the file do not exist yet
        if(initialize()){
            // initialization successful
            emit stateChanged();
        }
        return;
    }

    // the file already exist and we already loaded a copy
    // first of all, do nothing if dirty flag is set
    if(dirtyFlag)
        return;

    QFileInfo f(absoluteFilePath);

    if(!f.exists()){
        // the file stops to exist
        // set dirty flag and stop now
        dirtyFlag = true;
        emit stateChanged();
        return;
    }

    // if file is modified, perform different action under different mode
    QFile file(absoluteFilePath);
    QByteArray newHash;
    // check file size first
    bool isFileModified = false;
    if(lastAccessFileSize == f.size()){
        // empty files are all the same
        if(lastAccessFileSize == 0)
            return;

        QDateTime currentTimestamp = f.lastModified();
        if(lastAccessTimeStamp < currentTimestamp){
            // we have a more recent modify
            // check if hash matches

            isFileModified = true;
        }
        // modification should already been pulled in
    }else{
        // file size mismatch; file must already been modified
        isFileModified = true;
    }

    if(isFileModified){
        if(followFlag){
            // reload file content
            if(!file.isOpen()){
                if(!file.open(FILE_READ_FLAG)){
                    dirtyFlag = true;
                    emit stateChanged();
                    return;
                }
            }else{
                if(!file.reset()){
                    dirtyFlag = true;
                    emit stateChanged();
                    return;
                }
            }
            // save current cursor position
            QTextCursor currentCursor = ui_editor->textCursor();
            int blockNum = currentCursor.blockNumber();
            int columnNum = currentCursor.positionInBlock();
            DirtySignalDisabler lock(this);
            setDocumentContent(&file);
            file.close();
            int currentBlockCount = ui_editor->blockCount();
            QTextCursor newCursor = ui_editor->textCursor();
            if(blockNum >= currentBlockCount){
                // scroll to end of document
                newCursor.movePosition(QTextCursor::End);
            }else{
                if(blockNum > 0){
                    newCursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, blockNum);
                }
                newCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, columnNum);
            }
            ui_editor->setTextCursor(newCursor);
            dirtyFlag = false;
        }else{ // !followFlag
            dirtyFlag = true;
            emit stateChanged();
            return;
        }
    }
}

bool PlainTextDocumentWidget::saveToFile(QString filePath)
{
    bool isNewFile = !filePath.isEmpty();
    QString newAbsolutePath;
    QString newFileName;
    if(!filePath.isEmpty()){
        QFileInfo f(filePath);
        newAbsolutePath = f.absoluteFilePath();
        if(newAbsolutePath == absoluteFilePath){
            isNewFile = false;
        }else{
            newFileName = f.fileName();
        }
    }

    QSaveFile file((isNewFile? newAbsolutePath: absoluteFilePath));
    if(!file.open(FILE_WRITE_FLAG)){
        return false;
    }

    QByteArray data = getDocumentContent();
    file.write(data);
    if(!file.commit()){
        return false;
    }
    data.clear();

    if(isNewFile){
        rawFilePath = filePath;
        absoluteFilePath = newAbsolutePath;
        fileName = newFileName;
    }
    QFileInfo f(absoluteFilePath);
    lastAccessFileSize = f.size();
    lastAccessTimeStamp = f.lastModified();
    if(dirtyFlag || isNewFile){
        dirtyFlag = false;
        emit stateChanged();
    }
    return true;
}

QMenu* PlainTextDocumentWidget::getMenu()
{
    QMenu* ptr = new QMenu;
    QAction* readonlyAct = new QAction(tr("Read-only"));
    readonlyAct->setCheckable(true);
    readonlyAct->setChecked(readOnlyFlag);
    connect(readonlyAct, &QAction::triggered, [=]()->void{
        setReadOnly(!readOnlyFlag);
    });
    ptr->addAction(readonlyAct);
    return ptr;
}

QString PlainTextDocumentWidget::getFileName() const
{
    if(!fileName.isEmpty()){
        return fileName;
    }else if(!rawFilePath.isEmpty()){
        // probably impossible..
        int dashIndex = rawFilePath.lastIndexOf(QRegularExpression("[/\\\\]"), -2);
        if(dashIndex >= 0){
            return rawFilePath.mid(dashIndex+1);
        }else{
            return rawFilePath;
        }
    }else{
        return tr("Unnamed","Tab display name for document without file associated");
    }
}


DirtySignalDisabler::DirtySignalDisabler(PlainTextDocumentWidget* doc)
    : doc(doc)
{
    QObject::disconnect(doc->ui_editor, &QPlainTextEdit::textChanged, doc, &PlainTextDocumentWidget::setDirtyFlag);
}
DirtySignalDisabler::~DirtySignalDisabler()
{
    QObject::connect(doc->ui_editor, &QPlainTextEdit::textChanged, doc, &PlainTextDocumentWidget::setDirtyFlag);
}
