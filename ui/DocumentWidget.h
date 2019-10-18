#ifndef DOCUMENTWIDGET_H
#define DOCUMENTWIDGET_H

#include <QWidget>
#include <QMenu>
#include <QString>

class DocumentWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief createInstance
     * @param filePath
     * @return
     */
    static DocumentWidget* createInstance(QString filePath);

    DocumentWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {}

    virtual bool isDirty() const = 0;

    /**
     * @brief getAbsoluteFilePath get the absolute file path (for duplicate opening detection)
     * @return absolute file path if this DocumentWidget is associated with a file; empty string otherwise
     */
    virtual QString getAbsoluteFilePath() const = 0;

    /**
     * @brief getFileName get only the file name (for close dialog warning purpose)
     * @return file name without path
     */
    virtual QString getFileName() const = 0;

    /**
     * @brief saveToFile try to save the document to specified file path
     * @param filePath the new path to save to. Empty if saving to currently associated one
     * @return true if save is successful, false otherwise
     */
    virtual bool saveToFile(QString filePath = QString()) = 0;

    /**
     * @brief getMenu return a menu for right click pop-up menu
     * @return pointer to a new menu object for this document
     */
    virtual QMenu* getMenu() {return nullptr;}

    //-------------------------------------------------------------------------
    // optional public functions

    virtual bool isReadOnly() const {return false;}

    /**
     * @brief isEmpty returns if it is safe to remove this DocumentWidget if an open request succeeds
     * @return true if the DocumentWidget do not have unsaved data and is not associated with any file, false otherwise
     */
    virtual bool isEmpty() const {return !isDirty() && getAbsoluteFilePath().isEmpty();}

    /**
     * @brief getTabDisplayName get the string for showing file name on tab
     * @return string for tab name
     */
    virtual QString getTabDisplayName() const;

    /**
     * @brief getTitleDisplayName get the string for showing file path on window title
     * @return string for window title prefix
     */
    virtual QString getTitleDisplayName() const;

signals:
    /**
     * @brief stateChanged emitted when open mode, file association, or dirty flag changes
     */
    void stateChanged();

public slots:
    /**
     * @brief checkFileUpdate request for checking the status of associated file
     */
    void checkFileUpdate();

protected:
    /**
     * @brief fileRecheck will be called from checkFileUpdate()
     */
    virtual void fileRecheck() = 0;
};



#endif // DOCUMENTWIDGET_H
