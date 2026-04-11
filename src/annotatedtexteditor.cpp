#include "annotatedtexteditor.h"

#include <QFont>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QSize>
#include <QSet>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextLayout>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextCharFormat>
#include <QTimer>

namespace {

QString plainTextToHtml(const QString &text)
{
    QTextDocument document;
    document.setPlainText(text);
    return document.toHtml();
}

QString htmlToPlainText(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}

QTextCharFormat buildTextFormat(
    const AnnotatedTextEdit::TextAppearance &appearance,
    const QColor &backgroundColor,
    bool applyBackground = true)
{
    QTextCharFormat format;
    format.setForeground(appearance.textColor);
    if (applyBackground && backgroundColor.isValid()) {
        format.setBackground(backgroundColor);
    } else {
        format.clearBackground();
    }
    if (!appearance.family.isEmpty()) {
        format.setFontFamily(appearance.family);
    }
    if (appearance.pointSize > 0) {
        format.setFontPointSize(appearance.pointSize);
    }
    format.setFontWeight(appearance.fontWeight);
    format.setFontItalic(appearance.italic);
    format.setFontUnderline(appearance.underline);
    return format;
}

QTextBlockFormat buildSourceBlockFormat(const AnnotatedTextEdit::TextAppearance &appearance, const QColor &backgroundColor)
{
    QTextBlockFormat format;
    format.setLeftMargin(appearance.leftMargin);
    format.setTopMargin(appearance.topMargin);
    format.setBottomMargin(appearance.bottomMargin);
    if (backgroundColor.isValid()) {
        format.setBackground(backgroundColor);
    } else {
        format.clearBackground();
    }
    return format;
}

QTextBlockFormat buildCommentBlockFormat(const AnnotatedTextEdit::TextAppearance &appearance, const QColor &backgroundColor)
{
    QTextBlockFormat format;
    format.setLeftMargin(appearance.leftMargin);
    format.setTopMargin(appearance.topMargin);
    format.setBottomMargin(appearance.bottomMargin);
    if (backgroundColor.isValid()) {
        format.setBackground(backgroundColor);
    } else {
        format.clearBackground();
    }
    return format;
}

void applyExplicitBlockStyle(QTextCursor &cursor, const QTextBlockFormat &blockFormat, const QTextCharFormat &charFormat)
{
    cursor.select(QTextCursor::BlockUnderCursor);
    cursor.setCharFormat(charFormat);
    cursor.setBlockCharFormat(charFormat);
    cursor.setBlockFormat(blockFormat);
}

}

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(AnnotatedTextEdit *editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    AnnotatedTextEdit *m_editor;
};

class AnnotatedTextEdit::CommentBlockData final : public QTextBlockUserData
{
public:
    int groupId = -1;
    bool collapsed = false;
    bool showPlaceholder = false;
    int visualLineIndex = 0;
};

class AnnotatedTextEdit::ModificationGuard
{
public:
    explicit ModificationGuard(QTextDocument *document)
        : m_document(document)
        , m_modified(document ? document->isModified() : false)
    {
    }

    ~ModificationGuard()
    {
        if (m_document) {
            m_document->setModified(m_modified);
        }
    }

private:
    QTextDocument *m_document;
    bool m_modified;
};

AnnotatedTextEdit::AnnotatedTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
    , m_commentRelayoutPending(false)
    , m_internalMutation(false)
    , m_nextCommentGroupId(1)
    , m_commentBlockCount(0)
{
    m_sourceAppearance.family = font().family();
    m_sourceAppearance.pointSize = font().pointSize() > 0 ? font().pointSize() : 16;
    m_sourceAppearance.fontWeight = font().weight();
    m_sourceAppearance.italic = font().italic();
    m_sourceAppearance.underline = font().underline();
    m_sourceAppearance.textColor = palette().color(QPalette::Text);
    m_sourceAppearance.backgroundColor = palette().color(QPalette::Base);
    m_sourceAppearance.leftMargin = 0;
    m_sourceAppearance.topMargin = 0;
    m_sourceAppearance.bottomMargin = 0;
    m_sourceAppearance.highlightAnnotatedLines = true;
    m_sourceAppearance.highlightFullWidth = false;
    m_sourceAppearance.annotatedLineColor = QColor(QStringLiteral("#fff3bf"));
    m_sourceAppearance.annotatedLineBorderColor = QColor(QStringLiteral("#d8b24c"));
    m_sourceAppearance.annotatedLinePadding = 8;

    m_commentAppearance.family = QStringLiteral("Segoe UI");
    m_commentAppearance.pointSize = qMax(11, m_sourceAppearance.pointSize - 1);
    m_commentAppearance.fontWeight = QFont::Normal;
    m_commentAppearance.italic = false;
    m_commentAppearance.underline = false;
    m_commentAppearance.textColor = QColor(QStringLiteral("#334155"));
    m_commentAppearance.backgroundColor = QColor(QStringLiteral("#eef6ff"));
    m_commentAppearance.leftMargin = 26;
    m_commentAppearance.topMargin = 2;
    m_commentAppearance.bottomMargin = 2;
    m_commentAppearance.highlightAnnotatedLines = false;
    m_commentAppearance.highlightFullWidth = false;
    m_commentAppearance.annotatedLineColor = QColor();
    m_commentAppearance.annotatedLineBorderColor = QColor();
    m_commentAppearance.annotatedLinePadding = 0;

    connect(document(), &QTextDocument::blockCountChanged, this, [this](int newBlockCount) {
        if (newBlockCount <= 1) {
            m_commentBlockCount = 0;
        } else {
            m_commentBlockCount = qMin(m_commentBlockCount, qMax(0, newBlockCount - 1));
        }
        updateLineNumberAreaWidth(newBlockCount);
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        viewport()->update();
        if (m_lineNumberArea) {
            m_lineNumberArea->update();
        }
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, int dy) {
        updateLineNumberArea(rect, dy);
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (m_lineNumberArea) {
            m_lineNumberArea->update();
        }
    });

    updateLineNumberAreaWidth(document()->blockCount());
    setSourceAppearance(m_sourceAppearance);
}

