#include "translationcache.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

TranslationCache::TranslationCache() = default;

QString TranslationCache::key(const QString &text, const TranslationOptions &options) const
{
    // 指纹：原文 + 严格输出 + 温度 + 上下文 + 模型/端点 + 术语约束
    QJsonObject fingerprint;
    fingerprint.insert(QStringLiteral("text"), text);
    fingerprint.insert(QStringLiteral("strict"), options.strictOutput);
    fingerprint.insert(QStringLiteral("temp"), options.temperature);
    fingerprint.insert(QStringLiteral("src"), options.sourceLang);
    fingerprint.insert(QStringLiteral("tgt"), options.targetLang);
    fingerprint.insert(QStringLiteral("ctx"), QJsonArray::fromStringList(options.contextLines));
    fingerprint.insert(QStringLiteral("model"), options.model());
    fingerprint.insert(QStringLiteral("endpoint"), options.apiEndpoint());
    fingerprint.insert(QStringLiteral("glossary"),
                        options.extra.value(QStringLiteral("glossaryConstraint")).toString());

    const QByteArray data = QJsonDocument(fingerprint).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

TranslationResult TranslationCache::get(const QString &key) const
{
    // L1 内存
    const auto it = m_entries.constFind(key);
    if (it != m_entries.constEnd()) {
        TranslationResult result = it->result;
        result.fromCache = true;
        return result;
    }

    // L2 磁盘
    TranslationResult diskResult;
    if (m_diskEnabled && loadFromDisk(key, &diskResult)) {
        diskResult.fromCache = true;
        // 提升到内存
        Entry entry;
        entry.result = diskResult;
        entry.lastAccess = ++m_clock;
        m_entries.insert(key, entry);
        m_lruOrder.prepend(key);
        return diskResult;
    }

    return TranslationResult{};
}

void TranslationCache::put(const QString &key, const TranslationResult &result)
{
    // 已存在则先移除（更新 LRU 位置）
    m_lruOrder.removeAll(key);

    Entry entry;
    entry.result = result;
    entry.lastAccess = ++m_clock;
    m_entries.insert(key, entry);
    m_lruOrder.prepend(key);

    // L2 磁盘
    if (m_diskEnabled) {
        saveToDisk(key, result);
    }

    // LRU 淘汰
    while (m_entries.size() > m_maxEntries && !m_lruOrder.isEmpty()) {
        const QString victim = m_lruOrder.takeLast();
        m_entries.remove(victim);
    }
}

void TranslationCache::setDiskCacheEnabled(bool enabled)
{
    m_diskEnabled = enabled;
}

bool TranslationCache::diskCacheEnabled() const
{
    return m_diskEnabled;
}

QString TranslationCache::diskCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/cache");
    QDir().mkpath(dir);
    return dir;
}

QString TranslationCache::diskPathForKey(const QString &key) const
{
    // 按 key 前缀分片（key 为 64 位 hex，取前 2 位作子目录）
    const QString sub = key.left(2);
    return diskCacheDir() + QLatin1Char('/') + sub + QLatin1Char('/') + key + QStringLiteral(".json");
}

bool TranslationCache::loadFromDisk(const QString &key, TranslationResult *result) const
{
    if (!result) {
        return false;
    }
    QFile file(diskPathForKey(key));
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    result->text = obj.value(QStringLiteral("text")).toString();
    result->success = obj.value(QStringLiteral("success")).toBool();
    result->elapsedMs = obj.value(QStringLiteral("elapsedMs")).toVariant().toLongLong();
    return result->success && !result->text.isEmpty();
}

void TranslationCache::saveToDisk(const QString &key, const TranslationResult &result) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("text"), result.text);
    obj.insert(QStringLiteral("success"), result.success);
    obj.insert(QStringLiteral("elapsedMs"), static_cast<double>(result.elapsedMs));
    obj.insert(QStringLiteral("savedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));

    const QString path = diskPathForKey(key);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
        file.close();
    }
}

void TranslationCache::cleanDiskCache(int maxAgeDays)
{
    const QString dir = diskCacheDir();
    QDir root(dir);
    if (!root.exists()) {
        return;
    }
    const qint64 cutoff = QDateTime::currentDateTime().addDays(-maxAgeDays).toSecsSinceEpoch();
    const QStringList subDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subDirs) {
        QDir subDir(dir + QLatin1Char('/') + sub);
        const QStringList files = subDir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
        for (const QString &fileName : files) {
            const QFileInfo info(subDir.absoluteFilePath(fileName));
            if (info.lastModified().toSecsSinceEpoch() < cutoff) {
                QFile::remove(info.absoluteFilePath());
            }
        }
    }
}

void TranslationCache::clearMemory()
{
    m_entries.clear();
    m_lruOrder.clear();
}

int TranslationCache::size() const
{
    return m_entries.size();
}

int TranslationCache::diskEntryCount() const
{
    // 估算：遍历子目录中的 json 数量（可能较慢，仅供诊断）
    int count = 0;
    const QString dir = diskCacheDir();
    QDir root(dir);
    const QStringList subDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subDirs) {
        count += QDir(dir + QLatin1Char('/') + sub)
                     .entryList(QStringList() << QStringLiteral("*.json"), QDir::Files)
                     .size();
    }
    return count;
}
