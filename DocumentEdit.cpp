#include "DocumentEdit.h"
#include <QFontMetrics>
#include <QPainter>
#include <QTextBlock>

#ifndef LEFT_PADDING
#define LEFT_PADDING 4
#endif

#ifndef RIGHT_PADDING
#define RIGHT_PADDING 4
#endif

DocumentEdit::DocumentEdit(QWidget *parent)
    : QPlainTextEdit(parent),
      lineNumberArea(nullptr)
{
    lineNumberArea = new DocumentLineNumberArea(this);

    connect(this, &DocumentEdit::blockCountChanged, this, &DocumentEdit::updateLineNumberAreaWidth);
    connect(this, &DocumentEdit::updateRequest,     this, &DocumentEdit::updateLineNumberArea);
    connect(this, &DocumentEdit::cursorPositionChanged, this, &DocumentEdit::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
}

void DocumentEdit::updateLineNumberAreaWidth(int newBlockCount)
{
    Q_UNUSED(newBlockCount)
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void DocumentEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void DocumentEdit::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = QColor(Qt::lightGray).lighter(120);

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

int DocumentEdit::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    int space = LEFT_PADDING + RIGHT_PADDING + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;

    return space;
}

void DocumentEdit::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);

    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void DocumentEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), QColor(Qt::lightGray).lighter(120));
    painter.setPen(QColor(Qt::gray));
    painter.drawLine(event->rect().topRight(), event->rect().bottomRight());
    painter.setPen(QColor(Qt::black));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(Qt::black);
            painter.drawText(0, top, lineNumberArea->width() - RIGHT_PADDING, fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