AnnotatedTextEdit::~AnnotatedTextEdit() = default;

int AnnotatedTextEdit::currentLineNumber() const
{
    return sourceLineNumberForBlock(textCursor().block());
}

int AnnotatedTextEdit::lineCount() const
{
    return qMax(1, document()->blockCount() - m_commentBlockCount);
}

QString AnnotatedTextEdit::lineText(int lineNumber) const
{
    const QTextBlock block = sourceBlockForLine(lineNumber);
    return block.isValid() ? block.text() : QString();
}

void AnnotatedTextEdit::goToLine(int lineNumber)
{
    const QTextBlock block = sourceBlockForLine(lineNumber);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

QString AnnotatedTextEdit::sourcePlainText() const
{
    return sourceLines().join(QChar('\n'));
}

QStringList AnnotatedTextEdit::sourceLines() const
{
    QStringList lines;
    lines.reserve(lineCount());
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (!isCommentBlock(block)) {
            lines.append(block.text());
        }
    }
    return lines;
}

QList<int> AnnotatedTextEdit::selectedSourceLines() const
{
    const QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) {
        return {};
    }

    const int selectionStart = cursor.selectionStart();
    const int selectionEnd = qMax(selectionStart, cursor.selectionEnd() - 1);
    const QTextBlock startBlock = document()->findBlock(selectionStart);
    const QTextBlock endBlock = document()->findBlock(selectionEnd);
    if (!startBlock.isValid() || !endBlock.isValid()) {
        return {};
    }

    QList<int> lines;
    QSet<int> seenLines;
    for (QTextBlock block = startBlock; block.isValid(); block = block.next()) {
        const QTextBlock sourceBlock = sourceBlockForBlock(block);
        if (sourceBlock.isValid()) {
            const int lineNumber = sourceLineNumberForBlock(sourceBlock);
            if (!seenLines.contains(lineNumber)) {
                seenLines.insert(lineNumber);
                lines.append(lineNumber);
            }
        }
        if (block == endBlock) {
            break;
        }
    }
    return lines;
}

QString AnnotatedTextEdit::commentPlainTextAtLine(int lineNumber) const
{
    const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
    const QTextBlock commentBlock = firstCommentBlockAfterSource(sourceBlock);
    return commentPlainText(commentBlock);
}

bool AnnotatedTextEdit::currentBlockIsComment() const
{
    return isCommentBlock(textCursor().block());
}

void AnnotatedTextEdit::undo()
{
    const QTextBlock beforeBlock = textCursor().block();
    const bool commentBlockWasFocused = isCommentBlock(beforeBlock);
    const bool emptyCommentWasFocused = commentBlockWasFocused && isCommentGroupEmpty(beforeBlock);
    const int sourceLineNumber = commentBlockWasFocused ? sourceLineNumberForBlock(beforeBlock) : -1;

    QPlainTextEdit::undo();

    if (!emptyCommentWasFocused || sourceLineNumber < 0) {
        return;
    }

    const QTextBlock sourceBlock = sourceBlockForLine(sourceLineNumber);
    if (!sourceBlock.isValid() || firstCommentBlockAfterSource(sourceBlock).isValid()) {
        return;
    }

    QTextCursor cursor(sourceBlock);
    cursor.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(cursor);
}

void AnnotatedTextEdit::redo()
{
    const QTextBlock beforeBlock = textCursor().block();
    const bool sourceBlockWasFocused = beforeBlock.isValid() && !isCommentBlock(beforeBlock);
    const bool sourceHadNoComment = sourceBlockWasFocused && !firstCommentBlockAfterSource(beforeBlock).isValid();
    const int sourceLineNumber = sourceBlockWasFocused ? sourceLineNumberForBlock(beforeBlock) : -1;

    QPlainTextEdit::redo();

    if (!sourceHadNoComment || sourceLineNumber < 0) {
        return;
    }

    const QTextBlock sourceBlock = sourceBlockForLine(sourceLineNumber);
    const QTextBlock commentBlock = firstCommentBlockAfterSource(sourceBlock);
    if (!commentBlock.isValid()) {
        return;
    }

    QTextCursor cursor(commentBlock);
    setTextCursor(cursor);
}

