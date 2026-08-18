# 迭代5：插件化（A3）+ Outlook 式布局 + 文档 + Agent 流程 设计

> 状态：📋 设计中（2026-08-18）
> 范围（用户确认）：
> 1. 插件化做到**最完整（A3）**：iservice.h 落地 + 全部 service 实现健康度 + 注册表重构 + SDK + 示例插件 + 动态发现
> 2. UI：**Outlook 式布局**（最左可收起图标栏 + 中间可拖拽可收起分割侧边栏 + 内容区）、浮窗可调大小、设置页「调试」卡片
> 3. 文档：CONTRIBUTING.md + plugin-development.md + 漂移修复 + SERVICE-ARCHITECTURE 对齐
> 4. Agent 流程：build/plan/explore 专属 prompt + plan 只读 bash 白名单

## 1. 插件化（阶段 1，A3 最完整）

### 1.1 现状脱节（调研结论）

| 文档承诺（SERVICE-ARCHITECTURE.md v1） | 实现现状 |
| --- | --- |
| `iservice.h` 定义 IService | **不存在** |
| `plugin-development.md` 第三方指南 | **不存在** |
| 插件接口 `ITranslationPlugin`（Q_DECLARE_INTERFACE） | serviceregistry.cpp 用「registerTo(ServiceRegistry*)」metaobject 槽约定，**不一致** |
| 注册表管理全部服务 | 只管理翻译后端；main_qml.cpp 硬编码实例化 + setContextProperty |
| 健康检查 healthCheck() | **零实现** |

### 1.2 IService 接口（iservice.h，落地文档 v1 并扩展）

```cpp
class IService {
public:
    virtual ~IService() = default;
    virtual QString serviceId() const = 0;      // 稳定唯一，如 "translation"
    virtual QString displayName() const = 0;    // 显示名
    virtual QString serviceVersion() const = 0; // 版本
    // 健康检查：返回 {status: "ok"/"warn"/"error", message, detail}
    virtual QVariantMap healthCheck() const = 0;
    // 侧边栏面板 QML 组件 URL（可选；空=无面板）——插件 UI 扩展点
    virtual QString sidebarPanel() const { return QString(); }
};
Q_DECLARE_INTERFACE(IService, "org.translex.IService/1.0")
```

- 纯虚接口（不继承 QObject，避免多重继承问题）；service 类 `Q_INTERFACES(IService)` + 注册表 `qobject_cast<IService*>(obj)`
- `healthCheck()` 返回 QVariantMap（比文档的 QString 更结构化，QML 可直接渲染）

### 1.3 各 service 健康度实现（10 个 QObject service）

> 注：SecureStorage/TermGlossary/QualityGate/TranslationCache 是**非 QObject 工具类**
> （静态/值类，被其他 service 持有），不注册为 service——qobject_cast<IService*> 需要 QObject。

| service | healthCheck 内容 |
| --- | --- |
| DocumentModel | ok（模型可用） |
| TranslationService | 当前后端配置完整（backendId 非空 + 配置项齐全） |
| CommentService | ok |
| DocumentManager | ok |
| ChapterService | ok |
| FindService | ok |
| ConfigService | schema 加载成功（sections 非空） |
| TextToSpeechService | 引擎可用性（available） |
| TranslationHistoryService | ok |
| AppGuard | 日志文件可写 |

### 1.4 ServiceRegistry 扩展

- `registerService(QObject*)`：qobject_cast<IService*> 注册（非仅后端）
- `services()` / `serviceById(id)` / `healthReport()`（QVariantList 聚合全部健康度）
- 插件接口统一为文档的 `ITranslationPlugin`（Q_DECLARE_INTERFACE + Q_PLUGIN_METADATA），删除 registerTo 槽约定
- `scanPluginDirectory(dir)`：QPluginLoader 扫描 → instance() → 注册后端 + 服务

### 1.5 Translex_sdk CMake 目标

