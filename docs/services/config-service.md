# ConfigService 配置服务设计文档

> 状态：v0.3（核心 + QML schema 驱动设置页均已完成）
> 目标：**把设置/配置暴露给 service 提供者，实现类似 VSCode 插件的效果**（仅效果相似，不涉及技术栈）

## 1. 定位与目标

现状：配置是**写死**的——`TranslationService::setBackendConfig(QVariantMap)` 手传配置，设置页 UI 手写，新增后端要改核心代码 + 改设置页。

目标（对齐 VSCode 插件模型）：

| VSCode 概念 | 本方案 |
| --- | --- |
| `package.json` 的 `contributes.configuration` | 服务/插件自带的 `config.json`（声明配置项） |
| `workspace.getConfiguration("sec").get("key")` | `ConfigService::get(section, key)` / `values(section)` |
| `workspace.getConfiguration().update(...)` | `ConfigService::set(section, key, value)` |
| `onDidChangeConfiguration` | `ConfigService::configChanged(section, key, value)` 信号 |
| 设置面板自动生成 | QML 设置页遍历 schema 自动生成控件 |

收益：
- **第三方插件零 UI 改动**：插件只需带一个 `config.json`，设置页自动出现其配置项
- **核心零改动**：新增配置项 = 改 JSON，无需重编译核心
- **统一读写/持久化/加密**：secret 自动走 SecureStorage，其余走 QSettings

## 2. 配置声明（外部 JSON）

### 2.1 文件位置与加载

- **内置服务**：`src/services/config/*.json`（编译进 qrc 或作为资源）
- **第三方插件**：`<exe_dir>/plugins/<name>/config.json`，核心扫描 `plugins/` 目录加载
- 扫描时机：应用启动时 `ConfigService::scanConfigDirectory()`；热加载（后续可选）

### 2.2 JSON 格式

```json
{
  "id": "translation.network_model",
  "displayName": "网络大模型 API",
  "settings": [
    {
      "key": "apiEndpoint",
      "displayName": "接口地址",
      "description": "OpenAI 兼容的 /chat/completions 地址",
      "type": "string",
      "default": "https://api.deepseek.com/v1",
      "group": "连接"
    },
    {
      "key": "apiKey",
      "displayName": "API Key",
      "description": "访问密钥（加密存储）",
      "type": "secret",
      "default": "",
      "group": "连接"
    },
    {
      "key": "model",
      "displayName": "模型",
      "type": "string",
      "default": "deepseek-chat",
      "group": "模型"
    }
  ]
}
```

### 2.3 配置项类型

| `type` | 生成控件 | 持久化 | 说明 |
| --- | --- | --- | --- |
| `string` | FluTextBox | QSettings | 单行文本 |
| `multiline` | FluMultiLineTextBox | QSettings | 多行（提示词等） |
| `number` | FluSpinBox | QSettings | 数字 |
| `bool` | FluToggleSwitch | QSettings | 开关 |
| `enum` | FluComboBox | QSettings | `options` 固定选项 |
| `secret` | FluTextBox(Password) | SecureStorage | 加密存储 |
| `path` | FluTextBox + 浏览 | QSettings | 目录/文件路径 |

### 2.4 可选字段

- `group`：设置页内分组标题
- `options`：enum 的固定选项 `["a","b"]`（或 `[{value,label}]`）
- `min`/`max`：number 的上下限（对齐 FluSpinBox）
- `step`：number 步长
- `restartRequired`：true 时设置页提示"重启后生效"
- `placeholder`：输入框占位

## 3. 核心组件：ConfigService（单例）

### 3.1 C++ 结构

```cpp
// 配置项（对应 config.json 的一条 settings）
struct ConfigItem {
    QString key;            // 如 "apiEndpoint"
    QString displayName;    // 显示名
    QString description;    // 帮助文案
    QString type;           // string|multiline|number|bool|enum|secret|path
    QVariant defaultValue;
    QStringList options;    // enum 选项
    double min = 0, max = 0, step = 1;   // number
    QString group;          // 分组标题
    bool restartRequired = false;
    QString placeholder;
};

// 配置段（对应一个 config.json）
struct ConfigSection {
    QString id;             // 如 "translation.network_model"
    QString displayName;
    QList<ConfigItem> items;
};
```

