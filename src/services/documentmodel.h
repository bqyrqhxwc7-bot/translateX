#pragma once

#include <QAbstractListModel>
#include <QVector>

class CommentService;

class DocumentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        LineNumberRole = Qt::UserRole + 1,
        TextRole,
        IsCommentRole,
        HasCommentRole,
        CommentTextRole,
        DisplayRole,      // "plain" | "rich" | "image"（显示层，不参与 undo）
        RichTextRole,     // rich 模式显示层（HTML 片段）
        ImageIdsRole,     // image 模式引用的图片 id 列表
    };

    explicit DocumentModel(QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 懒加载访问（大文件核心：只暴露可见窗口 ± 缓冲区）
    Q_INVOKABLE QString lineText(int lineNumber) const;
    Q_INVOKABLE int lineCount() const;

    // 批量设置内容（仅在导入/打开时调用一次，之后按需行加载）
    Q_INVOKABLE void setLines(const QStringList &lines);

    // 批注支持（有 provider 时委托 CommentService 单一数据源，无 provider 用内部存储）
    Q_INVOKABLE void setCommentProvider(CommentService *provider);
    Q_INVOKABLE bool hasCommentAt(int lineNumber) const;
    Q_INVOKABLE QString commentAt(int lineNumber) const;
    Q_INVOKABLE void setComment(int lineNumber, const QString &text);

    // 单行更新（编辑时只通知该行）
    Q_INVOKABLE void updateLineText(int lineNumber, const QString &text);

    // 显示层（.trx 富文本/图片）：text 永远是编辑层；display 层不参与 undo。
    // 编辑 rich/image 行（updateLineText）后调用方应同时 setLineDisplay(i, "plain") 降级。
    Q_INVOKABLE void setLineDisplay(int lineNumber, const QString &mode);
    Q_INVOKABLE void setLineRich(int lineNumber, const QString &html);
    Q_INVOKABLE void setLineImages(int lineNumber, const QStringList &ids);
    // 只读访问（供 TrxParser 写回）
    QString displayAt(int lineNumber) const;
    QString richAt(int lineNumber) const;
    QStringList imageIdsAt(int lineNumber) const;

    // 编辑能力：插入/删除/追加行（返回操作后的行号）
    Q_INVOKABLE int insertLine(int atLineNumber, const QString &text = QString());
    Q_INVOKABLE int removeLine(int lineNumber);
    Q_INVOKABLE int appendLine(const QString &text = QString());

    Q_INVOKABLE void clear();

    // 撤销/重做（命令式编辑历史：updateLineText/insertLine/removeLine）
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;
    Q_INVOKABLE void clearUndoHistory();

signals:
    void lineCountChanged();
    void undoStackChanged();   // 撤销/重做可用性变化（供 UI 刷新按钮）

private:
    struct LineEntry {
        QString text;
        QString comment;
        bool hasComment = false;
        // 显示层（.trx）：text 为编辑层，display 层仅供渲染/往返
        QString display = QStringLiteral("plain");
        QString rich;
        QStringList imageIds;
    };

    struct EditCommand {
        enum Type { TextChange, Insert, Remove };
        Type type = TextChange;
        int line = -1;         // 操作行
        QString before;        // TextChange/Remove: 修改前文本；Insert: 空
        QString after;         // TextChange/Insert: 修改后文本；Remove: 空
    };

    QString textForLine(int lineNumber) const;
    void pushCommand(const EditCommand &cmd);
    void applyCommand(const EditCommand &cmd, bool undo);

    QVector<LineEntry> m_lines;
    QVector<EditCommand> m_undoStack;
    QVector<EditCommand> m_redoStack;
    bool m_undoEnabled = true;   // undo/redo 回放期间置 false，避免递归记录
    CommentService *m_commentProvider = nullptr;
};