- 导出公共头：`iservice.h` / `itranslationbackend.h` / `serviceregistry.h` / `translationtypes.h`
- 供示例插件与第三方链接

### 1.6 main_qml.cpp 重构

- service 创建后 `registry->registerService(...)` + `setContextProperty`（**名字不变，QML 零改动**）
- 启动时 `scanPluginDirectory(exe_dir/plugins)`；插件加载错误记录到 `loadedPluginErrors()`
- 设置页后端下拉自动列出插件后端（现有 availableBackends 机制已支持）

### 1.7 示例插件 plugins/

- `plugins/example_translation_plugin/`：自定义后端（如 "translation.example" 回显后端）+ 侧边栏面板演示
- 验证全链路：编译 → 部署 → 扫描 → 注册 → 设置页可见 → 可翻译

### 1.8 测试 tst_registry

- 注册/按 ID 查询/健康度聚合/插件加载（mock 插件 dll 或跳过）/重复注册覆盖

## 2. UI 改造（阶段 2）

### 2.1 布局（用户确认：Outlook 式）

```
┌───┬──────────────┬──────────────────────────┐
│图标栏│ 侧边栏(SplitView) │ 内容区(Loader)        │
│44px│ 可拖拽 180-400px │ 编辑页 / 设置页         │
│编辑│ 章节|批注|历史|术语 │ (NoStack 切换重建)     │
│设置│ tab 切换         │                        │
│可收起│ 可收起           │                        │
└───┴──────────────┴──────────────────────────┘
```

- **图标栏**（最左，Outlook 式）：44px 垂直图标（编辑/设置页面切换），当前页主题色指示条，顶部按钮可收起/展开
- **侧边栏**（中间）：`FluSplitLayout` 拖拽分割，tab 切换「章节/批注/翻译历史/术语表」，宽度持久化（`ui.leftPanelWidth` 进 ui.json schema），可收起/展开
- **内容区**：Loader 加载页面（NoStack 语义，与现状一致）
- **导航重构**：Main.qml 放弃 FluNavigationView，改自定义（页面仅 2 个，可控）
- **Ribbon 保留**：编辑页 Ribbon 不动，侧边栏是新增常驻导航面板

### 2.2 浮窗 resize

- 自绘右下角手柄 + `startSystemResize` + 最小尺寸（240×160）
- `TranslatePanelContent` 响应式（宽度变化时布局自适应）

### 2.3 设置页「调试」卡片

- service 健康度列表（healthReport 渲染，状态色点 + 消息）
- 后端连接测试按钮（复用 testBackendConnection）
- 插件加载错误列表（loadedPluginErrors）
- 配置/日志路径显示

## 3. 文档（阶段 3）

- `CONTRIBUTING.md`：贡献指南（构建/测试/文档先行/中文交流）
- `docs/services/plugin-development.md`：第三方插件开发指南（对齐实现）
- 漂移修复：README/ARCHITECTURE 测试目标数 13→15、功能清单补迭代4/4b
- SERVICE-ARCHITECTURE.md 与实现对齐（iservice.h 落地后）

## 4. Agent 流程（阶段 4）

- `.opencode/prompts/build.md` / `plan.md` / `explore.md` 专属 prompt
- opencode.json 引用 + plan 加只读 bash 白名单（可跑构建/测试验证）
- AGENTS.md 分工速查表

## 5. 测试与验收

- 每阶段独立提交；MSVC + clang 各 15/15 全绿（阶段1 后 16/16，新增 tst_registry）
- 冒烟：启动无崩溃、设置页后端下拉含插件后端（阶段1 后）
- qmllint 干净（仅既有 FluentUI 类型警告）

## 6. 限制（明示）

- 插件仅支持翻译后端 + 侧边栏面板（UI 扩展点最小集，后续可扩）
- 插件需与主程序同 Qt 版本/编译器（MSVC 静态 Qt，动态加载限制）
- 侧边栏宽度持久化仅存比例/像素值，不存布局树