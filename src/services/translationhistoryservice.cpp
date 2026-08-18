#include "translationhistoryservice.h"

#include <QDateTime>
#include <QVariantMap>

TranslationHistoryService::TranslationHistoryService(QObject *parent)
    : QObject(parent)
{
}

QString TranslationHistoryService::serviceId() const
{
    return QStringLiteral("translationHistory");
}

QString TranslationHistoryService::displayName() const
{
    return QStringLiteral("翻译历史");
}

QString TranslationHistoryService::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap TranslationHistoryService::healthCheck() const
{
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("记录 %1 条").arg(count()) } };
}

void TranslationHistoryService::record(int lineNumber, const QString &source,
                                       const QString &translated, bool success)
{
    Entry e;
    e.line = lineNumber;
    e.source = source;
    e.translated = translated;
    e.success = success;
    e.time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_entries.prepend(e);
    while (m_entries.size() > kMaxEntries) {
        m_entries.removeLast();
    }
    emit entryAdded();
}

QVariantList TranslationHistoryService::entries() const
{
    QVariantList out;
    out.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        QVariantMap item;
        item.insert(QStringLiteral("line"), e.line);
        item.insert(QStringLiteral("source"), e.source);
        item.insert(QStringLiteral("translated"), e.translated);
        item.insert(QStringLiteral("success"), e.success);
        item.insert(QStringLiteral("time"), e.time);
        out.append(item);
    }
    return out;
}

void TranslationHistoryService::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    emit entryAdded();
}

int TranslationHistoryService::count() const
{
    return m_entries.size();
}