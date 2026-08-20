# 迭代4：文档统计 + 自动保存 + 术语自动提取 设计

> 状态：✅ 已实现（2026-08-17）
> 范围（用户确认）：本轮只做文档统计 / 自动保存 / 术语自动提取；
> Markdown 导出 / 翻译历史面板 / 首启向导 留待后续。
> 通用原则：跨平台、可测试、不破坏受限模式与大文件性能。

## 1. 文档统计（DocumentModel::stats）

- **接口**：`DocumentModel::stats()` Q_INVOKABLE → `QVariantMap`
  - `lines` 总行数、`nonEmptyLines` 非空行、`chars` 非空白字符数、`words` 空白分词数、
    `comments` 批注数（走 CommentService）、`richLines` 富文本行、`imageLines` 图片行
- **性能**：一次性全量遍历（50 万行 ~百 ms 级，仅打开/翻译完成/批注变更时刷新，不实时）
- **UI**：状态栏「共 N 行 · M 字 · K 条批注」；打开文档/翻译完成/批注变化时刷新
- **受限模式**：只读遍历，正常可用

## 2. 自动保存（DocumentManager）

- **配置**（`ui.json`，设置页自动渲染）：
  - `autosaveEnabled`（bool，default `true`）
  - 间隔固定 60s（不进配置，减少复杂度；文档注明）
- **路径**：`QStandardPaths::AppConfigLocation`（Windows 实测 `%LOCALAPPDATA%/sr291/Translex/autosave/`）
  `<文档名>-<路径哈希8位>.autosave.trx`（`.trx` 完整往返含批注；文件名 sanitize；
  路径哈希避免跨目录/跨格式同名文档互相覆盖；未命名文档 → `未命名-<hash>.autosave.trx`）
- **逻辑**：
  - `QTimer` 60s tick：`isDirty() && autosaveEnabled && !limitedMode`（受限模式禁用，
    避免大文件反复写盘）→ `TrxParser::write` 到 autosave 文件（不清 dirty）；
    meta 副本注入 `originalPath` 供恢复时还原原始文档路径
  - 保存成功/打开文件/新建文档/恢复后 → 删除对应 autosave 文件（新旧路径都清理）
  - `autosaveEnabled` 变化（`configChanged` 信号）→ 主页 Connections 调 `setAutosaveEnabled` 立即生效
- **崩溃恢复**：
  - 启动时 `takeAutosavePrompt()`（应用级单例只弹一次，NoStack 页面重建不重复弹）
    → FluContentDialog「检测到上次未保存的更改（<文件名（时间）>），是否恢复？」
    （当前文档 dirty 时附加覆盖警告）
  - `restoreAutosave()` = 恢复（openFile autosave 文件 → 读 `meta.originalPath` 还原
    `m_path` + `setDirty(true)` + 移除最近文件死条目）/ `discardAutosave()` = 丢弃（删全部）
- **接口**：`setAutosaveEnabled(bool)`、`hasAutosave()`、`autosavePath()`、
  `restoreAutosave()`、`discardAutosave()`；`operationFailed` 复用提示
- **注意**：自动保存文件本身不再二次自动保存（tick 时若当前路径是 autosave 则跳过）

## 3. 术语自动提取（TermGlossary::extractCandidates）

> **2026-08-19 更新**：提取已支持中文与技术标识符（算法细节以 `translation-service.md` §4.2.1 为权威）；UI 增加设置页行内译文编辑与主页 Ribbon「提取术语」入口。本节为迭代4 原始设计与通用语义，要点未变者不再重复。

- **接口**：`extractCandidates(const QStringList &lines, int minFreq = 3, int maxCount = 20)`
  → `QList<QPair<QString, int>>`（词 → 频次，频率降序）
- **算法**（2026-08-19 起支持三类候选，不再限英文）：
  1. 英文/标识符词形 `[A-Za-z0-9+#-]{2,}`：覆盖技术标识符（API/C++/P2899R1/x86-64），
     必须含字母（纯数字/符号过滤），按小写归组统计（大小写不敏感）
  2. 中文 n-gram：连续 CJK 段内 2-3 字滑窗，含虚词字符（的了是在和与及或为有…）则跳过
  3. 过滤停用词（内置 ~60 词表）与已收录术语（大小写不敏感）；频率 ≥ minFreq →
     按频率降序 → top maxCount
  4. 返回原文中**最高频的实际书写形式**（术语表校验是大小写敏感的）
- **UI**（设置页术语表卡片）：「从文档提取」按钮（受限模式禁用；最多取前 5 万行）→
  `FluContentDialog` 弹窗列出候选（CheckBox 多选 + 全选）→ 确认加入术语表；
  **2026-08-19 起列表行内可编辑标准译文**（原文 → FluTextBox 译文 → 删除，
  `updateTermTranslation` 失焦保存）
- **翻译功能区入口（2026-08-19）**：主页 Ribbon「提取术语」按钮（`FluentIcons.DictionaryAdd`，
  受限模式禁用）→ `termExtractDialog` 弹窗（勾选 + 词频 + 行内译文 + 全选 + 添加选中）；
  术语表为 `TranslationService` 应用级单例，主页/设置页共享同一数据源
- 空译文占位语义不变（不注入提示词、不参与质量校验；经现有 `addTerm` 流程写回
  `translation.glossary`）
- 空文档/无候选 → 提示"未提取到高频词"

## 4. 测试计划

- `tst_documentmodel`：`stats()` 行/字/词/批注/rich/image 计数正确
- `tst_documentmanager`：
  - 自动保存：dirty 后 tick 生成 `.autosave.trx`（隔离数据目录）；保存后清理；
    enabled=false 不写；受限模式不写
  - 恢复：restoreAutosave 后行/批注一致；discardAutosave 删除文件
- `tst_quality`（或新 tst）：`extractCandidates` 频率排序、停用词过滤、minFreq、
  已有术语排除、maxCount 截断；`extractCandidatesEnhanced`（2026-08-19）：
  标识符（API/C++/P2899R1/x86-64）+ 中文 n-gram（术语表/翻译/质量）提取 + 停用词/纯数字排除

## 5. 限制（明示）

- 自动保存间隔固定 60s；受限模式（大文件）禁用自动保存
- ~~术语提取仅英文单词~~（**2026-08-19 已支持中文 n-gram 与技术标识符**）；不区分词形（单复数/时态不归并）
- 统计为一次性快照（不实时增量）
