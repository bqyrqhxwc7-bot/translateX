# Translex Service 架构与插件规范

> 状态：设计定稿（v1）
> 本文档定义 service 的**提供方式**，是第三方开发者编写插件的依据。

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
│  ├─ register("ollama", Factory)                │
│  └─ create("ollama") → ITranslationBackend     │
├─────────────────────────────────────────────────┤
│  内置服务 (src/services)                        │
│  DocumentModel / SecureStorage / AppGuard       │
│  TranslationService / CommentService / ...      │
├─────────────────────────────────────────────────┤
│  外部插件 (QPluginLoader 动态加载)              │
│  plugins/*.dll → 实现 IServicePlugin 接口        │
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

所有 service 继承 `QObject`，遵循以下约定：

```cpp
class IService : public QObject {
    Q_OBJECT
public:
    // 服务标识（稳定、唯一，如 "translation.ollama"）
    virtual QString serviceId() const = 0;
    // 服务名（显示用）
    virtual QString displayName() const = 0;
    // 服务版本
    virtual QString serviceVersion() const = 0;
    // 健康检查（返回空串=正常，否则返回错误描述）
    virtual QString healthCheck() const { return QString(); }
};
```

### 3.2 翻译后端接口

```cpp
class ITranslationBackend : public QObject {
    Q_OBJECT
public:
    // 后端能力描述
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    // 是否支持上下文翻译
    virtual bool supportsContext() const { return false; }
    // 是否支持流式
    virtual bool supportsStreaming() const { return false; }

    // 单条翻译（同步返回；长耗时调用方负责放入线程池）
    virtual TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) = 0;

    // 批量/上下文翻译（可覆盖默认的逐条循环）
    virtual QList<QPair<int, TranslationResult>> translateBatch(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag);
};
```

### 3.3 注册工厂

```cpp
// 后端工厂：创建指定 ID 的后端实例
using BackendFactory = std::function<std::shared_ptr<ITranslationBackend>()>;

class ServiceRegistry : public QObject {
    Q_OBJECT
public:
    static ServiceRegistry *instance();

    // 注册后端（核心内置 + 第三方插件都走这里）
    void registerBackend(const QString &id, BackendFactory factory, const QString &displayName);

    // 按 ID 创建
    std::shared_ptr<ITranslationBackend> createBackend(const QString &id);

    // 列出可用后端
    QStringList availableBackends() const;
    QString backendDisplayName(const QString &id) const;

    // 插件目录扫描（L3）
    void scanPluginDirectory(const QString &dir);
};
```

## 4. 动态插件机制（L3，供第三方）

### 4.1 插件接口

第三方插件编译为独立 `.dll`，实现：

```cpp
// plugins/plugin_interface.h （随 SDK 发布）
class ITranslationPlugin : public QObject {
    Q_OBJECT
public:
    virtual ~ITranslationPlugin() = default;
    // 返回此插件提供的后端 ID 列表
    virtual QStringList backendIds() const = 0;
    // 创建后端实例
    virtual std::shared_ptr<ITranslationBackend> createBackend(const QString &id) = 0;
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
├── iservice.h            # IService 基类
├── itranslationbackend.h # 翻译后端接口
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
