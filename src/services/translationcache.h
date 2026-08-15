#pragma once

#include <QHash>
#include <QString>
#include <QList>

#include "itranslationbackend.h"

// 翻译缓存：L1 内存（LRU）+ L2 磁盘（按日期分片，限额清理）。
// 命中磁盘缓存时零成本复用，重复文档/回译场景显著降本。
class TranslationCache
{
public:
    TranslationCache();

    // 缓存键：原文 + 关键选项指纹
    QString key(const QString &text, const TranslationOptions &options) const;

    // 命中返回 success=true 的结果；未命中返回 success=false 的空结果
    TranslationResult get(const QString &key) const;

    // 写入缓存（L1 内存 + L2 磁盘）
    void put(const QString &key, const TranslationResult &result);

    // 磁盘缓存开关（默认开）
    void setDiskCacheEnabled(bool enabled);
    bool diskCacheEnabled() const;
    // 磁盘缓存目录（%APPDATA%/Translex/cache/）
    static QString diskCacheDir();
    // 清理超过最大天数的磁盘缓存
    void cleanDiskCache(int maxAgeDays = 14);

    void clearMemory();
    int size() const;          // L1 内存条目数
    int diskEntryCount() const; // L2 磁盘条目数（估算）

private:
    QString diskPathForKey(const QString &key) const;
    bool loadFromDisk(const QString &key, TranslationResult *result) const;
    void saveToDisk(const QString &key, const TranslationResult &result) const;

    struct Entry {
        TranslationResult result;
        qint64 lastAccess = 0;
    };

    mutable QHash<QString, Entry> m_entries;
    mutable QList<QString> m_lruOrder; // 队首最新
    int m_maxEntries = 5000;
    mutable qint64 m_clock = 0;
    bool m_diskEnabled = true;
};
