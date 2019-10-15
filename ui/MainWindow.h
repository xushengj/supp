#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtGlobal>
#include <QMainWindow>
#include <QTabWidget>

#include "ui/DocumentWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow() override;

protected:
    void focusInEvent(QFocusEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

public slots:
    void newRequested();
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    void saveAllRequested();
    void closeRequested();
    void updateTitle();

private slots:
    void tabCloseSlot(int index);
    void fileCheckRelaySlot(int index);

private:
    /**
     * @brief closeDirtyDocumentConfirmation ask user operation if trying to close a dirty document
     * @param doc DocumentWidget with dirty flag set
     * @return true if dirty state is resolved (saved or confirm close), false otherwise
     */
    bool closeDirtyDocumentConfirmation(DocumentWidget* doc);
    bool trySaveDocument(DocumentWidget* doc, bool saveAs);

    bool closeWindowConfirmation();

    /**
     * @brief findAlreadyOpenedWidget given a path, check if one document is already associated with the file
     * @param path the file path for testing; not necessarily absolute
     * @param srcDoc the source document to exclude from returning
     * @param srcDocMatch written to true if srcDoc is found to have matching absolute path
     * @return pointer to the DocumentWidget that is not srcDoc and has the same absolute path
     */
    DocumentWidget* findAlreadyOpenedWidget(QString path, DocumentWidget* srcDoc, bool& srcDocMatch);

    DocumentWidget* getCurrentDocumentWidget() {
        QWidget* widget = tabWidget->currentWidget();
        Q_ASSERT(widget && "Broken invariant: tabWidget->currentWidget() returns nullptr!");
        DocumentWidget* doc = qobject_cast<DocumentWidget*>(widget);
        Q_ASSERT(doc && "Broken invariant: tabWidget->currentWidget() returns QWidget other than DocumentWidget!");
        return doc;
    }
    DocumentWidget* getDocumentWidget(int index) {
        QWidget* widget = tabWidget->widget(index);
        Q_ASSERT(widget && "Broken invariant: tabWidget->widget() returns nullptr!");
        DocumentWidget* doc = qobject_cast<DocumentWidget*>(widget);
        Q_ASSERT(doc && "Broken invariant: tabWidget->currentWidget() returns QWidget other than DocumentWidget!");
        return doc;
    }

    QString getDefaultSavePath() {return lastSavePath;}
    QString getDefaultOpenPath() {return lastOpenPath;}
    void installNewDocumentWidget(DocumentWidget* doc);
    void handleCloseDocument(DocumentWidget* doc);

private:
    Ui::MainWindow *ui;

    QTabWidget* tabWidget;

    QString lastSavePath;
    QString lastOpenPath;
};
#endif // MAINWINDOW_H
