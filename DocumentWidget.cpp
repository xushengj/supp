#include "DocumentWidget.h"
#include "DocumentEdit.h"
#include <QHBoxLayout>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QCryptographicHash>
#include <QDebug>
#include <QRegularExpression>
#include <cassert>

#ifndef HASH_ALGORITHM
#define HASH_ALGORITHM QCryptographicHash::Keccak_512
#endif

#ifndef FILE_READ_FLAG
#define FILE_READ_FLAG (QIODevice::ReadOnly | QIODevice::Text)
#endif

#ifndef FILE_WRITE_FLAG
#define FILE_WRITE_FLAG (QIODevice::WriteOnly | QIODevice::Text)
#endif

DocumentWidget::DocumentWidget(QString filePath, QWidget *parent)
    : QWidget(parent),
      ui_editor(nullptr),
      rawFilePath(filePath),
      absoluteFilePath(),
      fileName(),
      lastAccessTimeStamp(),
      lastAccessFileSize(0),
      lastAccessHash(),
      currentOpenMode(OpenMode::ReadWrite),
      dirtyFlag(false)
{
    ui_editor = new DocumentEdit(this);
    QHBoxLayout* layout = new QHBoxLayout();
    layout->addWidget(ui_editor);
    layout->setMargin(0);
    setLayout(layout);

    initialize();
    connect(ui_editor, &QPlainTextEdit::textChanged, this, &DocumentWidget::setDirtyFlag);
}

void DocumentWidget::reassociate(QString filePath)
{
    rawFilePath = filePath;
    absoluteFilePath.clear();
    fileName.clear();
    lastAccessTimeStamp = QDateTime();
    lastAccessFileSize = 0;
    lastAccessHash.clear();
    currentOpenMode = OpenMode::ReadWrite;
    dirtyFlag = false;
    DirtySignalDisabler lock(this);
    ui_editor->clear();
    initialize();
    emit stateChanged();
}

bool DocumentWidget::initialize()
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
    currentOpenMode = f.isWritable()? OpenMode::RWFollow: OpenMode::ReadFollow;
    absoluteFilePath = f.absoluteFilePath();
    fileName = f.fileName();
    lastAccessHash.clear();

    // do not bother to read file if it is zero sized
    if(f.size() == 0)
        return true;

    QCryptographicHash hash(HASH_ALGORITHM);
    // get the hash first
    if(!hash.addData(&file)){
        qDebug() << "File open success but failed to read";
        return true;
    }
    // seek back to beginning of file
    if(!file.reset()){
        qDebug() << "QFile.reset() fail";
        return true;
    }
    // do not set the hash unless the previous call is successful
    lastAccessHash = hash.result();
    setDocumentContent(&file);
    file.close();
    dirtyFlag = false;
    return true;
}

void DocumentWidget::setDocumentContent(QIODevice *src)
{
    QTextStream ts(src);
    ui_editor->setPlainText(ts.readAll());
}

QByteArray DocumentWidget::getDocumentContent()
{
    return ui_editor->toPlainText().toUtf8();
}

void DocumentWidget::setDirtyFlag()
{
    if(!dirtyFlag){
        dirtyFlag = true;
        emit stateChanged();
    }
}

void DocumentWidget::fileRecheckRequested()
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
    bool isFileAccessFailed = false;
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

            QCryptographicHash hash(HASH_ALGORITHM);
            // get the hash first
            // if any call fails, the hash won't match
            if(!file.open(FILE_READ_FLAG) || !hash.addData(&file)){
                qDebug() << "File open success but failed to read";
                isFileAccessFailed = true;
            }
            newHash = hash.result();
            if(newHash.length() != lastAccessHash.length()){
                // no idea when would this occur if both size is non-zero..
                isFileModified = true;
            }else{
                for(int i = 0, len = newHash.length(); i < len; ++i){
                    if(newHash.at(i) != lastAccessHash.at(i)){
                        isFileModified = true;
                        break;
                    }
                }
            }
        }
        // modification should already been pulled in
    }else{
        // file size mismatch; file must already been modified
        isFileModified = true;
    }

    // if file access failed, set flag and return anyway
    if(isFileAccessFailed){
        dirtyFlag = true;
        emit stateChanged();
        return;
    }

    if(isFileModified){
        switch(currentOpenMode){
        default: break; // should not happen
        case OpenMode::RWFollow:
        case OpenMode::ReadFollow:{
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
        }break;
        case OpenMode::ReadOnly:
        case OpenMode::ReadWrite:{
            dirtyFlag = true;
            emit stateChanged();
            return;
        }/*break;*/
        }
    }
}

bool DocumentWidget::saveToFile(QString filePath)
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

    lastAccessHash = QCryptographicHash::hash(data, HASH_ALGORITHM);
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

bool DocumentWidget::isEmpty() const
{
    return ui_editor->document()->isEmpty();
}

QString DocumentWidget::getFileName() const
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

QString DocumentWidget::getTabDisplayName() const
{
    QString name;
    if(dirtyFlag)
        name.append('*');

    name.append(getFileName());

    return name;
}

QString DocumentWidget::getTitleDisplayName() const
{
    if(absoluteFilePath.isEmpty())
        return getTabDisplayName();

    QString name;
    if(dirtyFlag)
        name.append('*');

    name.append(absoluteFilePath);
    return name;
}

DirtySignalDisabler::DirtySignalDisabler(DocumentWidget* doc)
    : doc(doc)
{
    QObject::disconnect(doc->ui_editor, &QPlainTextEdit::textChanged, doc, &DocumentWidget::setDirtyFlag);
}
DirtySignalDisabler::~DirtySignalDisabler()
{
    QObject::connect(doc->ui_editor, &QPlainTextEdit::textChanged, doc, &DocumentWidget::setDirtyFlag);
}