```cpp
class ConfigService : public QObject {
    Q_OBJECT
public:
    static ConfigService *instance();

    // ---- 声明加载 ----
    void loadBuiltinConfigs();                     // 加载内置 config.json（qrc）
    void scanConfigDirectory(const QString &dir);  // 扫描插件 config.json

    // ---- schema 查询（QML 设置页用）----
    Q_INVOKABLE QStringList sections() const;               // 所有 section id
    Q_INVOKABLE QString sectionDisplayName(const QString &id) const;
    Q_INVOKABLE QVariantList sectionItems(const QString &id) const; // → QML model

    // ---- 读写（VSCode getConfiguration 语义）----
    Q_INVOKABLE QVariant get(const QString &section, const QString &key) const;
    Q_INVOKABLE void set(const QString &section, const QString &key, const QVariant &value);
    QVariantMap values(const QString &section) const;  // 全部值（默认值填充）

    // 便捷：值是否用户显式设置过（区分"用了默认"和"用户改过"）
    bool isUserSet(const QString &section, const QString &key) const;

signals:
    void configChanged(const QString &section, const QString &key, const QVariant &value);
    void sectionsChanged();   // schema 变化（扫描插件后）
};
```

### 3.2 读取优先级

`用户显式值（QSettings/SecureStorage） > JSON 默认值 > 类型默认值`

### 3.3 持久化

- 非敏感：`QSettings`（INI，`%APPDATA%/sr291/Translex/config.ini`），键 = `section/key`
- `secret`：`SecureStorage`（复用现有机器指纹加密），键 = `section/key`
- 删除：`set(section, key, undefined)` 恢复默认（可选）

## 4. 服务提供者接入

### 4.1 启动时读取 + 监听变化（服务侧）

```cpp
// 内置后端（以 OllamaBackend 为例）
void OllamaBackend::initFromConfig()
{
    const QVariantMap cfg = ConfigService::instance()->values("translation.ollama");
    m_endpoint = cfg.value("endpoint").toString();
    m_model = cfg.value("model").toString();
}

void OllamaBackend::onConfigChanged(const QString &key, const QVariant &value)
{
    if (key == "endpoint") m_endpoint = value.toString();
    else if (key == "model") m_model = value.toString();
}
```

### 4.2 对现有 `TranslationService` 的兼容策略

- `TranslationService` 注册内置 section：`translation`（翻译选项）+ `translation.ollama` / `translation.network_model`（后端参数）
- 现有 `setBackendConfig(QVariantMap)`、`setContextRadius()` 等 setter **保留**（QML 兼容），内部改为读写 ConfigService，避免破坏已接线代码
- 后端创建时：`currentBackend()` → 用 `ConfigService::values(backendId 对应 section)` 调 `updateConfig()`，替代手写 QVariantMap
- `setBackend(backendId)` 时把选择持久化到 `translation/backend` 配置项

### 4.3 第三方插件接入流程

1. 插件目录放 `config.json`（声明配置项）
2. 核心 `scanConfigDirectory()` 加载 → schema 进入 ConfigService
3. QML 设置页**自动出现**该插件配置区（零 UI 改动）
4. 插件实现类在构造时 `ConfigService::instance()->values(自己的 section)` 读配置，并连接 `configChanged` 监听

## 5. QML 设置页（schema 驱动）

### 5.1 结构

```
TranslateSettingsPage
├── 翻译设置（手写保留：后端选择下拉 + 术语表编辑）   ← 特殊交互，保留手写
├── 由 schema 生成的各 section 分组                  ← 新增
│   └── Repeater 遍历 sectionItems(id)
│       ├── string   → FluTextBox
│       ├── number   → FluSpinBox
│       ├── bool     → FluToggleSwitch
│       ├── enum     → FluComboBox
│       ├── secret   → FluTextBox(Password)
│       └── path     → FluTextBox + 浏览按钮
└── 配置变化 → ConfigService.set(section, key, value)
```

### 5.2 组件：`ConfigSectionCard`

封装一个 section 的自动渲染（标题 + 分组 + 各项控件），供设置页 `Repeater` 使用。

### 5.3 术语表如何处理

术语表是**数据编辑**（增删条目）而非简单配置项，设置页保留手写 UI，内容持久化到 `translation/glossary`（JSON 字符串存 QSettings，或独立文件）。

## 6. 内置配置迁移清单

