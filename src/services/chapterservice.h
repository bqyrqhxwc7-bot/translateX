#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QRegularExpression>

class DocumentModel;
class QTimer;

// 章节服务：按标题行识别文档章节，提供章节列表与"行归属章节"查询。
// 为翻译提供章节级上下文、跳转、分块边界。
class ChapterService : public QObject
{
    Q_OBJECT

public:
    explicit ChapterService(QObject *parent = nullptr);

    // 关联文档模型（QML 中模型在页面内创建，用 setter 注入）
    Q_INVOKABLE void setDocument(DocumentModel *model);

    Q_INVOKABLE void setChapterPattern(const QString &regex);
    Q_INVOKABLE QString chapterPattern() const;

    Q_INVOKABLE void rebuild();   // 全量重建（打开文档后/手动触发）
    Q_INVOKABLE int chapterCount() const;
    Q_INVOKABLE QString chapterTitle(int index) const;
    Q_INVOKABLE int chapterStartLine(int index) const;
    Q_INVOKABLE int chapterAtLine(int lineNumber) const;   // 所属章节 index（-1 无章节）
    Q_INVOKABLE QStringList chapterTitles() const;

signals:
    void chaptersChanged();

private:
    struct Chapter {
        int startLine = 0;
        QString title;
    };

    // QPointer：页面（QML）销毁后自动置空，避免悬挂指针（NoStack 导航每次重建页面）
    QPointer<DocumentModel> m_model;
    QRegularExpression m_pattern;
    QVector<Chapter> m_chapters;
    // 结构变更防抖：行增删后 250ms 自动重建章节索引（避免每次编辑触发全量扫描）
    QTimer *m_debounceTimer = nullptr;
};
