#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QList>
#include <QRegularExpression>

class DocumentModel;

// 查找替换服务：基于 DocumentModel 数据（而非 UI）全文查找/替换。
// 虚拟化编辑器只渲染可见行，查找必须直接遍历模型数据。
class FindService : public QObject
{
    Q_OBJECT

public:
    explicit FindService(QObject *parent = nullptr);

    // 关联文档模型（QML 中模型在页面内创建，用 setter 注入）
    Q_INVOKABLE void setDocument(DocumentModel *model);

    // 查找（返回含至少一次匹配的行号列表；空查询返回空）
    Q_INVOKABLE QList<int> find(const QString &query, bool caseSensitive = false, bool wholeWord = false);
    Q_INVOKABLE int count(const QString &query, bool caseSensitive = false, bool wholeWord = false);
    Q_INVOKABLE int findNext(const QString &query, int fromLine, bool caseSensitive = false,
                             bool wholeWord = false, bool wrap = true) const;
    Q_INVOKABLE int findPrevious(const QString &query, int fromLine, bool caseSensitive = false,
                                 bool wholeWord = false, bool wrap = true) const;

    // 替换
    Q_INVOKABLE bool replaceLine(int lineNumber, const QString &query, const QString &replacement,
                                 bool caseSensitive = false, bool wholeWord = false);
    Q_INVOKABLE int replaceAll(const QString &query, const QString &replacement,
                               bool caseSensitive = false, bool wholeWord = false);

signals:
    void searchCompleted(int resultCount);   // find/count 完成（供 UI 更新状态）

private:
    QRegularExpression makePattern(const QString &query, bool caseSensitive, bool wholeWord) const;

    // QPointer：页面（QML）销毁后自动置空，避免悬挂指针（NoStack 导航每次重建页面）
    QPointer<DocumentModel> m_model;
};