void AnnotatedTextEdit::replaceSourceText(const QString &text)
{
    m_commentRelayoutPending = false;
    m_commentBlockCount = 0;
    m_nextCommentGroupId = 1;
    document()->setPlainText(text);
    QTextCursor cursor(document());
    cursor.movePosition(QTextCursor::Start);
    setTextCursor(cursor);
    refreshSourceBlocks();
    updateLineNumberAreaWidth(document()->blockCount());
    updateLineNumberAreaGeometry();
    if (m_lineNumberArea) {
        m_lineNumberArea->update();
    }
    viewport()->update();
}

bool AnnotatedTextEdit::addCommentToCurrentLine()
{
    QTextBlock sourceBlock = sourceBlockForCursor();
    if (!sourceBlock.isValid()) {
        return false;
    }

    return addCommentToLine(sourceLineNumberForBlock(sourceBlock), visualLineIndexForCursor(sourceBlock));
}

int AnnotatedTextEdit::addCommentRange(int startLine, int endLine, const QString &commentText, bool overwriteExisting)
{
    if (commentText.isEmpty()) {
        return 0;
    }

    const int normalizedStart = qMax(0, qMin(startLine, endLine));
    const int normalizedEnd = qMax(normalizedStart, qMax(startLine, endLine));
    const QStringList commentLines = commentText.split(QChar('\n'), Qt::KeepEmptyParts);
    QTextCursor editCursor(document());
    editCursor.beginEditBlock();

    int changedCount = 0;
    for (int lineNumber = normalizedStart; lineNumber <= normalizedEnd; ++lineNumber) {
        const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
        if (!sourceBlock.isValid()) {
            continue;
        }

        CommentBlockData *existingData = commentDataForLine(lineNumber);
        if (existingData && !overwriteExisting) {
            continue;
        }

        const bool wasCollapsed = existingData ? existingData->collapsed : false;
        const int visualLineIndex = existingData ? existingData->visualLineIndex : 0;
        if (existingData) {
            removeCommentGroup(sourceBlock);
            applySourceBlockStyle(sourceBlock);
        }

        QTextBlock insertAfter = sourceBlock;
        const int groupId = m_nextCommentGroupId++;
        for (const QString &lineText : commentLines) {
            QTextCursor cursor(insertAfter);
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
            const QTextBlock commentBlock = cursor.block();
            markBlockAsComment(commentBlock, groupId, wasCollapsed, visualLineIndex);
            cursor.setBlockFormat(buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setBlockCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            if (!lineText.isEmpty()) {
                cursor.insertText(lineText);
            }
            insertAfter = commentBlock;
        }

        applySourceBlockStyle(sourceBlock);
        applyCommentVisibility(groupId, wasCollapsed);
        ++changedCount;
    }

    if (changedCount > 0) {
        markCommentStateChanged();
    }
    editCursor.endEditBlock();
    if (changedCount > 0) {
        emit commentsChanged();
    }
    return changedCount;
}

int AnnotatedTextEdit::addCommentsToLines(const QList<QPair<int, QString>> &lineComments, bool overwriteExisting)
{
    if (lineComments.isEmpty()) {
        return 0;
    }

    QTextCursor editCursor(document());
    editCursor.beginEditBlock();
    int changedCount = 0;
    QSet<int> handledLines;

    for (const QPair<int, QString> &item : lineComments) {
        const int lineNumber = item.first;
        const QString commentText = item.second;
        if (handledLines.contains(lineNumber) || commentText.isEmpty()) {
            continue;
        }
        handledLines.insert(lineNumber);

        const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
        if (!sourceBlock.isValid()) {
            continue;
        }

        CommentBlockData *existingData = commentDataForLine(lineNumber);
        if (existingData && !overwriteExisting) {
            continue;
        }

        const bool wasCollapsed = existingData ? existingData->collapsed : false;
        const int visualLineIndex = existingData ? existingData->visualLineIndex : 0;
        if (existingData) {
            removeCommentGroup(sourceBlock);
            applySourceBlockStyle(sourceBlock);
        }

        const QStringList commentLines = commentText.split(QChar('\n'), Qt::KeepEmptyParts);
        QTextBlock insertAfter = sourceBlock;
        const int groupId = m_nextCommentGroupId++;
        for (const QString &lineText : commentLines) {
            QTextCursor cursor(insertAfter);
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
            const QTextBlock commentBlock = cursor.block();
            markBlockAsComment(commentBlock, groupId, wasCollapsed, visualLineIndex);
            cursor.setBlockFormat(buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setBlockCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            if (!lineText.isEmpty()) {
                cursor.insertText(lineText);
            }
            insertAfter = commentBlock;
        }

        applySourceBlockStyle(sourceBlock);
        applyCommentVisibility(groupId, wasCollapsed);
        ++changedCount;
    }

    if (changedCount > 0) {
        markCommentStateChanged();
    }
    editCursor.endEditBlock();
    if (changedCount > 0) {
        emit commentsChanged();
    }
    return changedCount;
}

bool AnnotatedTextEdit::addCommentToLine(int lineNumber, int visualLineIndex)
{
    const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
    if (!sourceBlock.isValid() || firstCommentBlockAfterSource(sourceBlock).isValid()) {
        return false;
    }

    QTextCursor editCursor(document());
    editCursor.beginEditBlock();
    m_internalMutation = true;
    QTextCursor cursor(sourceBlock);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertBlock();
    const QTextBlock commentBlock = cursor.block();
    const int groupId = m_nextCommentGroupId++;
    markBlockAsComment(commentBlock, groupId, false, visualLineIndex);
    if (CommentBlockData *data = commentDataForBlock(commentBlock)) {
        data->showPlaceholder = true;
    }
    applySourceBlockStyle(sourceBlock);
    cursor.setBlockFormat(buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
    cursor.setBlockCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
    cursor.setCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
    m_internalMutation = false;

    setTextCursor(cursor);
    applyCommentVisibility(groupId, false);
    markCommentStateChanged();
    editCursor.endEditBlock();
    emit commentsChanged();
    return true;
}

bool AnnotatedTextEdit::removeCommentFromLine(int lineNumber)
{
    const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
    if (!sourceBlock.isValid() || !firstCommentBlockAfterSource(sourceBlock).isValid()) {
        return false;
    }

    QTextCursor editCursor(document());
    editCursor.beginEditBlock();
    removeCommentGroup(sourceBlock);
    markCommentStateChanged();
    editCursor.endEditBlock();
    emit commentsChanged();
    return true;
}

bool AnnotatedTextEdit::toggleCommentAtLine(int lineNumber)
{
    CommentBlockData *data = commentDataForLine(lineNumber);
    if (!data) {
        return false;
    }

    data->collapsed = !data->collapsed;
    applyCommentVisibility(data->groupId, data->collapsed);

    if (isCommentBlock(textCursor().block()) && data->collapsed) {
        goToLine(lineNumber);
    }

    markCommentStateChanged();
    emit commentsChanged();
    return true;
}

bool AnnotatedTextEdit::hasCommentAtLine(int lineNumber) const
{
    return commentDataForLine(lineNumber) != nullptr;
}

QString AnnotatedTextEdit::commentHtmlAtLine(int lineNumber) const
{
    return plainTextToHtml(commentPlainTextAtLine(lineNumber));
}

void AnnotatedTextEdit::setCommentHtmlAtLine(int lineNumber, const QString &html)
{
    const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
    CommentBlockData *data = commentDataForLine(lineNumber);
    if (!sourceBlock.isValid() || !data) {
        return;
    }

    const bool wasCollapsed = data->collapsed;
    QTextCursor editCursor(document());
    editCursor.beginEditBlock();
    removeCommentGroup(sourceBlock);
    applySourceBlockStyle(sourceBlock);

    const QStringList lines = htmlToPlainText(html).split(QChar('\n'));
    m_internalMutation = true;
    QTextBlock insertAfter = sourceBlock;
    const int groupId = m_nextCommentGroupId++;
    for (int index = 0; index < qMax(1, lines.size()); ++index) {
        QTextCursor cursor(insertAfter);
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        const QTextBlock commentBlock = cursor.block();
        markBlockAsComment(commentBlock, groupId, wasCollapsed, data ? data->visualLineIndex : 0);
        cursor.setBlockFormat(buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
        cursor.setBlockCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
        cursor.setCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
        cursor.insertText(lines.value(index));
        insertAfter = commentBlock;
    }
    m_internalMutation = false;

    applySourceBlockStyle(sourceBlock);
    applyCommentVisibility(groupId, wasCollapsed);
    markCommentStateChanged();
    editCursor.endEditBlock();
    emit commentsChanged();
}

QList<AnnotatedTextEdit::CommentEntry> AnnotatedTextEdit::commentEntries() const
{
    QList<CommentEntry> entries;
    int sourceIndex = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (isCommentBlock(block)) {
            continue;
        }

        const QTextBlock commentBlock = firstCommentBlockAfterSource(block);
        if (commentBlock.isValid()) {
            CommentBlockData *data = commentDataForBlock(commentBlock);
            entries.append(CommentEntry {
                sourceIndex,
                data ? data->visualLineIndex : 0,
                sourceSubLineText(block, data ? data->visualLineIndex : 0),
                plainTextToHtml(commentPlainText(commentBlock)),
                data ? data->collapsed : false,
            });
        }
        ++sourceIndex;
    }
    return entries;
}

void AnnotatedTextEdit::loadCommentEntries(const QList<CommentEntry> &entries)
{
    clearAllComments();

    m_internalMutation = true;
    for (const CommentEntry &entry : entries) {
        const QTextBlock sourceBlock = sourceBlockForLine(entry.lineNumber);
        if (!sourceBlock.isValid()) {
            continue;
        }

        const QStringList lines = htmlToPlainText(entry.html).split(QChar('\n'));
        QTextBlock insertAfter = sourceBlock;
        const int groupId = m_nextCommentGroupId++;
        for (int index = 0; index < qMax(1, lines.size()); ++index) {
            QTextCursor cursor(insertAfter);
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertBlock();
            const QTextBlock commentBlock = cursor.block();
            markBlockAsComment(commentBlock, groupId, entry.collapsed, entry.visualLineIndex);
            cursor.setBlockFormat(buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setBlockCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.setCharFormat(buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
            cursor.insertText(lines.value(index));
            insertAfter = commentBlock;
        }
        applySourceBlockStyle(sourceBlock);
        applyCommentVisibility(groupId, entry.collapsed);
    }
    m_internalMutation = false;

    refreshSourceBlocks();
    scheduleCommentRelayout();
    emit commentsChanged();
}

void AnnotatedTextEdit::clearAllComments()
{
    for (int index = lineCount() - 1; index >= 0; --index) {
        removeCommentFromLine(index);
    }
}

AnnotatedTextEdit::TextAppearance AnnotatedTextEdit::sourceAppearance() const
{
    return m_sourceAppearance;
}

AnnotatedTextEdit::TextAppearance AnnotatedTextEdit::commentAppearance() const
{
    return m_commentAppearance;
}

void AnnotatedTextEdit::setSourceAppearance(const TextAppearance &appearance)
{
    m_sourceAppearance = appearance;
    ModificationGuard guard(document());

    QFont editorFont = font();
    if (!appearance.family.isEmpty()) {
        editorFont.setFamily(appearance.family);
    }
    if (appearance.pointSize > 0) {
        editorFont.setPointSize(appearance.pointSize);
    }
    editorFont.setWeight(static_cast<QFont::Weight>(appearance.fontWeight));
    editorFont.setItalic(appearance.italic);
    editorFont.setUnderline(appearance.underline);
    setFont(editorFont);

    QPalette editorPalette = palette();
    editorPalette.setColor(QPalette::Text, appearance.textColor);
    editorPalette.setColor(QPalette::Base, appearance.backgroundColor);
    setPalette(editorPalette);
    refreshSourceBlocks();
    updateLineNumberAreaWidth(document()->blockCount());
    if (m_lineNumberArea) {
        m_lineNumberArea->update();
    }
    viewport()->update();
}

void AnnotatedTextEdit::setCommentAppearance(const TextAppearance &appearance)
{
    m_commentAppearance = appearance;
    scheduleCommentRelayout();
    viewport()->update();
}

void AnnotatedTextEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        redo();
        return;
    }

    const QTextBlock beforeBlock = textCursor().block();
    const int beforeGroupId = commentGroupId(beforeBlock);
    const bool deletionKey = event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete;
    const bool undoOrRedo = event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo);
    if (CommentBlockData *data = commentDataForBlock(beforeBlock)) {
        const bool typedText = !event->text().isEmpty()
                               && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (data->showPlaceholder && typedText) {
            data->showPlaceholder = false;
        }
    }
    const bool splitComment = beforeGroupId >= 0
        && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & Qt::ShiftModifier);

    QPlainTextEdit::keyPressEvent(event);

    if (splitComment) {
        const QTextBlock currentBlock = textCursor().block();
        if (currentBlock.isValid() && !isCommentBlock(currentBlock)) {
            const CommentBlockData *previousData = commentDataForBlock(beforeBlock);
            markBlockAsComment(currentBlock, beforeGroupId, false, previousData ? previousData->visualLineIndex : 0);
            applySourceBlockStyle(sourceBlockForBlock(currentBlock));
            markCommentStateChanged();
            emit commentsChanged();
        }
    }

    QTextBlock probeBlock = textCursor().block();
    if (!isCommentBlock(probeBlock)) {
        probeBlock = beforeBlock;
    }
    if (CommentBlockData *data = commentDataForBlock(probeBlock)) {
        if (probeBlock.text().isEmpty()) {
            data->showPlaceholder = true;
        } else {
            data->showPlaceholder = false;
        }
    }
    if (isCommentBlock(probeBlock) && deletionKey && !undoOrRedo && removeCommentGroupIfEmpty(probeBlock)) {
        return;
    }
}

void AnnotatedTextEdit::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);

    QPainter painter(viewport());
    painter.setClipRect(event->rect());

    QColor fillColor = m_sourceAppearance.annotatedLineColor;
    if (fillColor.alpha() == 255) {
        fillColor.setAlpha(140);
    }
    const QColor placeholderColor = QColor(QStringLiteral("#8a97a6"));
    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();
    const QRectF viewportRect = event->rect();
    while (block.isValid()) {
        if (!block.isVisible()) {
            block = block.next();
            continue;
        }

        const QRectF blockRect = blockBoundingGeometry(block).translated(offset);
        if (blockRect.bottom() < viewportRect.top()) {
            block = block.next();
            continue;
        }
        if (blockRect.top() > viewportRect.bottom()) {
            break;
        }

        if (m_sourceAppearance.highlightAnnotatedLines && !isCommentBlock(block) && firstCommentBlockAfterSource(block).isValid()) {
            QRectF highlightRect;
            const CommentBlockData *commentData = commentDataForBlock(firstCommentBlockAfterSource(block));
            const int targetVisualLine = commentData ? qMax(0, commentData->visualLineIndex) : 0;
            const qreal padding = qMax(0, m_sourceAppearance.annotatedLinePadding);
            if (block.layout() && block.layout()->lineCount() > 0) {
                if (lineWrapMode() == QPlainTextEdit::WidgetWidth) {
                    if (m_sourceAppearance.highlightFullWidth) {
                        const qreal left = qMax<qreal>(0.0, blockRect.left() - padding);
                        highlightRect = QRectF(left, blockRect.top(), viewport()->width() - left - 1.0, qMax<qreal>(1.0, blockRect.height()));
                    } else {
                        QRectF combinedRect;
                        for (int lineIndex = 0; lineIndex < block.layout()->lineCount(); ++lineIndex) {
                            const QTextLine wrappedLine = block.layout()->lineAt(lineIndex);
                            const QRectF naturalRect = wrappedLine.naturalTextRect();
                            const QRectF lineRect(
                                blockRect.left() + naturalRect.left() - padding,
                                blockRect.top() + wrappedLine.position().y(),
                                qMax<qreal>(1.0, naturalRect.width() + padding * 2.0),
                                qMax<qreal>(1.0, wrappedLine.height()));
                            combinedRect = combinedRect.isNull() ? lineRect : combinedRect.united(lineRect);
                        }
                        highlightRect = combinedRect;
                    }
                } else {
                    const QTextLine line = block.layout()->lineAt(qMin(targetVisualLine, block.layout()->lineCount() - 1));
                    const QRectF naturalRect = line.naturalTextRect();
                    const qreal top = blockRect.top() + line.position().y();
                    const qreal height = qMax<qreal>(1.0, line.height());
                    if (m_sourceAppearance.highlightFullWidth) {
                        const qreal left = qMax<qreal>(0.0, blockRect.left() - padding);
                        highlightRect = QRectF(left, top, viewport()->width() - left - 1.0, height);
                    } else {
                        const qreal left = blockRect.left() + naturalRect.left() - padding;
                        const qreal width = qMax<qreal>(1.0, naturalRect.width() + padding * 2.0);
                        highlightRect = QRectF(left, top, width, height);
                    }
                }
            } else {
                highlightRect = QRectF(blockRect.left(), blockRect.top(), qMax<qreal>(1.0, cursorWidth()), blockRect.height());
            }

            if (!highlightRect.isEmpty() && highlightRect.width() > 1.0 && highlightRect.height() > 1.0) {
                painter.fillRect(highlightRect.toAlignedRect(), fillColor);
            }
        }

        if (shouldShowCommentPlaceholder(block) && block.layout() && block.layout()->lineCount() > 0) {
            const QTextLine line = block.layout()->lineAt(0);
            const qreal left = blockRect.left() + line.position().x() + 6.0;
            const qreal baseline = blockRect.top() + line.position().y() + line.ascent();
            painter.setPen(placeholderColor);
            painter.drawText(QPointF(left, baseline), QStringLiteral("在这里输入注释..."));
        }

        block = block.next();
    }
}

