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
- **单块预算（2026-08-19 调整）**：`maxChunkChars` 默认 **6000**（原 14000，schema 调整，减小单块请求耗时，慢速大模型下更早出结果）；批量请求超时 **90s → 60s**（`NetworkModelBackend::translateBatch` 走 `requestChat`，`timeoutMs` 上限 60s）

### 3.4 失败降级链 ✅ 已实现
`网络大模型 → 免费云端`（`fallbackBackend`）自动降级，**不因单点失败浪费已完成成本**。
- **降级链错误透出（2026-08-19）**：`translateSync`/`translateBatchSync` 记录主后端失败原因（模型名错误/网络错误），最终仍失败（含回显拦截）时 `errorMessage` = 主错误 +「（降级后也未成功）」——否则降级后云端返回原文被回显拦截时，用户只见「与原文相同」而掩盖真实的模型名/API 配置错误
- **后端内逐行兜底已删除（2026-08-19）**：`NetworkModelBackend::translateBatch` 批量请求失败后**不再逐行兜底**（`ITranslationBackend::translateBatch` 默认实现 = 逐行 translate）。原兜底不受 `fallbackEnabled` 开关控制，且与 `translateBatchSync` 上层重试**叠加成双重重试**——批量超时后每行再等 30-60 秒，15 行卡 7 分钟以上，用户看到「翻译卡住不动」「关了降级还在重试」。修复：批量失败直接返回失败结果，由 `translateBatchSync` 统一处理
- **批量兜底语义（2026-08-19 重构）**：批量「**部分成功**」→ 仅重试失败行（单行失败逐行重试合理）；批量「**全失败**」（超时/整体慢/配置错误）→ **不逐行重试**主后端，只降级一次（受 `fallbackEnabled` 控制）；`fallbackEnabled=false` 时全失败**快速失败**（不再等待逐行重试）

## 4. 质量优化策略（核心目标之二）

### 4.1 上下文翻译 ✅ 已实现
- `buildOptions` 参考行前后 N 行（`m_contextRadius`，可配置）
- 后端 `buildPrompt` 支持上下文提示模板（`customContextPrompt`）

### 4.2 术语一致性 ✅ 已实现
- `TermGlossary`：用户可配置 `term → translation` 映射（`setTerm/loadFromMap/toMap`）
- 翻译前：`buildOptions` 注入 `glossaryConstraint`，后端 `buildPrompt` 追加到提示词
- 翻译后：`TermGlossary::verify` 计算术语命中率，`missingTerms` 列出未命中项

### 4.2.1 术语自动提取 ✅ 已实现（2026-08-19 增强）

`TermGlossary::extractCandidates(lines, minFreq = 3, maxCount = 20)` → `QList<QPair<QString,int>>`（词 → 频次，频率降序、同频按字母序稳定）。**maxCount 语义（2026-08-19 放宽，用户要求「达标都可候选」）**：`-1` = 不限（达标都返回），`0` 仍兼容旧语义为 1；主页/设置页调用统一改 `extractTermCandidates(lines, 3, -1)`。初始版只提英文单词，2026-08-19 增强为三类候选：

- **英文/标识符词形**：正则 `[A-Za-z0-9+#-]{2,}`，覆盖技术文档标识符（API/C++/P2899R1/x86-64）；**必须含字母**（纯数字/纯符号过滤）；2 字母词若非停用词也保留；停用词（内置 ~60 词表）/已收录术语过滤（大小写不敏感）
- **中文 n-gram**：连续 CJK 段（`[\x{4e00}-\x{9fff}]`）内取 **2-3 字滑窗**，频率 ≥ `minFreq` 才入选；n-gram 含中文虚词字符（的了是在和与及或为有不这那之以而中也上下都吧吗呢着过把被从对至于个就才便再又）则跳过（「的翻译」「里面」等噪音）
- 返回原文中**最高频的实际书写形式**（术语表校验是大小写敏感的）

**踩坑（2026-08-19）**：
- 字符类**不能含 `.`**——句点应作分隔符，否则 `client.` 与 `client` 词频分裂
- `QRegularExpression` 中文码点范围用 `\x{4e00}` 语法，`\u{4e00}` 不被支持
- **FluContentDialog 弹窗崩坏（2026-08-19 修复）**：`contentDelegate` 宽度 420 > `FluContentDialog` `implicitWidth` 400 → 内容溢出被裁剪崩坏（提取术语弹窗页面显示异常）；修复 = 宽度改 **380**（与设置页 `extractDialog` 一致）

