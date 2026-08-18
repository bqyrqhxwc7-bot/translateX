#include "configservice.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "securestorage.h"

// 静态库场景：qrc 资源需显式初始化（qt_add_library 静态链接时资源不自动注册）
// 注：匿名命名空间 + 文件作用域 static const 语义等价（均为内部链接）；
// 保留 static 形式便于 grep 定位初始化点
struct ConfigResourceInit {
    ConfigResourceInit() { Q_INIT_RESOURCE(config); }
};
static const ConfigResourceInit g_configResourceInit;

QString ConfigService::s_dataDirectoryOverride;

ConfigService *ConfigService::instance()
{
    static ConfigService service;
    return &service;
}

void ConfigService::setDataDirectoryForTest(const QString &dir)
{
    s_dataDirectoryOverride = dir;
}

ConfigService::ConfigService(QObject *parent)
    : QObject(parent)
    , m_settings(settingsPath(), QSettings::IniFormat)
{
    loadBuiltinConfigs();
}

QString ConfigService::serviceId() const
{
    return QStringLiteral("config");
}

QString ConfigService::displayName() const
{
    return QStringLiteral("配置服务");
}

QString ConfigService::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap ConfigService::healthCheck() const
{
    if (m_sections.isEmpty()) {
        return { { QStringLiteral("status"), QStringLiteral("error") },
                 { QStringLiteral("message"), QStringLiteral("配置 schema 未加载") } };
    }
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("配置段 %1 个").arg(m_sections.size()) } };
}

QString ConfigService::settingsPath()
{
    if (!s_dataDirectoryOverride.isEmpty()) {
        QDir().mkpath(s_dataDirectoryOverride);
        return s_dataDirectoryOverride + QStringLiteral("/config.ini");
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/config.ini");
}

void ConfigService::loadBuiltinConfigs()
{
    const QDir dir(QStringLiteral(":/config"));
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (const QString &file : files) {
        QFile f(dir.filePath(file));
        if (f.open(QIODevice::ReadOnly)) {
            loadJson(f.readAll(), file);
        }
    }
    // 内置声明属启动初始化，不触发 sectionsChanged
}

void ConfigService::scanConfigDirectory(const QString &dir)
{
    QDir root(dir);
    if (!root.exists()) {
        return;
    }

    bool changed = false;
    // 支持 <dir>/config.json 与 <dir>/<plugin>/config.json 两种布局
    auto loadFile = [this, &changed](const QString &path) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            loadJson(f.readAll(), path);
            changed = true;
        }
    };

    const QString rootCfg = root.filePath(QStringLiteral("config.json"));
    if (QFile::exists(rootCfg)) {
        loadFile(rootCfg);
    }

    const QStringList subDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subDirs) {
        const QString cfg = root.filePath(sub + QStringLiteral("/config.json"));
        if (QFile::exists(cfg)) {
            loadFile(cfg);
        }
    }

    if (changed) {
        emit sectionsChanged();
    }
}

void ConfigService::loadJson(const QByteArray &json, const QString &sourceName)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "ConfigService: 解析失败" << sourceName << err.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    ConfigSection section;
    section.id = root.value(QStringLiteral("id")).toString();
    section.displayName = root.value(QStringLiteral("displayName")).toString(section.id);
    if (section.id.isEmpty()) {
        qWarning() << "ConfigService: 缺少 id" << sourceName;
        return;
    }

    const QJsonArray settings = root.value(QStringLiteral("settings")).toArray();
    for (const QJsonValue &v : settings) {
        const QJsonObject o = v.toObject();
        ConfigItem item;
        item.key = o.value(QStringLiteral("key")).toString();
        if (item.key.isEmpty()) {
            continue;
        }
        item.displayName = o.value(QStringLiteral("displayName")).toString(item.key);
        item.description = o.value(QStringLiteral("description")).toString();
        item.type = o.value(QStringLiteral("type")).toString(QStringLiteral("string"));
        item.defaultValue = o.value(QStringLiteral("default")).toVariant();
        item.group = o.value(QStringLiteral("group")).toString();
        item.placeholder = o.value(QStringLiteral("placeholder")).toString();
        item.restartRequired = o.value(QStringLiteral("restartRequired")).toBool();
        item.min = o.value(QStringLiteral("min")).toDouble(0);
        item.max = o.value(QStringLiteral("max")).toDouble(0);
        item.step = o.value(QStringLiteral("step")).toDouble(1);

        const QJsonArray opts = o.value(QStringLiteral("options")).toArray();
        for (const QJsonValue &ov : opts) {
            item.options.append(ov.toString());
        }
        section.items.append(item);
    }

    m_sections.insert(section.id, section);
}

