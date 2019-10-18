#ifndef PLAINTEXTDOCUMENTWIDGET_H
#define PLAINTEXTDOCUMENTWIDGET_H

#include <QObject>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QDateTime>
#include <QByteArray>

#include "ui/DocumentWidget.h"

class DocumentEdit;
class DirtySignalDisabler;

class PlainTextDocumentWidget : public DocumentWidget
{
    Q_OBJECT

    friend class DirtySignalDisabler;
public:
    enum class OpenMode{
        ReadWrite,  //!< the most basic mode that reads and writes selected file; set dirty flag when file changed
        ReadOnly,   //!< disable editing on the file; set dirty flag when file changed
        ReadFollow, //!< disable editing on the file; auto reload when file changed
        RWFollow,   //!< same as ReadWrite except that if dirty flag is not set, this mode also do auto reload when file changed
    };

    /**
     * @brief DocumentWidget create instance with file association. open mode is automatically determined
     * @param filePath the raw path to file to be associated
     * @param parent parameter to QPlainTextEdit constructor
     */
    explicit PlainTextDocumentWidget(QString filePath, QWidget *parent = nullptr);

    QString getAbsoluteFilePath() const override {return absoluteFilePath;}
    QString getFileName() const override;
    bool isDirty() const override {return dirtyFlag;}
    bool isReadOnly() const override {return readOnlyFlag;}
    bool saveToFile(QString filePath) override;

    QMenu* getMenu() override;

protected:
    void fileRecheck() override;

private slots:
    void setDirtyFlag();
    void setReadOnly(bool isReadOnly);

private:
    DocumentEdit* ui_editor;                //!< pointer to main editor widget; never null
    QString rawFilePath;                    //!< raw file path (only used before the file exists)
    QString absoluteFilePath;               //!< file path used for duplicate avoidance
    QString fileName;                       //!< file name, no path (e.g. for on-tab text)
    QDateTime lastAccessTimeStamp;          //!< timestamp of last access on file
    qint64 lastAccessFileSize;              //!< file size at last time it is accessed

    bool dirtyFlag = false;                 //!< dirty flag of current document
    bool readOnlyFlag = false;              //!< whether current document is read only
    bool followFlag = true;                 //!< whether update the content if the file is changed while dirty flag is not set

    /**
     * @brief initialize try to initialize the document by accessing the file
     * @return true if the file is successfully read; false otherwise
     */
    bool initialize();

    /**
     * @brief setDocumentContent initialize document content from given source
     * @param src source of content (an open, initialized QFile probably)
     */
    void setDocumentContent(QIODevice *src);
    /**
     * @brief getDocumentContent get document content as byte array
     */
    QByteArray getDocumentContent();
};

class DirtySignalDisabler{
    friend class PlainTextDocumentWidget;
private:
    DirtySignalDisabler(PlainTextDocumentWidget* doc);
    ~DirtySignalDisabler();
    PlainTextDocumentWidget* doc;
};

#endif // PLAINTEXTDOCUMENTWIDGET_H