**UI 入口**（术语表为 `TranslationService` 应用级单例，主页/设置页共享同一数据源，经 `extractTermCandidates` + `glossary()/setGlossary` 读写）：
- **设置页术语表卡片（行内译文编辑，2026-08-19）**：列表 delegate 为「原文 → `FluTextBox` 译文（`onEditingFinished` 保存）→ 删除」；`updateTermTranslation` 失焦保存——自动提取的占位项在此补填标准译文
- **主页翻译 Ribbon「提取术语」按钮（2026-08-19）**：`FluentIcons.DictionaryAdd`，受限模式禁用 → `termExtractDialog` 弹窗（页面级 `termCandidateModel`：勾选 + 词频显示 + 行内译文输入 + 全选 + 添加选中）
- **设置页「从文档提取」弹窗升级（2026-08-19，与主页一致）**：`extractDialog` 候选行加译文输入框（FluTextBox，`onEditingFinished` 写回 model，空 = 占位）+ 底部「AI 建议译文」按钮（enabled 绑定 `termSuggestionAvailable()`）；`addExtractedTerms` 用输入框译文（空 = 占位）
- 受限模式（大文件）禁用提取
- **空译文 = 占位**（不注入提示词、不参与质量校验，避免「译文=原文」污染自检）语义不变

**AI 建议译文（2026-08-19，网络大模型后端专用）**：术语候选按文档上下文猜测标准译文，减少手动补填成本（成本策略延续「为用户减成本」定位）。
- C++ 侧（`TranslationService`）：
  - `Q_INVOKABLE void suggestTermTranslations(QStringList terms, QStringList contextLines)`：异步（`QtConcurrent::run` + `QFutureWatcher`），结果经新信号 `termSuggestionsReady(QVariantMap suggestions, bool ok, QString errorMessage)` 返回；terms 为空直接发错误信号
  - `Q_INVOKABLE bool termSuggestionAvailable() const`：仅当前后端为 `translation.network_model` **且**已配置 `apiEndpoint`/`apiKey` 时 true（本地 Ollama / 云端在线 / echo 不支持）——QML 按钮 enabled 绑定它
  - 提示词：要求模型「根据文档上下文给出每个术语的目标语言标准译文，逐行 `术语 = 译文`，不解释、不要序号」；上下文 = 每个术语在文档中的 1 条出现行（最多 30 条，截断 3000 字符）；目标语言显示名映射（zh→中文、en→英文、ja→日文…）
  - 异步线程内**重新创建后端实例**（`ServiceRegistry::createBackend`）+ 合并 `ConfigService::values` 与 `m_backendConfig` 快照，不读 this 成员，规避竞态
  - 静态 `parseTermSuggestions(text, terms)`：解析模型输出，容忍分隔符 `= / ：/ : / → / =>`、行首序号（`1. / 1) / 1、`）、英文/中文弯引号/直角引号包裹、大小写差异；**自译（key==value，模型回显）跳过**；键与原术语大小写不敏感匹配，无关行忽略
- QML（`TranslateHomePage.qml` + `TranslateSettingsPage.qml`，弹窗行为一致）：提取弹窗底部「AI 建议译文」按钮（enabled 绑定 `termSuggestionAvailable()`）；`suggestTermTranslations()` 收集勾选术语 + 文档行上下文发起请求；`onTermSuggestionsReady` 成功时把建议填入候选行译文输入框（可修改后添加），失败/未勾选走状态栏/infoBar 提示
- **en 目标失败提示（2026-08-19）**：目标语言为 `en` 且大模型未返回可用结果时，错误消息追加「当前目标语言为英文，若文档/术语为英文请检查目标语言设置」——英文术语 + 目标英文 = 模型自译被过滤，建议全空（用户只看到「未返回可用结果」时无从排查）
- 测试：`tst_quality` 新增 `parseTermSuggestionsFormats`（多分隔符/序号/引号/自译排除）；`extractCandidatesUnlimited` 覆盖 maxCount=-1 候选不限

### 4.3 质量自检（`QualityGate`）✅ 已实现
`TranslationService::postProcess` 对每条结果执行规则校验：
- **纯回显检测（始终生效）**：译文与原文高度相似视为未翻译——**硬性拦截**（原文永远不会被当作译文写入，不受 `qualityGateEnabled` 开关控制）；拦截**不进「质量自检复核面板」**（面板只收集规则自检告警，受开关控制），回显失败经 `translationFailed` 单条提示
  - **回显豁免（2026-08-19）**：新增 `QualityGate::hasTranslatableContent(text)`——原文去除数字/型号/代码 token（复用 `extractTokens`）后仍有 ≥2 连续字母普通词或 CJK 才视为可翻译；仅剩数字/型号/标点（"P2899R1"、"2025-03-14"、"API"）时译文=原文是**正确翻译**（专名/数字本就不译），不再误报「疑似未翻译」
  - **错误消息（2026-08-19 明确化）**：「疑似未翻译（译文与原文相同）：请检查源语言/目标语言设置；若语言正确，请检查后端模型名与 API 配置是否正确」——不再让用户误以为只是回显问题而忽略语言/模型配置错误
