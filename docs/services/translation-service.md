# TranslationService 设计文档

> 状态：v0.2（阶段 A/B/C 已实现；模型分级路由决策：暂不实现，保持手动选后端）
> 定位不变：**更好的翻译质量 + 为用户减少成本**

## 1. 定位

从旧版（`mainwindow.cpp` 内硬编码的 Ollama/云端/网络大模型请求）重构为**可插拔翻译服务**，围绕两个核心目标：

- **质量**：上下文感知、术语一致性、质量自检
- **成本**：缓存复用、模型分级、智能分块

## 2. 架构

```
┌──────────────────────────────────────────────┐
│          TranslationService (门面)           │
│  - 选择后端（用户配置 or 自动）              │
│  - 质量策略（上下文/术语/自检）              │
│  - 成本策略（缓存/分块）                     │
├──────────────────────────────────────────────┤
│  ITranslationBackend (可插拔)                │
│  ├─ OllamaBackend       (本地，低成本)       │
│  ├─ OnlineBackend       (内置云端，免费)     │
│  ├─ NetworkModelBackend (DeepSeek/OpenAI)    │
│  └─ 第三方插件 (QPluginLoader)               │
├──────────────────────────────────────────────┤
│  TranslationCache (缓存：L1 内存 + L2 磁盘)  │
│  TermGlossary (术语表)                       │
│  QualityGate (质量自检)                      │
└──────────────────────────────────────────────┘
```

## 3. 成本优化策略（核心目标之一）

### 3.1 翻译缓存（`TranslationCache`）✅ 已实现
- **键**：`sha256(原文 + 严格输出 + 温度 + 上下文 + 模型/端点 + 术语约束)`（`TranslationCache::key`）
- **分级**：
  - L1 内存缓存（会话内，`QHash`，LRU 淘汰，上限 5000 条）
  - L2 磁盘缓存（`%APPDATA%/sr291/Translex/cache/<2位前缀>/<key>.json`，跨会话复用）
- **收益**：重复翻译/回译/重新打开文档时**零成本**
- **注意**：术语表变化会改变缓存键，避免旧译文复用（已含 `glossaryConstraint` 指纹）

### 3.2 模型分级路由 ❌ 暂不实现（决策：保持手动选后端）
按文本特征自动切换后端的方案已设计（见下表），但**决定暂不实现**——后端选择保持手动，避免自动切换引入不可预期的成本/质量行为。若未来需要，可在此方案基础上实现：

| 场景 | 推荐 | 理由 |
| --- | --- | --- |
| 单行/短句 | 本地 Ollama 或免费云端 | 快、低成本 |
| 长段落/术语密集 | 网络大模型（DeepSeek v3.2） | 质量优先 |
| 重复内容 | 缓存直接命中 | 零成本 |

### 3.3 智能分块 ✅ 已实现
- `TranslationService::buildChunks`：按 **token 预算**（`m_maxChunkChars`）合并**行号连续**的相邻行
- **句边界分块**（`sentenceAwareChunking`，默认开）：当前块末行以句末标点结尾（`。！？…；.!?;`，中文标点按 Unicode 码点比较）即截断，避免跨句合并稀释上下文质量；`setSentenceAwareChunking` 可关
- `translateBatchSync` 逐块调用 `backend->translateBatch(...)`；后端可覆盖 `translateBatch` 合并多行为一次请求，真正降请求数
- 未覆盖 `translateBatch` 的后端按默认逐条循环，无回归
- 行序保持、缓存命中行跳过、失败单行重试 + 降级链

### 3.4 失败降级链 ✅ 已实现
`网络大模型 → 免费云端`（`fallbackBackend`）自动降级，**不因单点失败浪费已完成成本**。

## 4. 质量优化策略（核心目标之二）

### 4.1 上下文翻译 ✅ 已实现
- `buildOptions` 参考行前后 N 行（`m_contextRadius`，可配置）
- 后端 `buildPrompt` 支持上下文提示模板（`customContextPrompt`）

### 4.2 术语一致性 ✅ 已实现
- `TermGlossary`：用户可配置 `term → translation` 映射（`setTerm/loadFromMap/toMap`）
- 翻译前：`buildOptions` 注入 `glossaryConstraint`，后端 `buildPrompt` 追加到提示词
- 翻译后：`TermGlossary::verify` 计算术语命中率，`missingTerms` 列出未命中项

### 4.3 质量自检（`QualityGate`）✅ 已实现
`TranslationService::postProcess` 对每条结果执行规则校验（不通过不阻塞，仅发 `qualityWarning` 提示人工复核）：
- **纯回显检测**：译文与原文高度相似视为未翻译
- **长度校验**：译文长度在原文 0.2x ~ 4x 内（中文更紧凑，阈值放宽）
- **Token 保留**：数字/版本号/占位符/代码标识符（驼峰、下划线、含数字、全大写）至少保留 80%；普通英文单词不视为 token，避免英→中误报
- **术语一致性**：术语命中率计入综合分
- 综合分 ≥ 0.6 通过

