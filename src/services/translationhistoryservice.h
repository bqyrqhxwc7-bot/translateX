#pragma once

#include <QObject>
#include <QList>
#include <QVariantList>

// 翻译历史服务（迭代4b）：记录每次翻译请求（内存环形缓冲，会话级不落盘）。
// 由 QML 在 onLineTranslated 回调调用 record()（QML 是胶水层，不耦合 TranslationService）。
// 见 docs/services/iteration4b-history-markdown-guide.md §1
class TranslationHistoryService : public QObject
{
    Q_OBJECT

public:
    explicit TranslationHistoryService(QObject *parent = nullptr);

    // 记录一条翻译结果（最新在前；超过上限覆盖最旧）
    Q_INVOKABLE void record(int lineNumber, const QString &source,
                            const QString &translated, bool success);
    // 全部条目（最新在前）：[{ line, source, translated, success, time }]
    Q_INVOKABLE QVariantList entries() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE int count() const;

signals:
    void entryAdded();

private:
    struct Entry {
        int line = -1;
        QString source;
        QString translated;
        bool success = false;
        QString time;
    };
    QList<Entry> m_entries;
    static constexpr int kMaxEntries = 500;
};