- **长度校验**：常规行译文长度在原文 0.2x ~ 4x 内（中文更紧凑，阈值放宽）；**短行（≤8 字符）或纯型号行阈值放宽 0.1x ~ 8x（2026-08-19）**——"OK"→"好" 0.5x、"AI"→"人工智能" 3x 天然波动大，纯数字/型号行译文本就等长；极端异常（"hi"→30 字 15x）仍拦截
- **Token 保留**：数字/版本号/占位符/代码标识符（驼峰、下划线、含数字、全大写）至少保留 80%；普通英文单词不视为 token，避免英→中误报
- **术语一致性**：术语命中率计入综合分
- 综合分 ≥ 0.6 通过（`qualityGateEnabled` 关闭时跳过规则自检与 `qualityWarning` 告警）

### 4.4 严格输出 ✅ 已实现
- `m_strictOutput`：仅输出译文，减少模型废话浪费 token

### 4.5 质量自检复核面板 ✅ 已实现（2026-08-17）
- `qualityWarning(lineNumber, issue)` 逐条发信号；主页收集到 `qualityWarnings` ListModel，`onBatchFinished` 有告警时弹出**复核面板**（行号+问题+跳转按钮，`focusLine` 定位复核），可清除列表
- 翻译开始时自动清空上一批告警
- **面板只收集规则自检告警（2026-08-19 明确）**：受 `qualityGateEnabled` 开关控制；**回显拦截不进入面板**（硬性拦截，经 `translationFailed` 单条提示，见 §4.3）——否则用户关了「质量自检」仍看到「疑似未翻译」误以为开关无效

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
    // 术语自动提取（迭代4，2026-08-19 增强）：高频词候选，频率降序，
    // 返回 [{word, count}]；支持英文/技术标识符/中文 n-gram（见 §4.2.1）；
    // maxCount=-1 不限（达标都候选），0 兼容旧语义为 1
    Q_INVOKABLE QVariantList extractTermCandidates(const QStringList &lines,
                                                   int minFreq = 3, int maxCount = 20);
    // 术语建议译文（异步，网络大模型后端专用，2026-08-19）：按文档上下文猜测
    // 术语译文，结果经 termSuggestionsReady 信号返回（见 §4.2.1）
    Q_INVOKABLE void suggestTermTranslations(const QStringList &terms,
                                             const QStringList &contextLines);
    Q_INVOKABLE bool termSuggestionAvailable() const;   // 仅网络大模型后端且已配 API 时 true
    static QVariantMap parseTermSuggestions(const QString &text, const QStringList &terms);
    Q_INVOKABLE void setQualityGateEnabled(bool enabled);

    // 成本：智能分块开关 + 预算
    Q_INVOKABLE void setSmartChunkingEnabled(bool enabled);
    Q_INVOKABLE void setMaxChunkChars(int chars);

    // 翻译入口（QML 调用，内部异步）
    Q_INVOKABLE void translateLines(const QList<int> &lineNumbers, const QStringList &sourceLines);

    // 进度 / 取消
    Q_INVOKABLE void cancelTranslation();        // 取消当前批量翻译（后端请求同步中止）
    Q_INVOKABLE bool translationActive() const;  // 是否正在翻译

    // 目标语言预检测（跳过已是目标语言的行，避免目标语言→目标语言回显误报；
    // 2026-08-19 通用化：支持 zh/ja/ko/en）
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
    // 术语建议译文结果（suggestions：术语→译文映射；ok=false 时 errorMessage 说明原因）
    void termSuggestionsReady(const QVariantMap &suggestions, bool ok,
                              const QString &errorMessage);
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
- **跳过目标语言行**：`TranslationService::isTargetLanguageText()`（**2026-08-19 通用化**，支持 zh/ja/ko/en）：中文 CJK ≥ 30%；日文假名占比 ≥ 10%（含汉字时 ≥ 5%）；韩文谚文 ≥ 30%；英文拉丁字母 ≥ 80% 且无 CJK——避免「目标语言→目标语言」回显被误报为「疑似未翻译」（用户配置 targetLang=en + 翻译英文文档即踩此坑，整篇误报）。
- **全部跳过提示（2026-08-19）**：`translateAllPending`/`translateSelected` 待译行全被跳过（跳过数 > 0）时状态栏提示「没有需要翻译的行……文档可能已是目标语言，请检查设置页的源语言/目标语言」。
- **批量合并**：`NetworkModelBackend::translateBatch` 一次请求多行（JSON 数组输出，DeepSeek 自动关闭 thinking mode 加速）；批量请求失败**返回失败结果**（不在后端内逐行兜底，见 §3.4），由 `translateBatchSync` 统一降级。
- **回显拦截**：`postProcess` 始终执行回显检测（不受质量自检开关影响），原文永远不会被当作译文写入；拦截不进复核面板，经 `translationFailed` 单条提示（见 §4.3）。

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