QStringList ConfigService::sections() const
{
    return m_sections.keys();
}

QString ConfigService::sectionDisplayName(const QString &id) const
{
    const auto it = m_sections.constFind(id);
    return it == m_sections.constEnd() ? id : it->displayName;
}

QVariantList ConfigService::sectionItems(const QString &id) const
{
    QVariantList list;
    const auto it = m_sections.constFind(id);
    if (it == m_sections.constEnd()) {
        return list;
    }
    for (const ConfigItem &item : it->items) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), item.key);
        m.insert(QStringLiteral("displayName"), item.displayName);
        m.insert(QStringLiteral("description"), item.description);
        m.insert(QStringLiteral("type"), item.type);
        m.insert(QStringLiteral("defaultValue"), item.defaultValue);
        m.insert(QStringLiteral("options"), item.options);
        m.insert(QStringLiteral("group"), item.group);
        m.insert(QStringLiteral("placeholder"), item.placeholder);
        m.insert(QStringLiteral("restartRequired"), item.restartRequired);
        m.insert(QStringLiteral("min"), item.min);
        m.insert(QStringLiteral("max"), item.max);
        m.insert(QStringLiteral("step"), item.step);
        list.append(m);
    }
    return list;
}

QList<ConfigItem> ConfigService::items(const QString &id) const
{
    const auto it = m_sections.constFind(id);
    return it == m_sections.constEnd() ? QList<ConfigItem>() : it->items;
}

const ConfigItem *ConfigService::findItem(const QString &section, const QString &key) const
{
    const auto it = m_sections.constFind(section);
    if (it == m_sections.constEnd()) {
        return nullptr;
    }
    for (const ConfigItem &item : it->items) {
        if (item.key == key) {
            return &item;
        }
    }
    return nullptr;
}

QString ConfigService::storageKey(const QString &section, const QString &key) const
{
    return section + QLatin1Char('/') + key;
}

bool ConfigService::isSecret(const QString &section, const QString &key) const
{
    const ConfigItem *item = findItem(section, key);
    return item && item->type == QStringLiteral("secret");
}

QVariant ConfigService::normalize(const QVariant &value, const QString &type)
{
    if (type == QStringLiteral("number")) {
        return value.toDouble();
    }
    if (type == QStringLiteral("bool")) {
        return value.toBool();
    }
    return value;
}

QVariant ConfigService::get(const QString &section, const QString &key) const
{
    const ConfigItem *item = findItem(section, key);
    if (!item) {
        return QVariant();
    }

    const QString sk = storageKey(section, key);
    if (isSecret(section, key)) {
        const QString plain = SecureStorage::decrypt(m_settings.value(sk).toByteArray());
        return plain.isEmpty() ? item->defaultValue : plain;
    }

    if (!m_settings.contains(sk)) {
        return item->defaultValue;
    }
    return normalize(m_settings.value(sk), item->type);
}

void ConfigService::set(const QString &section, const QString &key, const QVariant &value)
{
    const ConfigItem *item = findItem(section, key);
    if (!item) {
        return;
    }

    const QString sk = storageKey(section, key);
    if (isSecret(section, key)) {
        const QString text = value.toString();
        if (text.isEmpty()) {
            m_settings.remove(sk);
        } else {
            m_settings.setValue(sk, SecureStorage::encrypt(text));
        }
    } else if (value.isValid() && !value.isNull()) {
        m_settings.setValue(sk, normalize(value, item->type));
    } else {
        m_settings.remove(sk); // 无效值 → 恢复默认
    }
    m_settings.sync();

    emit configChanged(section, key, get(section, key));
}

QVariantMap ConfigService::values(const QString &section) const
{
    QVariantMap map;
    const auto it = m_sections.constFind(section);
    if (it == m_sections.constEnd()) {
        return map;
    }
    for (const ConfigItem &item : it->items) {
        map.insert(item.key, get(section, item.key));
    }
    return map;
}

bool ConfigService::isUserSet(const QString &section, const QString &key) const
{
    return m_settings.contains(storageKey(section, key));
}
