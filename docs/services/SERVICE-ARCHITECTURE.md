# Translex Service 架构与插件规范

> 状态：已与实现对齐（2026-08-18）
> 本文档定义 service 的**提供方式**，是第三方开发者编写插件的依据。
> **接口定义一律以 `src/services/` 头文件为准**（`iservice.h` / `itranslationbackend.h` / `itranslationplugin.h` / `serviceregistry.h`），本文档为说明性汇总，冲突时以头文件为准。

## 1. 目标

- **前后端分离**：UI（QML）只消费服务 API，不包含业务逻辑
- **可插拔**：任何服务（翻译后端、批注、章节、查找）都可独立替换/扩展
- **第三方可写插件**：外部开发者无需改动核心代码即可接入新能力
- **文档先行**：每个服务有独立文档，接口稳定

## 2. Service 提供方式总览

```
┌─────────────────────────────────────────────────┐
│  QML UI (import Translex.Services 1.0)        │
├─────────────────────────────────────────────────┤
│  服务注册表 ServiceRegistry (单例)              │
│  ├─ registerBackend("ollama", Factory)          │
│  └─ createBackend("ollama")→ITranslationBackend │
├─────────────────────────────────────────────────┤
│  内置服务 (src/services)                        │
│  DocumentModel / SecureStorage / AppGuard       │
│  TranslationService / CommentService / ...      │
├─────────────────────────────────────────────────┤
│  外部插件 (QPluginLoader 动态加载)              │
│  plugins/*.dll → 实现 ITranslationPlugin 接口      │
└─────────────────────────────────────────────────┘
```

### 三种集成层级

| 层级 | 机制 | 适用 | 是否需改核心 |
| --- | --- | --- | --- |
| **L1 内置服务** | 编译进应用，`qmlRegisterType` 注册 | 核心服务（DocumentModel 等） | 否 |
| **L2 注册式扩展** | 实现 C++ 接口类，核心提供注册 API | 翻译后端（Ollama/云端/网络） | 否（核心已定义接口） |
| **L3 动态插件** | `QPluginLoader` 加载 `.dll` | 第三方自定义后端/能力 | 否（纯外部） |

## 3. 接口规范

### 3.1 服务基类

**纯虚接口，不继承 `QObject`**（service 类本身是 QObject，避免多重继承；注册表经
`qobject_cast<IService*>(obj)` 转换，service 类需 `Q_INTERFACES(IService)`）。
定义见 `src/services/iservice.h`：

```cpp
class IService {
public:
    virtual ~IService() = default;
    // 服务标识（稳定、唯一，如 "translation"）
    virtual QString serviceId() const = 0;
    // 服务名（显示用）
    virtual QString displayName() const = 0;
    // 服务版本
    virtual QString serviceVersion() const = 0;
    // 健康检查：返回 { status: "ok"/"warn"/"error", message, detail }
    virtual QVariantMap healthCheck() const = 0;
    // 侧边栏面板 QML 组件 URL（可选；空=无面板）——插件 UI 扩展点
    virtual QString sidebarPanel() const { return QString(); }
};

#define TranslexService_iid "org.translex.IService/1.0"
Q_DECLARE_INTERFACE(IService, TranslexService_iid)
```

### 3.2 翻译后端接口

定义见 `src/services/itranslationbackend.h`（`TranslationOptions`/`TranslationResult` 结构体同头文件）。
**不使用 `Q_OBJECT`**（纯虚接口、无信号槽），便于第三方插件继承，也避免 AUTOMOC 额外处理：

```cpp
class ITranslationBackend : public QObject {
public:
    explicit ITranslationBackend(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~ITranslationBackend() = default;

    // 后端标识（稳定唯一，如 "translation.ollama"）
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    // 是否支持上下文翻译
    virtual bool supportsContext() const { return false; }
    // 是否支持流式
    virtual bool supportsStreaming() const { return false; }

    // 单条翻译（同步返回；长耗时由调用方放入线程池）
    virtual TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) = 0;

    // 批量/上下文翻译（默认逐条循环；后端可覆盖做合并/流式优化）
    virtual QList<QPair<int, TranslationResult>> translateBatch(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag);

    // 健康检查：空串=正常
    virtual QString healthCheck() const { return QString(); }
    // 配置更新（后端自行决定是否使用）
    virtual void updateConfig(const QVariantMap &config) { Q_UNUSED(config); }
};
```

### 3.3 ServiceRegistry（单例）

