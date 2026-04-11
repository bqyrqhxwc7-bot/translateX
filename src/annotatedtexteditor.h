#pragma once

#include <QColor>
#include <QList>
#include <QPair>
#include <QPlainTextEdit>
#include <QStringList>

class LineNumberArea;
class QTextBlock;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWidget;

class AnnotatedTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    struct TextAppearance {
        QString family;
        int pointSize;
        int fontWeight;
        bool italic;
        bool underline;
        QColor textColor;
        QColor backgroundColor;
        int leftMargin;
        int topMargin;
        int bottomMargin;
        bool highlightAnnotatedLines;
        bool highlightFullWidth;
        QColor annotatedLineColor;
        QColor annotatedLineBorderColor;
        int annotatedLinePadding;
    };

    struct CommentEntry {
        int lineNumber;
        int visualLineIndex;
        QString sourceText;
        QString html;
        bool collapsed;
    };

    explicit AnnotatedTextEdit(QWidget *parent = nullptr);
    ~AnnotatedTextEdit() override;

    int currentLineNumber() const;
    int lineCount() const;
    QString lineText(int lineNumber) const;
    void goToLine(int lineNumber);
    QString sourcePlainText() const;
    QStringList sourceLines() const;
    QList<int> selectedSourceLines() const;
    QString commentPlainTextAtLine(int lineNumber) const;
    bool currentBlockIsComment() const;
    void undo();
    void redo();
    void replaceSourceText(const QString &text);

    bool addCommentToCurrentLine();
    bool addCommentToLine(int lineNumber, int visualLineIndex = 0);
    int addCommentRange(int startLine, int endLine, const QString &commentText, bool overwriteExisting);
    int addCommentsToLines(const QList<QPair<int, QString>> &lineComments, bool overwriteExisting);
    bool removeCommentFromLine(int lineNumber);
    bool toggleCommentAtLine(int lineNumber);
    bool hasCommentAtLine(int lineNumber) const;
    QString commentHtmlAtLine(int lineNumber) const;
    void setCommentHtmlAtLine(int lineNumber, const QString &html);

    QList<CommentEntry> commentEntries() const;
    void loadCommentEntries(const QList<CommentEntry> &entries);
    void clearAllComments();

    TextAppearance sourceAppearance() const;
    TextAppearance commentAppearance() const;
    void setSourceAppearance(const TextAppearance &appearance);
    void setCommentAppearance(const TextAppearance &appearance);

signals:
    void commentsChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    class CommentBlockData;
    class ModificationGuard;
    friend class LineNumberArea;

    bool isCommentBlock(const QTextBlock &block) const;
    int commentGroupId(const QTextBlock &block) const;
    QTextBlock sourceBlockForLine(int lineNumber) const;
    QTextBlock sourceBlockForCursor() const;
    QTextBlock sourceBlockForBlock(const QTextBlock &block) const;
    QTextBlock firstCommentBlockAfterSource(const QTextBlock &sourceBlock) const;
    QTextBlock lastCommentBlockInGroup(const QTextBlock &commentBlock) const;
    int sourceLineNumberForBlock(const QTextBlock &block) const;
    int visualLineIndexForCursor(const QTextBlock &sourceBlock) const;
    QString sourceSubLineText(const QTextBlock &sourceBlock, int visualLineIndex) const;
    CommentBlockData *commentDataForLine(int lineNumber) const;
    CommentBlockData *commentDataForBlock(const QTextBlock &block) const;
    CommentBlockData *ensureCommentDataForBlock(const QTextBlock &block);
    void markBlockAsComment(const QTextBlock &block, int groupId, bool collapsed, int visualLineIndex = 0);
    void applySourceBlockStyle(const QTextBlock &block);
    void applyCommentBlockStyle(const QTextBlock &block);
    void applyCommentVisibility(int groupId, bool collapsed);
    void refreshSourceBlocks();
    void refreshCommentBlocks();
    void removeCommentGroup(const QTextBlock &sourceBlock);
    bool removeCommentGroupIfEmpty(const QTextBlock &commentBlock);
    QString commentPlainText(const QTextBlock &commentBlock) const;
    bool isCommentGroupEmpty(const QTextBlock &commentBlock) const;
    bool shouldShowCommentPlaceholder(const QTextBlock &commentBlock) const;
    int lineNumberAreaWidth() const;
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void updateLineNumberAreaGeometry();
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    void scheduleCommentRelayout();
    void markCommentStateChanged();

    TextAppearance m_sourceAppearance;
    TextAppearance m_commentAppearance;
    QWidget *m_lineNumberArea;
    bool m_commentRelayoutPending;
    bool m_internalMutation;
    int m_nextCommentGroupId;
    int m_commentBlockCount;
};
