#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "itranslationbackend.h"
#include "iservice.h"

class QPluginLoader;
class ITranslationPlugin;

// 后端工厂（可插拔：内置 + 第三方插件统一注册）
using BackendFactory = std::function<std::shared_ptr<ITranslationBackend>()>;

// 服务注册表（单例）：管理可用翻译后端 + 应用级服务（迭代5 插件化 A3）。
// - 后端：registerBackend/createBackend（内置 + 插件统一）
// - 服务：registerService/services/healthReport（IService 健康度聚合）
// - 插件：scanPluginDirectory 用 QPluginLoader + ITranslationPlugin（Q_DECLARE_INTERFACE）
class ServiceRegistry : public QObject
{
    Q_OBJECT

public:
    static ServiceRegistry *instance();

    // ---- 翻译后端 ----
    void registerBackend(const QString &id, BackendFactory factory, const QString &displayName);
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) const;
    QStringList availableBackends() const;
    QString backendDisplayName(const QString &id) const;

    // ---- 服务注册（IService，迭代5）----
    // 注册应用级服务（qobject_cast<IService*> 校验；重复 ID 覆盖）
    void registerService(QObject *service);
    // 全部已注册服务（QObject*，QML 可遍历）
    QVariantList services() const;
    // 按 ID 查询（未注册返回 nullptr）
    Q_INVOKABLE QObject *serviceById(const QString &id) const;
    // 健康度聚合：[{ id, displayName, version, status, message }]
    Q_INVOKABLE QVariantList healthReport() const;
    // 注册了侧边栏面板的 service（迭代5 UI 扩展点）：
    // [{ id, displayName, panel }]（panel = sidebarPanel() QML 组件 URL）
    Q_INVOKABLE QVariantList sidebarPanels() const;

    // ---- 插件（L3 动态插件）----
    void scanPluginDirectory(const QString &dir);
    Q_INVOKABLE QStringList loadedPluginErrors() const;

private:
    explicit ServiceRegistry(QObject *parent = nullptr);

    struct BackendEntry {
        BackendFactory factory;
        QString displayName;
    };

    QHash<QString, BackendEntry> m_backends;
    QHash<QString, QObject *> m_services;   // serviceId → service
    QList<QPluginLoader *> m_pluginLoaders;
    QStringList m_pluginErrors;
};