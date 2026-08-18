# 插件开发指南（L3 动态插件）

> 迭代5（2026-08-18）落地，基于实际实现撰写；接口定义一律以 `src/services/` 头文件与 `Translex_sdk` 为准。
> 可对照示例插件：`plugins/example_translation_plugin/`（回显后端 `translation.echo` + 侧边栏面板）。

## 1. 背景：三种集成层级

| 层级 | 机制 | 适用 | 是否需改核心 |
| --- | --- | --- | --- |
| **L1 内置服务** | 编译进应用，`qmlRegisterType` 注册 | 核心服务（DocumentModel 等） | 否 |
| **L2 注册式扩展** | 实现 C++ 接口，核心提供注册 API | 翻译后端（Ollama/云端/网络） | 否（核心已定义接口） |
| **L3 动态插件** | `QPluginLoader` 加载 `<exe>/plugins/*.dll` | 第三方自定义后端 / 侧边栏面板 | 否（纯外部） |

本指南面向 **L3**：第三方把插件编译为独立 DLL，放入主程序 `plugins/` 目录，启动时自动加载，无需改动核心代码与 QML。

## 2. 插件接口（Translex_sdk 导出）

第三方插件需要实现 **`ITranslationPlugin`**，可选提供自定义翻译后端（实现 `ITranslationBackend`）与侧边栏面板（`sidebarPanel()`）。

### 2.1 ITranslationPlugin（插件入口，`itranslationplugin.h`）

```cpp
class ITranslationPlugin
{
public:
    virtual ~ITranslationPlugin() = default;

    virtual QStringList backendIds() const = 0;                      // 提供的后端 ID 列表
    virtual std::shared_ptr<ITranslationBackend> createBackend(const QString &id) = 0;
    virtual QString sidebarPanel() const { return QString(); }        // 可选：侧边栏面板 QML 路径
};

#define TranslexPlugin_iid "org.translex.ITranslationPlugin/1.0"
Q_DECLARE_INTERFACE(ITranslationPlugin, TranslexPlugin_iid)
```

- 纯虚接口**不继承 QObject**（避免插件类菱形继承）；插件类自身 `class MyPlugin : public QObject, public ITranslationPlugin`。
- 元数据：`Q_PLUGIN_METADATA(IID TranslexPlugin_iid)` + `Q_INTERFACES(ITranslationPlugin)`（`Q_DECLARE_INTERFACE` 机制）。

### 2.2 ITranslationBackend（翻译后端，`itranslationbackend.h`）

```cpp
class ITranslationBackend : public QObject {
public:
    virtual QString backendId() const = 0;          // 稳定唯一，如 "translation.mybackend"
    virtual QString displayName() const = 0;
    virtual bool supportsContext() const { return false; }
    virtual bool supportsStreaming() const { return false; }
    virtual TranslationResult translate(
        const QString &text, const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) = 0;   // 同步返回；长耗时调用方放线程池
    // 批量翻译（默认逐条循环，可覆盖做合并/流式优化）
    virtual QList<QPair<int, TranslationResult>> translateBatch(...);
    virtual QString healthCheck() const { return QString(); }        // 空串=正常
    virtual void updateConfig(const QVariantMap &config) { Q_UNUSED(config); }
};
```

- `TranslationResult` / `TranslationOptions` 结构体定义在 `itranslationbackend.h`。
- 后端同步接口，UI 侧经 `QtConcurrent` 线程池异步调用，插件内无需自带线程。

### 2.3 IService（服务健康度，`iservice.h`）

插件若要参与设置页「调试」卡片的健康度列表，可让插件的 QObject 类再实现 `IService`（`Q_INTERFACES(IService)`）：

```cpp
class IService
{
public:
    virtual ~IService() = default;
    virtual QString serviceId() const = 0;      // 稳定唯一，如 "translation"
    virtual QString displayName() const = 0;
    virtual QString serviceVersion() const = 0;
    // 健康检查：返回 { status: "ok"/"warn"/"error", message, detail }
    virtual QVariantMap healthCheck() const = 0;
    // 侧边栏面板 QML 组件 URL（可选；空=无面板）
    virtual QString sidebarPanel() const { return QString(); }
};
#define TranslexService_iid "org.translex.IService/1.0"
Q_DECLARE_INTERFACE(IService, TranslexService_iid)
```

> 插件面板注册不经过 `qmlRegisterType`：`sidebarPanel()` 返回本地 QML 文件路径（绝对路径或 `qrc:/...`），
> `Main.qml` 经 `serviceRegistry.sidebarPanels()` 读到后放入图标栏，点击时用 `Loader.source` 加载该文件。

## 3. 插件目录与加载机制

- 插件 DLL（及面板 QML）放入 `<exe>/plugins/`（与 `translex.exe` 同级的 `plugins/` 目录）。
- 应用启动时 `main_qml.cpp` 调用 `registry->scanPluginDirectory(QCoreApplication::applicationDirPath() + "/plugins")`（`src/main_qml.cpp:99`）。
- `scanPluginDirectory`（`serviceregistry.cpp`）用 `QPluginLoader` 扫描 `*.dll` / `*.so` / `*.dylib`，逐个 `load()` → `instance()` → `qobject_cast<ITranslationPlugin*>`：
  - 加载失败 / `instance()` 失败 / 未实现接口 → 记录到 `loadedPluginErrors()`（设置页「调试」卡片展示），不影响其他插件。
  - 成功 → `backendIds()` 逐个 `registerBackend`，并持有 `QPluginLoader` 保持插件存活。