### 4.4 严格输出 ✅ 已实现
- `m_strictOutput`：仅输出译文，减少模型废话浪费 token

### 4.5 质量自检复核面板 ✅ 已实现（2026-08-17）
- `qualityWarning(lineNumber, issue)` 逐条发信号；主页收集到 `qualityWarnings` ListModel，`onBatchFinished` 有告警时弹出**复核面板**（行号+问题+跳转按钮，`focusLine` 定位复核），可清除列表
- 翻译开始时自动清空上一批告警

### 4.6 后端连接测试 ✅ 已实现（2026-08-17）
- `testBackendConnection(backendId)`：异步（QtConcurrent + QFutureWatcher）创建后端实例，`healthCheck()` 非空即成功；否则最小翻译探测（`translate("hello")`，8s 超时）
- 结果经 `connectionTested(backendId, ok, message)` 信号回传；设置页「测试连接」按钮展示（成功绿/失败红）
- **踩坑（2026-08-17 修复）**：①探测必须合并 `ConfigService::values(backendId)` 用户配置（`m_backendConfig` 仅为运行时覆盖，正常为空）；②`NetworkModelBackend` 未重写 `updateConfig`（基类默认空实现）→ 合并结果必须放 `TranslationOptions.extra`（`apiEndpoint/apiKey/model` 从 extra 读）——修复后 `connectionTestUsesConfigServiceSection` 回归测试覆盖

## 5. 接口（对齐 SERVICE-ARCHITECTURE.md）

> 配置管理：所有开关/参数经 `ConfigService`（VSCode-like 配置服务）持久化与通知。
> 以下 setter 均写回 `translation` section；后端创建时由 `ConfigService::values(backendId)` 提供参数。

```cpp
class TranslationService : public QObject {
    Q_OBJECT
public:
    // 选择后端
    Q_INVOKABLE void setBackend(const QString &backendId);
    Q_INVOKABLE QString backend() const;
    Q_INVOKABLE QStringList availableBackends() const;
    Q_INVOKABLE void setBackendConfig(const QVariantMap &config);

    // 翻译选项
    Q_INVOKABLE void setContextRadius(int radius);
    Q_INVOKABLE void setStrictOutput(bool strict);
    Q_INVOKABLE void setCacheEnabled(bool enabled);
    Q_INVOKABLE void setFallbackEnabled(bool enabled);
    Q_INVOKABLE void setTimeoutMs(int ms);

    // 质量：术语表 + 自检开关
    Q_INVOKABLE void setGlossary(const QVariantMap &terms);
    Q_INVOKABLE void clearGlossary();
    Q_INVOKABLE QVariantMap glossary() const;
    Q_INVOKABLE void setQualityGateEnabled(bool enabled);

    // 成本：智能分块开关 + 预算
    Q_INVOKABLE void setSmartChunkingEnabled(bool enabled);
    Q_INVOKABLE void setMaxChunkChars(int chars);

    // 翻译入口（QML 调用，内部异步）
    Q_INVOKABLE void translateLines(const QList<int> &lineNumbers, const QStringList &sourceLines);

    // 进度 / 取消
    Q_INVOKABLE void cancelTranslation();        // 取消当前批量翻译（后端请求同步中止）
    Q_INVOKABLE bool translationActive() const;  // 是否正在翻译

    // 目标语言预检测（跳过已是目标语言的行，避免中文→中文回显误报）
    Q_INVOKABLE bool isTargetLanguageText(const QString &text) const;

    // 同步辅助（测试/内部）
    TranslationResult translateSync(const QString &text);
    QList<QPair<int, TranslationResult>> translateBatchSync(
        const QStringList &sourceLines, const QList<int> &targetLines);

signals:
    void lineTranslated(int lineNumber, const QString &text, bool success);
    void batchFinished(int total, int succeeded, int failed);
    void backendChanged(const QString &backendId);
    void qualityWarning(int lineNumber, const QString &issue);
    // ---- 进度 / 取消 ----
    void translationStarted(int total);
    void translationProgress(int done, int total);
    void translationCanceled();
};
```

## 6. 后端接口（复用规范）