void AnnotatedTextEdit::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    updateLineNumberAreaGeometry();
    viewport()->update();
}

void AnnotatedTextEdit::showEvent(QShowEvent *event)
{
    QPlainTextEdit::showEvent(event);
    updateLineNumberAreaGeometry();
    scheduleCommentRelayout();
}

int AnnotatedTextEdit::lineNumberAreaWidth() const
{
    int digits = 1;
    int maximum = qMax(1, lineCount());
    while (maximum >= 10) {
        maximum /= 10;
        ++digits;
    }
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void AnnotatedTextEdit::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    updateLineNumberAreaGeometry();
}

void AnnotatedTextEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (!m_lineNumberArea) {
        return;
    }

    if (dy != 0) {
        m_lineNumberArea->scroll(0, dy);
    } else {
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    }

    if (rect.contains(viewport()->rect())) {
        updateLineNumberAreaWidth(document()->blockCount());
    }
}

void AnnotatedTextEdit::updateLineNumberAreaGeometry()
{
    if (!m_lineNumberArea) {
        return;
    }

    const QRect contents = contentsRect();
    m_lineNumberArea->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
}

void AnnotatedTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    if (!m_lineNumberArea) {
        return;
    }

    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(QStringLiteral("#f4f7fb")));
    painter.setPen(QColor(QStringLiteral("#d7e0ea")));
    painter.drawLine(m_lineNumberArea->rect().topRight(), m_lineNumberArea->rect().bottomRight());

    const QTextBlock currentSourceBlock = sourceBlockForBlock(textCursor().block());
    QTextBlock block = firstVisibleBlock();
    const QPointF offset = contentOffset();
    while (block.isValid()) {
        if (!block.isVisible()) {
            block = block.next();
            continue;
        }

        const QRectF blockRect = blockBoundingGeometry(block).translated(offset);
        if (blockRect.bottom() < event->rect().top()) {
            block = block.next();
            continue;
        }
        if (blockRect.top() > event->rect().bottom()) {
            break;
        }

        if (!isCommentBlock(block)) {
            const QRect numberRect(0, qRound(blockRect.top()), m_lineNumberArea->width() - 8, qRound(blockRect.height()));
            const bool isCurrent = block == currentSourceBlock;
            if (isCurrent) {
                painter.fillRect(numberRect.adjusted(2, 1, -2, -1), QColor(QStringLiteral("#e8f1fb")));
            }
            painter.setPen(isCurrent ? QColor(QStringLiteral("#0f6cbd")) : QColor(QStringLiteral("#7b8794")));
            painter.drawText(numberRect, Qt::AlignRight | Qt::AlignVCenter, QString::number(sourceLineNumberForBlock(block) + 1));
        }

        block = block.next();
    }
}

