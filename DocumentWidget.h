#ifndef DOCUMENTWIDGET_H
#define DOCUMENTWIDGET_H

#include <QObject>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QDateTime>
#include <QByteArray>

class DocumentEdit;
class DirtySignalDisabler;

class DocumentWidget : public QWidget
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
    explicit DocumentWidget(QString filePath, QWidget *parent = nullptr);

    /**
     * @brief reassociate reopen on another file; old file association is dropped and dirty content discarded
     * @param filePath path to new file to be associated with
     */
    void reassociate(QString filePath);

    QString getAbsoluteFilePath()   const {return absoluteFilePath;}
    OpenMode getOpenMode()          const {return currentOpenMode;}
    bool isDirty()                  const {return dirtyFlag;}

    bool isEmpty() const;
    QString getFileName() const;
    QString getTabDisplayName() const;
    QString getTitleDisplayName() const;

    /**
     * @brief saveToFile try to save the document to specified file path
     * @param filePath the new path to save to. Empty if saving to currently associated one
     * @return true if save is successful, false otherwise
     */
    bool saveToFile(QString filePath = QString());

signals:
    /**
     * @brief stateChanged emitted when open mode, file association, or dirty flag changes
     */
    void stateChanged();

public slots:
    /**
     * @brief fileRecheckRequested request for checking the status of associated file
     */
    void fileRecheckRequested();
    void setDirtyFlag();

private:
    DocumentEdit* ui_editor;                //!< pointer to main editor widget; never null
    QString rawFilePath;                    //!< raw file path (only used before the file exists)
    QString absoluteFilePath;               //!< file path used for duplicate avoidance
    QString fileName;                       //!< file name, no path (e.g. for on-tab text)
    QDateTime lastAccessTimeStamp;          //!< timestamp of last access on file
    qint64 lastAccessFileSize;              //!< file size at last time it is accessed
    QByteArray lastAccessHash;              //!< hash of file at last access

    OpenMode currentOpenMode;               //!< open mode of associated file
    bool dirtyFlag = false;                 //!< dirty flag of current document

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
    friend class DocumentWidget;
private:
    DirtySignalDisabler(DocumentWidget* doc);
    ~DirtySignalDisabler();
    DocumentWidget* doc;
};

#endif // DOCUMENTWIDGET_H
