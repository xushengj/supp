#ifndef DOCUMENTEDIT_H
#define DOCUMENTEDIT_H

#include <QObject>
#include <QWidget>
#include <QPlainTextEdit>

// reference: https://doc.qt.io/qt-5/qtwidgets-widgets-codeeditor-example.html

class DocumentLineNumberArea;

class DocumentEdit : public QPlainTextEdit
{
    Q_OBJECT

    friend class DocumentLineNumberArea;
public:
    DocumentEdit(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &, int);
    void highlightCurrentLine();

private:
    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

private:
    DocumentLineNumberArea* lineNumberArea;
};

class DocumentLineNumberArea : public QWidget
{
    Q_OBJECT

public:
    DocumentLineNumberArea(DocumentEdit* editor) : QWidget(editor), editor(editor) {}

    QSize sizeHint() const override {
        return QSize(editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        editor->lineNumberAreaPaintEvent(event);
    }

private:
    DocumentEdit* editor;
};

#endif // DOCUMENTEDIT_H