bool AnnotatedTextEdit::isCommentBlock(const QTextBlock &block) const
{
    return commentDataForBlock(block) != nullptr;
}

int AnnotatedTextEdit::commentGroupId(const QTextBlock &block) const
{
    CommentBlockData *data = commentDataForBlock(block);
    return data ? data->groupId : -1;
}

QTextBlock AnnotatedTextEdit::sourceBlockForLine(int lineNumber) const
{
    if (lineNumber < 0) {
        return QTextBlock();
    }

    int sourceIndex = 0;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (isCommentBlock(block)) {
            continue;
        }
        if (sourceIndex == lineNumber) {
            return block;
        }
        ++sourceIndex;
    }
    return QTextBlock();
}

QTextBlock AnnotatedTextEdit::sourceBlockForCursor() const
{
    return sourceBlockForBlock(textCursor().block());
}

QTextBlock AnnotatedTextEdit::sourceBlockForBlock(const QTextBlock &block) const
{
    if (!block.isValid()) {
        return QTextBlock();
    }

    if (!isCommentBlock(block)) {
        return block;
    }

    QTextBlock current = block.previous();
    while (current.isValid()) {
        if (!isCommentBlock(current)) {
            return current;
        }
        current = current.previous();
    }
    return QTextBlock();
}

QTextBlock AnnotatedTextEdit::firstCommentBlockAfterSource(const QTextBlock &sourceBlock) const
{
    if (!sourceBlock.isValid()) {
        return QTextBlock();
    }

    const QTextBlock nextBlock = sourceBlock.next();
    return isCommentBlock(nextBlock) ? nextBlock : QTextBlock();
}

