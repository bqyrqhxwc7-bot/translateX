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
- **路径**：`%APPDATA%/sr291/Translex/autosave/<文档名>.autosave.trx`（`.trx` 完整往返含批注；
  文件名 sanitize；未命名文档 → `未命名.autosave.trx`）
- **逻辑**：
  - `QTimer` 60s tick：`isDirty() && autosaveEnabled && !limitedMode`（受限模式禁用，
    避免大文件反复写盘）→ `TrxParser::write` 到 autosave 文件（不清 dirty）
  - 保存成功/打开文件/新建文档/恢复后 → 删除对应 autosave 文件
  - `autosaveEnabled` 变化（`configChanged` 信号）→ QML 调 `setAutosaveEnabled` 启停 timer
- **崩溃恢复**：
  - `hasAutosave()` → 主页 onCompleted 弹 FluContentDialog「检测到未保存的更改，是否恢复？」
  - `restoreAutosave()` = 恢复（openFile autosave 文件）/ `discardAutosave()` = 丢弃（删文件）
- **接口**：`setAutosaveEnabled(bool)`、`hasAutosave()`、`autosavePath()`、
  `restoreAutosave()`、`discardAutosave()`；`operationFailed` 复用提示
- **注意**：自动保存文件本身不再二次自动保存（tick 时若当前路径是 autosave 则跳过）

## 3. 术语自动提取（TermGlossary::extractCandidates）

- **接口**：`extractCandidates(const QStringList &lines, int minFreq = 3, int maxCount = 20)`
  → `QList<QPair<QString, int>>`（词 → 频次，频率降序）
- **算法**（英文为主，文档注明中文暂不支持自动提取）：
  1. 正则提取 ASCII 字母序列（≥3 字母），转小写
  2. 过滤停用词（内置 ~60 词表：a/an/the/and/.../i/you/he/she/...）
  3. 过滤已在术语表中的词（`contains`）
  4. 频率 ≥ minFreq → 按频率降序 → top maxCount
- **UI**（设置页术语表卡片）：「从文档提取」按钮 →
  `FluContentDialog` 弹窗列出候选（CheckBox 多选 + 全选）→ 确认加入术语表
  （经现有 `addTerm` 流程写回 `translation.glossary`）
- 空文档/无候选 → 提示"未提取到高频词"

## 4. 测试计划

- `tst_documentmodel`：`stats()` 行/字/词/批注/rich/image 计数正确
- `tst_documentmanager`：
  - 自动保存：dirty 后 tick 生成 `.autosave.trx`（隔离数据目录）；保存后清理；
    enabled=false 不写；受限模式不写
  - 恢复：restoreAutosave 后行/批注一致；discardAutosave 删除文件
- `tst_quality`（或新 tst）：`extractCandidates` 频率排序、停用词过滤、minFreq、
  已有术语排除、maxCount 截断

## 5. 限制（明示）

- 自动保存间隔固定 60s；受限模式（大文件）禁用自动保存
- 术语提取仅英文单词（中文分词复杂，后续按需）；不区分词形（单复数/时态不归并）
- 统计为一次性快照（不实时增量）
