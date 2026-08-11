#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>

#include "itranslationbackend.h"

class QPluginLoader;

// 后端工厂（可插拔：内置 + 第三方插件统一注册）
using BackendFactory = std::function<std::shared_ptr<ITranslationBackend>()>;

// 服务注册表（单例）：管理可用的翻译后端
class ServiceRegistry : public QObject
{
    Q_OBJECT

public:
    static ServiceRegistry *instance();

    // 注册后端
    void registerBackend(const QString &id, BackendFactory factory, const QString &displayName);

    // 按 ID 创建后端实例
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) const;

    // 可用后端列表 / 显示名
    QStringList availableBackends() const;
    QString backendDisplayName(const QString &id) const;

    // 扫描插件目录（L3 动态插件）
    void scanPluginDirectory(const QString &dir);

    // 已加载插件信息（诊断用）
    QStringList loadedPluginErrors() const;

private:
    explicit ServiceRegistry(QObject *parent = nullptr);

    struct BackendEntry {
        BackendFactory factory;
        QString displayName;
    };

    QHash<QString, BackendEntry> m_backends;
    QList<QPluginLoader *> m_pluginLoaders;
    QStringList m_pluginErrors;
};