QTextBlock AnnotatedTextEdit::lastCommentBlockInGroup(const QTextBlock &commentBlock) const
{
    if (!isCommentBlock(commentBlock)) {
        return QTextBlock();
    }

    const int groupId = commentGroupId(commentBlock);
    QTextBlock current = commentBlock;
    while (current.next().isValid() && commentGroupId(current.next()) == groupId) {
        current = current.next();
    }
    return current;
}

int AnnotatedTextEdit::sourceLineNumberForBlock(const QTextBlock &block) const
{
    const QTextBlock sourceBlock = sourceBlockForBlock(block);
    if (!sourceBlock.isValid()) {
        return 0;
    }

    int sourceIndex = 0;
    for (QTextBlock current = document()->begin(); current.isValid(); current = current.next()) {
        if (isCommentBlock(current)) {
            continue;
        }
        if (current == sourceBlock) {
            return sourceIndex;
        }
        ++sourceIndex;
    }
    return 0;
}

int AnnotatedTextEdit::visualLineIndexForCursor(const QTextBlock &sourceBlock) const
{
    if (!sourceBlock.isValid() || !sourceBlock.layout() || sourceBlock.layout()->lineCount() <= 1) {
        return 0;
    }

    const QTextCursor currentCursor = textCursor();
    const int cursorInBlock = qBound(0, currentCursor.position() - sourceBlock.position(), qMax(0, sourceBlock.length() - 1));
    for (int index = 0; index < sourceBlock.layout()->lineCount(); ++index) {
        const QTextLine candidate = sourceBlock.layout()->lineAt(index);
        const int lineStart = candidate.textStart();
        const int lineEnd = lineStart + qMax(0, candidate.textLength());
        if (cursorInBlock >= lineStart && cursorInBlock <= lineEnd) {
            return index;
        }
    }
    return 0;
}

