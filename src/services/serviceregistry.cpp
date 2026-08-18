#include "serviceregistry.h"

#include <QPluginLoader>
#include <QDir>
#include <QDebug>

#include "itranslationplugin.h"

ServiceRegistry *ServiceRegistry::instance()
{
    static ServiceRegistry registry;
    return &registry;
}

ServiceRegistry::ServiceRegistry(QObject *parent)
    : QObject(parent)
{
}

void ServiceRegistry::registerBackend(const QString &id, BackendFactory factory, const QString &displayName)
{
    BackendEntry entry;
    entry.factory = std::move(factory);
    entry.displayName = displayName;
    m_backends.insert(id, std::move(entry));
}

std::shared_ptr<ITranslationBackend> ServiceRegistry::createBackend(const QString &id) const
{
    const auto it = m_backends.constFind(id);
    if (it == m_backends.constEnd() || !it->factory) {
        return nullptr;
    }
    return it->factory();
}

QStringList ServiceRegistry::availableBackends() const
{
    return m_backends.keys();
}

QString ServiceRegistry::backendDisplayName(const QString &id) const
{
    const auto it = m_backends.constFind(id);
    return it == m_backends.constEnd() ? id : it->displayName;
}

void ServiceRegistry::registerService(QObject *service)
{
    if (!service) {
        return;
    }
    auto *iface = qobject_cast<IService *>(service);
    if (!iface) {
        qWarning() << "ServiceRegistry: 对象未实现 IService 接口，忽略注册" << service->metaObject()->className();
        return;
    }
    m_services.insert(iface->serviceId(), service);
}

QVariantList ServiceRegistry::services() const
{
    QVariantList out;
    out.reserve(m_services.size());
    for (QObject *service : m_services) {
        out.append(QVariant::fromValue(service));
    }
    return out;
}

QObject *ServiceRegistry::serviceById(const QString &id) const
{
    return m_services.value(id, nullptr);
}

QVariantList ServiceRegistry::healthReport() const
{
    QVariantList out;
    out.reserve(m_services.size());
    for (QObject *service : m_services) {
        auto *iface = qobject_cast<IService *>(service);
        if (!iface) {
            continue;
        }
        const QVariantMap health = iface->healthCheck();
        QVariantMap item;
        item.insert(QStringLiteral("id"), iface->serviceId());
        item.insert(QStringLiteral("displayName"), iface->displayName());
        item.insert(QStringLiteral("version"), iface->serviceVersion());
        item.insert(QStringLiteral("status"), health.value(QStringLiteral("status"), QStringLiteral("ok")));
        item.insert(QStringLiteral("message"), health.value(QStringLiteral("message")));
        item.insert(QStringLiteral("detail"), health.value(QStringLiteral("detail")));
        out.append(item);
    }
    return out;
}

void ServiceRegistry::scanPluginDirectory(const QString &dir)
{
    QDir pluginsDir(dir);
    if (!pluginsDir.exists()) {
        return;
    }

    const QStringList filters = QStringList()
        << QStringLiteral("*.dll")
        << QStringLiteral("*.so")
        << QStringLiteral("*.dylib");

    const QStringList entries = pluginsDir.entryList(filters, QDir::Files);
    for (const QString &fileName : entries) {
        auto *loader = new QPluginLoader(pluginsDir.absoluteFilePath(fileName), this);
        if (!loader->load()) {
            m_pluginErrors.append(loader->errorString());
            delete loader;
            continue;
        }

        QObject *pluginInstance = loader->instance();
        if (!pluginInstance) {
            m_pluginErrors.append(fileName + QStringLiteral(": instance() failed"));
            delete loader;
            continue;
        }

        // 统一接口：ITranslationPlugin（Q_DECLARE_INTERFACE + Q_PLUGIN_METADATA）
        auto *plugin = qobject_cast<ITranslationPlugin *>(pluginInstance);
        if (!plugin) {
            m_pluginErrors.append(fileName + QStringLiteral(": 未实现 ITranslationPlugin 接口"));
            delete loader;
            continue;
        }

        const QStringList backendIds = plugin->backendIds();
        for (const QString &id : backendIds) {
            registerBackend(id, [plugin, id]() { return plugin->createBackend(id); },
                            id);
        }
        m_pluginLoaders.append(loader);
        qInfo() << "ServiceRegistry: 插件加载成功" << fileName << "后端:" << backendIds;
    }
}

QStringList ServiceRegistry::loadedPluginErrors() const
{
    return m_pluginErrors;
}