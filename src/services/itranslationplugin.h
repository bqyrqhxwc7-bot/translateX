#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

class ITranslationBackend;

// 翻译插件接口（L3 动态插件，随 Translex_sdk 发布）。
// 第三方插件编译为独立 .dll 放入 <exe_dir>/plugins/，实现本接口：
//   - 提供自定义翻译后端（backendIds/createBackend）
//   - 可选：提供侧边栏面板 QML（sidebarPanel，插件 UI 扩展点）
// 应用启动时 ServiceRegistry::scanPluginDirectory() 用 QPluginLoader 扫描加载。
// 见 docs/services/SERVICE-ARCHITECTURE.md §4 与 docs/services/plugin-development.md
class ITranslationPlugin : public QObject
{
    Q_OBJECT

public:
    explicit ITranslationPlugin(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~ITranslationPlugin() override = default;

    // 返回此插件提供的后端 ID 列表
    virtual QStringList backendIds() const = 0;
    // 创建后端实例（ID 不在 backendIds() 中时返回 nullptr）
    virtual std::shared_ptr<ITranslationBackend> createBackend(const QString &id) = 0;

    // 侧边栏面板 QML 组件 URL（可选；空=无面板）。
    // 插件目录下的 .qml 文件路径（如 "qrc:/plugins/MyPanel.qml" 或绝对路径）。
    virtual QString sidebarPanel() const { return QString(); }
};

#define TranslexPlugin_iid "org.translex.ITranslationPlugin/1.0"
Q_DECLARE_INTERFACE(ITranslationPlugin, TranslexPlugin_iid)