QString AnnotatedTextEdit::sourceSubLineText(const QTextBlock &sourceBlock, int visualLineIndex) const
{
    if (!sourceBlock.isValid()) {
        return QString();
    }
    if (lineWrapMode() == QPlainTextEdit::WidgetWidth) {
        return sourceBlock.text();
    }
    if (!sourceBlock.layout() || sourceBlock.layout()->lineCount() <= 1) {
        return sourceBlock.text();
    }

    const QTextLine line = sourceBlock.layout()->lineAt(qMin(qMax(0, visualLineIndex), sourceBlock.layout()->lineCount() - 1));
    const int start = line.textStart();
    const int end = start + qMax(0, line.textLength());
    return sourceBlock.text().mid(start, qMax(0, end - start));
}

AnnotatedTextEdit::CommentBlockData *AnnotatedTextEdit::commentDataForLine(int lineNumber) const
{
    const QTextBlock sourceBlock = sourceBlockForLine(lineNumber);
    return commentDataForBlock(firstCommentBlockAfterSource(sourceBlock));
}

AnnotatedTextEdit::CommentBlockData *AnnotatedTextEdit::commentDataForBlock(const QTextBlock &block) const
{
    if (!block.isValid()) {
        return nullptr;
    }
    return static_cast<CommentBlockData *>(block.userData());
}

AnnotatedTextEdit::CommentBlockData *AnnotatedTextEdit::ensureCommentDataForBlock(const QTextBlock &block)
{
    if (!block.isValid()) {
        return nullptr;
    }
    if (CommentBlockData *existing = commentDataForBlock(block)) {
        return existing;
    }
    auto *data = new CommentBlockData;
    const_cast<QTextBlock &>(block).setUserData(data);
    return data;
}

void AnnotatedTextEdit::markBlockAsComment(const QTextBlock &block, int groupId, bool collapsed, int visualLineIndex)
{
    const bool wasComment = isCommentBlock(block);
    CommentBlockData *data = ensureCommentDataForBlock(block);
    data->groupId = groupId;
    data->collapsed = collapsed;
    data->visualLineIndex = qMax(0, visualLineIndex);
    if (!wasComment) {
        data->showPlaceholder = false;
    }
    if (!wasComment) {
        ++m_commentBlockCount;
    }
    applyCommentBlockStyle(block);
}