- 加载的后端自动出现在设置页后端列表与 Ribbon/浮窗后端切换中（`availableBackends()` 机制），无需改 QML。

## 4. ServiceRegistry 注册/查询 API（`serviceregistry.h`）

进程内单例：`ServiceRegistry::instance()`。插件运行期可读（QML 亦可直接调用）：

| API | 说明 |
| --- | --- |
| `registerBackend(id, factory, displayName)` | 注册后端工厂（内置 + 插件统一走这里） |
| `createBackend(id)` | 按 ID 创建后端实例（未注册返回 nullptr） |
| `availableBackends()` / `backendDisplayName(id)` | 列出可用后端 / 显示名 |
| `registerService(QObject*)` | 注册服务（`qobject_cast<IService*>` 校验，重复 ID 覆盖） |
| `services()` / `serviceById(id)` | 全部服务 / 按 ID 查询 |
| `healthReport()` | 健康度聚合：`[{ id, displayName, version, status, message, detail? }]` |
| `sidebarPanels()` | 注册了 `sidebarPanel()` 的服务：`[{ id, displayName, panel }]` |
| `scanPluginDirectory(dir)` | 扫描并加载插件目录 |
| `loadedPluginErrors()` | 插件加载错误列表 |

## 5. 插件结构模板

最小插件（对照 `plugins/example_translation_plugin/exampleplugin.h`）：

```cpp
// myplugin.h
#include <QObject>
#include "itranslationbackend.h"
#include "itranslationplugin.h"

class MyBackend : public ITranslationBackend
{
public:
    QString backendId() const override { return QStringLiteral("translation.mybackend"); }
    QString displayName() const override { return QStringLiteral("我的后端"); }
    TranslationResult translate(const QString &text, const TranslationOptions &options,
                                const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        Q_UNUSED(options); Q_UNUSED(cancelFlag);
        TranslationResult r;
        r.text = QStringLiteral("[My] ") + text;
        r.success = true;
        return r;
    }
};

class MyPlugin : public QObject, public ITranslationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TranslexPlugin_iid)
    Q_INTERFACES(ITranslationPlugin)
public:
    QStringList backendIds() const override { return { QStringLiteral("translation.mybackend") }; }
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) override
    {
        if (id == QStringLiteral("translation.mybackend"))
            return std::make_shared<MyBackend>();
        return nullptr;
    }
    // 可选：侧边栏面板（QML 文件随插件分发，与 DLL 同放 <exe>/plugins/）
    QString sidebarPanel() const override
    {
        return QCoreApplication::applicationDirPath()
               + QStringLiteral("/plugins/MyPanel.qml");
    }
};
```

## 6. 构建示例（CMake）

顶层 `CMakeLists.txt` 已定义 **`Translex_sdk`**（INTERFACE 目标，导出 `src/services` 公共头 + `Qt6::Core`），插件只需链接它：

```cmake
# plugins/my_plugin/CMakeLists.txt
cmake_minimum_required(VERSION 3.21)
project(my_plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

qt_add_library(my_plugin SHARED
    myplugin.cpp
    myplugin.h
)
target_link_libraries(my_plugin PRIVATE Translex_sdk)

# 部署：把 DLL + 面板 QML 拷到 <exe>/plugins/（MSVC 主构建）
if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT MINGW)
    add_custom_command(TARGET my_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:translex>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:my_plugin>" "$<TARGET_FILE_DIR:translex>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/qml/MyPanel.qml" "$<TARGET_FILE_DIR:translex>/plugins"
    )
endif()
```

> 完整参考：`plugins/example_translation_plugin/CMakeLists.txt`。需在顶层 `CMakeLists.txt` 加 `add_subdirectory(plugins/xxx)`。

## 7. 调试与验证

1. **构建**：`cmake --build build-vs2026-x64 --config Debug`（示例插件自动部署到 `build-vs2026-x64\Debug\plugins\`）。
2. **启动**：运行 `translex.exe`，控制台/日志（`%APPDATA%/sr291/Translex/Translex-<yyyyMMdd>.log`）应出现 `插件加载成功`。
3. **设置页「调试」卡片**：查看服务健康度列表、插件加载诊断（`loadedPluginErrors()`）、配置/日志路径。
4. **全链路验证**：设置页切换到插件后端 → 选行翻译 → 批注写入回显结果；图标栏出现插件面板入口（`sidebarPanel()` 非空时）。
5. **自动化**：`tst_registry`（`tests/tst_registry.cpp`）覆盖服务注册/按 ID 查询/重复 ID 覆盖/健康度聚合/后端注册；
   UI 驱动 `getBackends` 命令可查询当前已注册后端列表（含插件后端）。

## 8. 限制（明示）

- 插件当前仅支持「翻译后端 + 侧边栏面板」两类扩展点（最小集，后续可扩）。
- 插件需与主程序**同 Qt 版本 / 同编译器**（MSVC 静态 Qt，动态加载限制）。
- 侧边栏宽度等布局状态经 `ui.*` 配置持久化（像素值），不存布局树。
- 插件 DLL 若引用了主程序私有符号以外的 Qt 库，须自带部署（windeployqt 不覆盖插件目录）。