| section | 配置项 | 现状来源 |
| --- | --- | --- |
| `translation` | backend、contextRadius、sourceLang、targetLang、strictOutput、cacheEnabled、fallbackEnabled、smartChunking、sentenceAwareChunking、maxChunkChars、qualityGateEnabled、enableCustomPrompt、customPrompt、customContextPrompt、glossary、docxCommentStyle | `src/services/config/translation.json`（原有手写 setter 已改为读写 ConfigService） |
| `translation.ollama` | endpoint、model | `m_backendConfig` 手写 |
| `translation.network_model` | apiEndpoint、apiKey(secret)、model | `m_backendConfig` 手写 |

## 7. 测试计划

- `tst_configservice`（9 用例）：加载内置 JSON → schema 完整（builtinSections）；默认值回退（defaultValues）；get/set 持久化往返（setGetRoundTrip）；configChanged 信号；values 填充默认；secret 走 SecureStorage 不可明文（secretEncrypted）；isUserSet；number/bool 类型规范化（numberBoolNormalize）；插件扫描（scanPluginDirectory）
- 现有 `tst_translation` / `tst_quality` 回归（兼容策略保证不破坏）

## 8. 分步实施

1. ~~文档确认~~ ✅
2. `ConfigItem/ConfigSection` 结构 + `ConfigService` 核心（加载/读写/信号/持久化） ✅ `src/services/configservice.*`
3. 内置 `config.json`（translation + ollama + network_model）+ qrc 注册 ✅ `src/services/config/*.json` + `config.qrc`
4. `TranslationService` 接入（setter 内部改走 ConfigService，后端创建读配置，术语表持久化） ✅
5. QML：`ConfigSectionCard` + 设置页 schema 渲染 + secret 控件 ✅ `qml/ConfigSectionCard.qml`
6. 测试 `tst_configservice`（9 用例）+ 回归 ✅ 9/9 通过
7. 文档收尾（更新 translation-service.md / SERVICE-ARCHITECTURE.md / plugin-development 占位） ✅

## 9. 关键决策记录

- 配置声明：**外部 JSON**（用户已确认）
- 敏感项：**SecureStorage 加密**（用户已确认；secret 值经 `SecureStorage::encrypt` 加密后 base64 存入 `config.ini`，与普通项同文件、自包含可测试隔离）
- 设置 UI：**全自动 schema 驱动**（用户确认；术语表等特殊交互保留手写）
- 兼容：`TranslationService` 现有 setter 保留（写回 ConfigService），避免破坏已接线 QML

## 10. 当前实现要点

- `ConfigService::instance()` 首次调用即加载内置 `:/config/*.json`
- 存储：`%APPDATA%/<org>/<app>/config.ini`（QSettings IniFormat）；`setDataDirectoryForTest()` 可覆盖（测试隔离）
- `get/set/values/isUserSet`：默认值回退、number/bool 类型规范化、`set(无效值)` 恢复默认
- `configChanged(section, key, value)` 信号：服务提供者监听自身 section 应用变化
- `scanConfigDirectory(dir)`：支持 `<dir>/config.json` 与 `<dir>/<plugin>/config.json` 两种布局，加载后发 `sectionsChanged`
- `TranslationService`：构造时从 `translation` section 恢复全部开关/术语表；setter 写回；`currentBackend()` 用 `values(backendId)` 填充后端配置（`m_backendConfig` 仍可运行时覆盖）

## 11. QML schema 驱动设置页

- `main_qml.cpp` 将 `ConfigService::instance()` 暴露为 context property `configService`
- `qml/ConfigSectionCard.qml`：遍历 `configService.sectionItems(sectionId)` 自动生成控件
  - `string`→FluTextBox、`number`→FluSpinBox、`bool`→FluToggleSwitch、`enum`→FluComboBox、`secret`→FluTextBox(Password)、`multiline`→FluMultilineTextBox
  - `excludeKeys` 排除特殊项（backend/glossary）；enum 无 options 自动跳过
  - 顶部 `pragma ComponentBehavior: Bound`（嵌套组件访问外层 id 安全）
- `TranslateSettingsPage.qml`：手写翻译选项区（7 个控件）替换为 `ConfigSectionCard`（translation section，排除 backend/glossary）；后端参数区随 `backendSection` 动态渲染（切换后端即时变化）
- 后端参数热更新天然生效：`currentBackend()` 每次创建实例都从 `ConfigService::values()` 读最新配置
- 术语表保留手写 UI（数据编辑型交互），持久化到 `translation/glossary`