void AnnotatedTextEdit::applySourceBlockStyle(const QTextBlock &block)
{
    if (!block.isValid() || isCommentBlock(block)) {
        return;
    }

    ModificationGuard guard(document());
    QTextCursor cursor(block);
    applyExplicitBlockStyle(cursor, buildSourceBlockFormat(m_sourceAppearance, m_sourceAppearance.backgroundColor), buildTextFormat(m_sourceAppearance, QColor(), false));
}

void AnnotatedTextEdit::applyCommentBlockStyle(const QTextBlock &block)
{
    if (!block.isValid() || !isCommentBlock(block)) {
        return;
    }

    ModificationGuard guard(document());
    QTextCursor cursor(block);
    applyExplicitBlockStyle(
        cursor,
        buildCommentBlockFormat(m_commentAppearance, m_commentAppearance.backgroundColor),
        buildTextFormat(m_commentAppearance, m_commentAppearance.backgroundColor));
}

void AnnotatedTextEdit::applyCommentVisibility(int groupId, bool collapsed)
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (commentGroupId(block) != groupId) {
            continue;
        }

        QTextBlock editableBlock = block;
        editableBlock.setVisible(!collapsed);
        editableBlock.setLineCount(collapsed ? 0 : 1);
        if (CommentBlockData *data = commentDataForBlock(block)) {
            data->collapsed = collapsed;
        }
    }

    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
}

void AnnotatedTextEdit::refreshCommentBlocks()
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (!isCommentBlock(block)) {
            continue;
        }
        applyCommentBlockStyle(block);
    }

    QSet<int> handledGroups;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const int groupId = commentGroupId(block);
        if (groupId < 0 || handledGroups.contains(groupId)) {
            continue;
        }
        handledGroups.insert(groupId);
        if (CommentBlockData *data = commentDataForBlock(block)) {
            applyCommentVisibility(groupId, data->collapsed);
        }
    }
}

void AnnotatedTextEdit::refreshSourceBlocks()
{
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        if (isCommentBlock(block)) {
            continue;
        }
        applySourceBlockStyle(block);
    }
}

void AnnotatedTextEdit::removeCommentGroup(const QTextBlock &sourceBlock)
{
    QTextBlock commentBlock = firstCommentBlockAfterSource(sourceBlock);
    if (!commentBlock.isValid()) {
        return;
    }

    m_internalMutation = true;
    while (commentBlock.isValid() && isCommentBlock(commentBlock)) {
        m_commentBlockCount = qMax(0, m_commentBlockCount - 1);
        QTextCursor cursor(commentBlock);
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deletePreviousChar();
        commentBlock = firstCommentBlockAfterSource(sourceBlock);
    }
    m_internalMutation = false;
    applySourceBlockStyle(sourceBlock);
}

bool AnnotatedTextEdit::removeCommentGroupIfEmpty(const QTextBlock &commentBlock)
{
    if (!isCommentBlock(commentBlock) || !isCommentGroupEmpty(commentBlock)) {
        return false;
    }

    const QTextBlock sourceBlock = sourceBlockForBlock(commentBlock);
    if (!sourceBlock.isValid()) {
        return false;
    }

    removeCommentGroup(sourceBlock);
    QTextCursor cursor(sourceBlock);
    setTextCursor(cursor);
    markCommentStateChanged();
    emit commentsChanged();
    return true;
}

QString AnnotatedTextEdit::commentPlainText(const QTextBlock &commentBlock) const
{
    if (!commentBlock.isValid() || !isCommentBlock(commentBlock)) {
        return QString();
    }

    const int groupId = commentGroupId(commentBlock);
    QStringList lines;
    for (QTextBlock block = commentBlock; block.isValid() && commentGroupId(block) == groupId; block = block.next()) {
        lines.append(block.text());
    }
    return lines.join(QChar('\n'));
}

bool AnnotatedTextEdit::isCommentGroupEmpty(const QTextBlock &commentBlock) const
{
    if (!commentBlock.isValid() || !isCommentBlock(commentBlock)) {
        return false;
    }

    const int groupId = commentGroupId(commentBlock);
    for (QTextBlock block = commentBlock; block.isValid() && commentGroupId(block) == groupId; block = block.next()) {
        if (!block.text().trimmed().isEmpty()) {
            return false;
        }
    }
    return true;
}

bool AnnotatedTextEdit::shouldShowCommentPlaceholder(const QTextBlock &commentBlock) const
{
    CommentBlockData *data = commentDataForBlock(commentBlock);
    return data && data->showPlaceholder && commentBlock.text().isEmpty();
}

void AnnotatedTextEdit::scheduleCommentRelayout()
{
    if (m_commentRelayoutPending) {
        return;
    }

    m_commentRelayoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_commentRelayoutPending = false;
        refreshCommentBlocks();
        if (m_lineNumberArea) {
            m_lineNumberArea->update();
        }
    });
}

void AnnotatedTextEdit::markCommentStateChanged()
{
    document()->setModified(true);
    scheduleCommentRelayout();
}
