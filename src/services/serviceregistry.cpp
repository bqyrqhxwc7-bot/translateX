#include "serviceregistry.h"

#include <QPluginLoader>
#include <QDir>
#include <QMetaMethod>
#include <QDebug>

namespace {

// 供内部后端注册的辅助
struct BuiltinRegistrar {
    BuiltinRegistrar()
    {
        // 各后端在自身 cpp 中通过 registerBuiltinBackends 注册
        registerBuiltinBackends();
    }
    void registerBuiltinBackends();
};

void registerBuiltinBackends()
{
    // 具体后端在对应 cpp 中通过 RegisterBackendHelper 静态对象注册
    // 这里留空；后端文件各自包含注册逻辑
}

} // namespace

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

        // 约定：插件实例提供 registerTo(ServiceRegistry*) 槽方法
        const int metaIndex = pluginInstance->metaObject()->indexOfMethod("registerTo(ServiceRegistry*)");
        if (metaIndex < 0) {
            m_pluginErrors.append(fileName + QStringLiteral(": 缺少 registerTo(ServiceRegistry*) 方法"));
            delete loader;
            continue;
        }

        const QMetaMethod method = pluginInstance->metaObject()->method(metaIndex);
        method.invoke(pluginInstance, Q_ARG(ServiceRegistry *, this));
        m_pluginLoaders.append(loader);
    }
}

QStringList ServiceRegistry::loadedPluginErrors() const
{
    return m_pluginErrors;
}
