#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <memory>

#include "itranslationbackend.h"
#include "itranslationplugin.h"

// 示例后端：回显（演示用，不真正翻译）——验证插件后端全链路：
// 编译 → 部署到 <exe>/plugins/ → scanPluginDirectory 加载 → 设置页可见 → 可翻译
class EchoBackend : public ITranslationBackend
{
public:
    QString backendId() const override { return QStringLiteral("translation.echo"); }
    QString displayName() const override { return QStringLiteral("示例回显后端（插件）"); }

    TranslationResult translate(const QString &text,
                                const TranslationOptions &options,
                                const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        Q_UNUSED(options);
        Q_UNUSED(cancelFlag);
        TranslationResult r;
        r.text = QStringLiteral("[Echo] ") + text;
        r.success = true;
        return r;
    }
};

// 插件入口：Q_PLUGIN_METADATA + Q_INTERFACES（Q_DECLARE_INTERFACE 机制）
class ExampleTranslationPlugin : public QObject, public ITranslationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TranslexPlugin_iid)
    Q_INTERFACES(ITranslationPlugin)

public:
    QStringList backendIds() const override
    {
        return { QStringLiteral("translation.echo") };
    }

    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) override
    {
        if (id == QStringLiteral("translation.echo")) {
            return std::make_shared<EchoBackend>();
        }
        return nullptr;
    }

    // 侧边栏面板：插件目录下的 QML（部署时与 DLL 同放 <exe>/plugins/）
    QString sidebarPanel() const override
    {
        return QCoreApplication::applicationDirPath()
               + QStringLiteral("/plugins/ExamplePanel.qml");
    }
};