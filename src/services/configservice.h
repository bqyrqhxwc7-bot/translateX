#pragma once

#include <QObject>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
#include <QSettings>

// 配置项（对应 config.json 的一条 settings）
struct ConfigItem {
    QString key;             // 如 "apiEndpoint"
    QString displayName;     // 显示名
    QString description;     // 帮助文案
    QString type;            // string|multiline|number|bool|enum|secret|path
    QVariant defaultValue;
    QStringList options;     // enum 固定选项
    double min = 0;          // number 下限
    double max = 0;          // number 上限（0=无上限）
    double step = 1;         // number 步长
    QString group;           // 设置页分组标题
    bool restartRequired = false;
    QString placeholder;
};

// 配置段（对应一个 config.json，类比 VSCode 的 contributes.configuration）
struct ConfigSection {
    QString id;              // 如 "translation.network_model"
    QString displayName;
    QList<ConfigItem> items;
};

// 配置服务（单例）：服务提供者声明配置 → 统一读写/持久化/加密 → 通知变化。
// 类比 VSCode：getConfiguration()/update()/onDidChangeConfiguration + 设置面板自动生成。
class ConfigService : public QObject
{
    Q_OBJECT

public:
    static ConfigService *instance();
    // 测试用：覆盖数据目录（必须在首次 instance() 之前调用）
    static void setDataDirectoryForTest(const QString &dir);

    // ---- 声明加载 ----
    void loadBuiltinConfigs();                     // 从 qrc:/config/*.json 加载内置声明
    void scanConfigDirectory(const QString &dir);  // 扫描插件目录中的 config.json

    // ---- schema 查询（QML 设置页自动生成 UI 用）----
    Q_INVOKABLE QStringList sections() const;
    Q_INVOKABLE QString sectionDisplayName(const QString &id) const;
    Q_INVOKABLE QVariantList sectionItems(const QString &id) const;
    QList<ConfigItem> items(const QString &id) const;
    const ConfigItem *findItem(const QString &section, const QString &key) const;

    // ---- 读写（VSCode getConfiguration 语义）----
    Q_INVOKABLE QVariant get(const QString &section, const QString &key) const;
    Q_INVOKABLE void set(const QString &section, const QString &key, const QVariant &value);
    QVariantMap values(const QString &section) const;  // 某段全部当前值（默认值填充）
    Q_INVOKABLE bool isUserSet(const QString &section, const QString &key) const;

signals:
    void configChanged(const QString &section, const QString &key, const QVariant &value);
    void sectionsChanged();   // schema 变化（扫描插件后）

private:
    explicit ConfigService(QObject *parent = nullptr);
    static QString settingsPath();
    void loadJson(const QByteArray &json, const QString &sourceName);
    QString storageKey(const QString &section, const QString &key) const;
    bool isSecret(const QString &section, const QString &key) const;
    static QVariant normalize(const QVariant &value, const QString &type);

    QHash<QString, ConfigSection> m_sections;
    QSettings m_settings;
    static QString s_dataDirectoryOverride;
};