```cpp
struct TranslationOptions {
    QString sourceLang = "en";
    QString targetLang = "zh-CN";
    QStringList contextLines;   // 上下文
    bool strictOutput = true;
    double temperature = 0.2;
    int timeoutMs = 0;
    QVariantMap extra;          // 后端特定参数（model/customPrompt/glossaryConstraint 等）
    // 便捷方法：model()/customPrompt()/customContextPrompt()/apiEndpoint()/apiKey()...
};

struct TranslationResult {
    QString text;
    bool success = false;
    QString errorMessage;
    qint64 elapsedMs = 0;
    bool fromCache = false;     // 是否缓存命中
};

class ITranslationBackend : public QObject {
public:
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool supportsContext() const;
    virtual bool supportsStreaming() const;
    virtual TranslationResult translate(text, options, cancelFlag) = 0;
    // 批量翻译：默认逐条循环；后端可覆盖做合并/流式优化（智能分块的降本关键）
    virtual QList<QPair<int, TranslationResult>> translateBatch(
        sourceLines, targetLines, options, cancelFlag);
    virtual QString healthCheck() const;
    virtual void updateConfig(const QVariantMap &config);
};
```

## 7. 实现状态

### 阶段 A（功能骨架）✅
1. `ITranslationBackend` 接口 + `TranslationOptions/Result`
2. `TranslationService` 门面（选择后端 + 同步转异步）
3. 迁移现有 Ollama/云端/网络后端为独立类
4. 基础缓存（内存 L1）

### 阶段 B（质量增强）✅
5. 上下文翻译接入新接口
6. 术语表 + 一致性校验（`TermGlossary`）
7. 质量自检 `QualityGate`（回显/长度/Token 保留/术语）

### 阶段 C（成本深化）✅
8. 磁盘缓存 L2 + LRU ✅
9. 智能分块（token 预估 + 行合并 + `translateBatch`）✅
10. 模型分级路由 ❌ 决策：暂不实现，保持手动选后端

### 阶段 D（插件化）✅ 已实现（迭代5）
11. `QPluginLoader` 扫描 + 插件 SDK 头文件 ✅
12. 插件开发指南文档 ✅（`docs/services/plugin-development.md`）

## 8. 度量（验收指标）

| 指标 | 目标 | 状态 |
| --- | --- | --- |
| 缓存命中率 | ≥ 30%（重复文档场景） | 待验证 |
| 大文件翻译成本 | 较旧版降低 ≥ 40%（分块+缓存） | 待验证 |
| 翻译质量 | 术语一致率 ≥ 95%（有术语表时） | 待验证 |
| 后端接入成本 | 新后端 ≤ 1 天（实现接口+注册） | 架构已支持 |

## 9. UI 翻译入口（编辑器页）

编辑器提供四种翻译入口，全部走 `translateLines`（异步、可取消、带进度）：

| 入口 | 说明 |
| --- | --- |
| 翻译当前行 | 翻译当前高亮行（跳过空行/已是目标语言的行） |
| 翻译全部待译行 | 所有**非空、无批注、非目标语言**的行（支持多语言文档） |
| 翻译选中行 | **Ctrl+点击**多选若干行后翻译（跳过空行/目标语言行）；再 Ctrl+点击取消选择 |
| 进度 / 取消 | 翻译中状态栏显示 `X/Y` + 旋转环 + **取消**按钮；`cancelTranslation()` 中止请求 |

行为约定：
- **跳过目标语言行**：`TranslationService::isTargetLanguageText()` 判定 CJK 占比 ≥ 30%（目标为中文时），避免"中文→中文"回显被误报为"疑似未翻译"。
- **批量合并**：`NetworkModelBackend::translateBatch` 一次请求多行（JSON 数组输出），失败行自动逐行兜底；DeepSeek 自动关闭 thinking mode 加速。
- **回显拦截**：`postProcess` 始终执行回显检测（不受质量自检开关影响），原文永远不会被当作译文写入。

工具栏布局（参考旧版 QtWidgets：文档管理与翻译分离，翻译面板浮动）：
- **菜单栏**（FluMenuBar）：
  - 文件：新建 / 打开 / 保存 / 另存为 / 加载示例 / 清除译文
  - 编辑：撤销 / 重做 / 翻译当前行 / 翻译全部待译行 / 翻译选中行
  - 视图：**翻译面板**（打开/关闭浮动面板）
- **文档工具栏**（精简，编辑器上方）：新建 / 打开 / 最近 / 保存 | 撤销 / 重做 | 共 N 行
- **浮动翻译面板**（`Popup`，对应旧版 `translationDock` 的浮动形态）：
  - **不占用布局空间**：编辑器始终占满内容区
  - 视图菜单「翻译面板」打开；**标题栏可拖拽到任意位置**；✕ 或视图菜单关闭
  - 内容：翻译当前行 / 所选行（主按钮）、翻译全部待译行、翻译选中行、进度条 + X/Y + 取消、快捷键说明