```cpp
// 后端工厂：创建指定 ID 的后端实例
using BackendFactory = std::function<std::shared_ptr<ITranslationBackend>()>;

class ServiceRegistry : public QObject {
    Q_OBJECT
public:
    static ServiceRegistry *instance();

    // ---- 翻译后端 ----
    // 注册后端（核心内置 + 第三方插件都走这里）
    void registerBackend(const QString &id, BackendFactory factory, const QString &displayName);
    // 按 ID 创建（未注册返回 nullptr）
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) const;
    // 列出可用后端
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
    // 注册了侧边栏面板的 service：[{ id, displayName, panel }]
    Q_INVOKABLE QVariantList sidebarPanels() const;

    // ---- 插件目录扫描（L3）----
    void scanPluginDirectory(const QString &dir);
    Q_INVOKABLE QStringList loadedPluginErrors() const;
};
```

## 4. 动态插件机制（L3，供第三方）

### 4.1 插件接口

第三方插件编译为独立 `.dll`，实现 `src/services/itranslationplugin.h` 中的接口
（**纯虚接口，不继承 QObject**——插件类自身继承 `QObject` + 本接口，避免菱形继承）：

```cpp
// itranslationplugin.h （随 SDK 发布）
class ITranslationPlugin {
public:
    virtual ~ITranslationPlugin() = default;
    // 返回此插件提供的后端 ID 列表
    virtual QStringList backendIds() const = 0;
    // 创建后端实例（ID 不在 backendIds() 中时返回 nullptr）
    virtual std::shared_ptr<ITranslationBackend> createBackend(const QString &id) = 0;
    // 侧边栏面板 QML 组件 URL（可选；空=无面板）——插件 UI 扩展点
    virtual QString sidebarPanel() const { return QString(); }
};

#define TranslexPlugin_iid "org.translex.ITranslationPlugin/1.0"
Q_DECLARE_INTERFACE(ITranslationPlugin, TranslexPlugin_iid)
```

### 4.2 插件接入流程

1. 插件 `.dll` 放入 `<exe_dir>/plugins/` 目录
2. 应用启动时 `ServiceRegistry::scanPluginDirectory()` 用 `QPluginLoader` 扫描
3. 加载成功 → 调 `instance()` 得到 `ITranslationPlugin*`
4. 注册其 `backendIds()` 到注册表
5. QML 设置页自动列出新后端（无需改 UI）

### 4.3 插件开发模板（第三方视角）

```cmake
qt_add_library(my_translation_plugin SHARED
    myplugin.cpp
)
target_link_libraries(my_translation_plugin PRIVATE Translex_sdk)
```

```cpp
class MyBackend : public ITranslationBackend { /* ... */ };

class MyPlugin : public QObject, public ITranslationPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TranslexPlugin_iid)
    Q_INTERFACES(ITranslationPlugin)
public:
    QStringList backendIds() const override { return { "translation.mybackend" }; }
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id) override {
        return std::make_shared<MyBackend>();
    }
};
```

## 5. 目录约定

```
src/services/
├── iservice.h            # IService 基类（纯虚接口，不继承 QObject）
├── itranslationbackend.h # 翻译后端接口
├── itranslationplugin.h  # L3 插件接口（Q_DECLARE_INTERFACE）
├── serviceregistry.h     # 注册表 + 插件扫描
├── documentmodel.*       # L1 内置服务（懒加载文档模型）
├── securestorage.*
├── appguard.*
├── configservice.*       # 配置服务（VSCode-like：JSON 声明/读写/加密）
├── commentservice.*      # 批注服务（单一数据源，模型 provider 委托）
├── documentmanager.*     # 文档管理（打开/保存，批注持久化）
├── chapterservice.*      # 章节服务（章节索引）
├── findservice.*         # 查找替换服务
├── translationcache.*    # 翻译缓存（L1 内存 + L2 磁盘）
├── translationbackend.*  # 内置翻译后端
├── translationservice.*  # 翻译门面
├── termglossary.*        # 术语表
└── qualitygate.*         # 质量自检
plugins/                  # 第三方插件输出目录（部署时创建）
docs/services/            # 每个服务独立文档
├── documentmodel.md
├── translation-service.md
├── securestorage.md
├── config-service.md
├── comment-service.md
├── document-manager.md
├── chapter-service.md
├── find-service.md
└── plugin-development.md # 第三方插件开发指南
```

## 6. 设计原则

1. **接口稳定**：`IService`/`ITranslationBackend` 定稿后不轻易改动；新增能力用新接口方法（默认实现）
2. **同步核心 + 异步外壳**：后端接口同步，UI 侧用线程池/QtConcurrent 异步调用
3. **可降级**：后端失败可回退（保持现有 Ollama→云端 回退链）
4. **可测试**：每个后端可独立单元测试（mock 网络）
5. **文档与实现同库**：`docs/services/*.md` 与 `src/services/*.h` 一一对应
