#include "ui/MainWindow.h"
#include "ui_MainWindow.h"

#include "ui/DocumentWidget.h"

#include <QtGlobal>
#include <QMessageBox>
#include <QFileDialog>
#include <QCloseEvent>
#include <QTabBar>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      tabWidget(nullptr)
{
    ui->setupUi(this);

    ForwardingTabWidget* tab = new ForwardingTabWidget(this);
    tabWidget = tab;
    tabWidget->setMovable(true);
    tabWidget->setTabsClosable(true);
    ui->centralwidget->layout()->addWidget(tabWidget);

    // create a temporary one
    newRequested();

    connect(tabWidget, &QTabWidget::currentChanged, this, &MainWindow::updateTitle);
    connect(tabWidget, &QTabWidget::currentChanged, this, &MainWindow::fileCheckRelaySlot);
    connect(tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::tabCloseSlot);

    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newRequested);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openRequested);
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveRequested);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveAsRequested);
    connect(ui->actionSaveAll, &QAction::triggered, this, &MainWindow::saveAllRequested);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeRequested);

    setFocusPolicy(Qt::StrongFocus);
    tab->setTabBarEventFilter(this);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::focusInEvent(QFocusEvent *event)
{
    getCurrentDocumentWidget()->checkFileUpdate();
    QMainWindow::focusInEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(!closeWindowConfirmation()){
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // return false to pass through the event and true to intercept it

    // get right click event on tab
    if(obj == static_cast<QObject*>(tabWidget->tabBar())){
        if(event->type() == QEvent::MouseButtonPress){
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();
            int index = tabWidget->currentIndex();
            for(int i = 0, num = tabWidget->count(); i < num; ++i){
                if(tabWidget->tabBar()->tabRect(i).contains(pos)){
                    index = i;
                    break;
                }
            }
            if(mouseEvent->button() == Qt::RightButton){
                QMenu* menu = getDocumentWidget(index)->getMenu();
                if(menu){
                    menu->exec(mouseEvent->globalPos());
                    delete menu;
                    return true;
                }
            }
        }
    }

    return false;
}

void MainWindow::installNewDocumentWidget(DocumentWidget* doc)
{
    tabWidget->addTab(doc, doc->getTabDisplayName());
    connect(doc, &DocumentWidget::stateChanged, [=](){
        // update display name
        int index = tabWidget->indexOf(doc);
        tabWidget->setTabText(index, doc->getTabDisplayName());
        if(index == tabWidget->currentIndex()){
            updateTitle();
        }
    });
    tabWidget->setCurrentWidget(doc);
    updateTitle();
}

void MainWindow::newRequested()
{
    installNewDocumentWidget(DocumentWidget::createInstance(QString()));
}

void MainWindow::openRequested()
{
    QString path = QFileDialog::getOpenFileName(this,
                                                /* title */ tr("Open file"),
                                                /* dir   */ getDefaultOpenPath(),
                                                /* filter */tr("Any file (*.*)"));
    if(path.isEmpty())
        return;

    lastOpenPath = path;
    bool dummy = false;
    DocumentWidget* doc = findAlreadyOpenedWidget(path, nullptr, dummy);
    if(doc){
        tabWidget->setCurrentWidget(doc);
        return;
    }

    // if there is only one document opening and it is not associated with any file yet, remove that page after opening the specified document
    DocumentWidget* docToDelete = nullptr;

    if(tabWidget->count() == 1){
        DocumentWidget* curDoc = getCurrentDocumentWidget();
        if(curDoc->getAbsoluteFilePath().isEmpty() && curDoc->isEmpty()){
            docToDelete = curDoc;
        }
    }

    doc = DocumentWidget::createInstance(path);

    if(doc->getAbsoluteFilePath().isEmpty()){
        QMessageBox::warning(this, tr("Open fail"), tr("Specified file cannot be opened"));
        delete doc;
        return;
    }

    installNewDocumentWidget(doc);

    if(docToDelete){
        tabWidget->removeTab(tabWidget->indexOf(docToDelete));
        docToDelete->deleteLater();
    }

    updateTitle();
}

void MainWindow::tabCloseSlot(int index)
{
    handleCloseDocument(getDocumentWidget(index));
}

void MainWindow::closeRequested()
{
    handleCloseDocument(getCurrentDocumentWidget());
}

void MainWindow::handleCloseDocument(DocumentWidget* doc)
{
    if(doc->isDirty()){
        if(!closeDirtyDocumentConfirmation(doc)){
            return;
        }
    }

    // trying to remove the last widget will cause a crash
    // create a new one first if this is the case
    if(tabWidget->count() <= 1){
        Q_ASSERT(static_cast<QWidget*>(doc) == tabWidget->currentWidget());
        newRequested();
    }
    tabWidget->removeTab(tabWidget->indexOf(static_cast<QWidget*>(doc)));
    doc->deleteLater();
}

void MainWindow::updateTitle()
{
    DocumentWidget* doc = getCurrentDocumentWidget();
    QString title = doc->getTitleDisplayName();
    title.append(tr(" - ","concatenation string between current file and application name"));
    title.append(QCoreApplication::applicationName());
    setWindowTitle(title);
}

void MainWindow::fileCheckRelaySlot(int index)
{
    getDocumentWidget(index)->checkFileUpdate();
}

bool MainWindow::closeWindowConfirmation()
{
    int dirtyCount = 0;

    for(int i = 0, count = tabWidget->count(); i < count; ++i){
        if(getDocumentWidget(i)->isDirty()){
            dirtyCount += 1;
        }
    }

    if(dirtyCount > 0){
        if(QMessageBox::warning(this,
                                /* title     */ tr("Close without save"),
                                /* text      */ tr("%1 file(s) have unsaved changes. Discard all changes and exit?", "", dirtyCount).arg(dirtyCount),
                                /* button    */ QMessageBox::Discard | QMessageBox::Cancel,
                                /* default   */ QMessageBox::Cancel
                                ) != QMessageBox::Discard){
            return false;
        }
    }

    return true;
}

void MainWindow::saveRequested()
{
    trySaveDocument(getCurrentDocumentWidget(), false);
}

void MainWindow::saveAsRequested()
{
    trySaveDocument(getCurrentDocumentWidget(), true);
}

void MainWindow::saveAllRequested()
{
    for(int i = 0, count = tabWidget->count(); i < count; ++i){
        DocumentWidget* doc = getDocumentWidget(i);
        if(doc->isDirty()){
            // stop upon first failure
            tabWidget->setCurrentIndex(i);
            if(!trySaveDocument(doc, false)){
                return;
            }
        }
    }
}

bool MainWindow::closeDirtyDocumentConfirmation(DocumentWidget* doc)
{
    auto result = QMessageBox::question(this,
                             /* title    */ tr("Save file"),
                             /* text     */ tr("Save file \"%1\"?").arg(doc->getFileName()),
                             /* button   */ QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                             /* default  */ QMessageBox::Yes);
    if(result == QMessageBox::Cancel)
        return false;

    if(result == QMessageBox::Yes){
       return trySaveDocument(doc, false);
    }

    return true;
}

bool MainWindow::trySaveDocument(DocumentWidget* doc, bool saveAs)
{
    QString path = doc->getAbsoluteFilePath();
    if(path.isEmpty() || saveAs){
        QString startPath = doc->getAbsoluteFilePath();
        if(startPath.isEmpty()){
            startPath = getDefaultSavePath();
        }
        path = QFileDialog::getSaveFileName(this,
                                            /* title */ tr("Save file"),
                                            /* dir */   startPath,
                                            /* filter */tr("Any file (*.*)"));
        if(path.isEmpty())
            return false;

        lastSavePath = path;
    }

    // make sure no other DocumentWidget is referencing the same file
    bool isSrcDocMatch = false;
    if(findAlreadyOpenedWidget(path, doc, isSrcDocMatch)){
        QMessageBox::critical(this, tr("Error"), tr("The file is already opened in supp!"));
        return false;
    }

    // let doc know that we are just saving to the same file if this is the case
    if(isSrcDocMatch)
        path.clear();

    if(!doc->saveToFile(path)){
        QMessageBox::critical(this, tr("Error"), tr("File save failed. Please check permission, available disk space, and existence of parent directories."));
        return false;
    }

    return true;
}

DocumentWidget* MainWindow::findAlreadyOpenedWidget(QString path, DocumentWidget* srcDoc, bool& srcDocMatch)
{
    QFileInfo f(path);
    QString absolutePath = f.absoluteFilePath();
    for(int i = 0, count = tabWidget->count(); i < count; ++i){
        DocumentWidget* currentDoc = getDocumentWidget(i);
        if(currentDoc->getAbsoluteFilePath() == absolutePath){
            if(currentDoc == srcDoc){
                srcDocMatch = true;
                continue;
            }
            return currentDoc;
        }
    }
    return nullptr;
}
