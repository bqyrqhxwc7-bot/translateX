# 迭代4b：翻译历史面板 + Markdown 导出 + 功能引导 设计

> 状态：✅ 已实现（2026-08-18）
> 范围（用户确认）：翻译历史面板、Markdown 导出；
> 首启向导改为**不默认弹窗**，在设置页放「功能引导」按钮手动打开。

## 1. 翻译历史面板（TranslationHistoryService）

- **独立 service**（可插拔原则，不耦合 TranslationService）：
  `src/services/translationhistoryservice.h/cpp`，main_qml 注册 `translationHistoryService`
- **记录**：QML 在 `onLineTranslated` 回调里调 `record(lineNumber, source, translated, success)`
  （QML 是胶水层；source 从 `documentModel.lineText(lineNumber)` 取）
- **存储**：内存环形缓冲（上限 500 条，会话级不落盘——避免隐私与复杂度，文档注明）
- **接口**：
  - `Q_INVOKABLE void record(int lineNumber, const QString &source, const QString &translated, bool success)`
  - `Q_INVOKABLE QVariantList entries()`（最新在前，`[{line, source, translated, success, time}]`）
  - `Q_INVOKABLE void clear()`
  - `signal entryAdded()`（QML 刷新列表）
- **UI**：主页 Ribbon「翻译历史」按钮 → FluContentDialog 弹窗
  （contentDelegate 内 ListView：行号/原文/译文/时间；点击条目跳转行 focusLine；
  底部「清空」按钮；ListModel 声明在页面级——迭代4 review 教训）

## 2. Markdown 导出

- **入口**：另存为对话框加 `Markdown 文档 (*.md)` filter → `writeDocument` 加 md 分支
- **格式**（对照式，长文本友好）：
  ```
  # <文档名>

  ## 第 1 行
  原文文本
  > 译文（批注）

  ## 第 2 行
  原文文本
  ```
  - 有批注（译文）行：原文 + `> 译文` 引用块
  - 无批注行：仅原文
  - 空行跳过
- **实现**：`DocumentManager::writeDocument` 加 `isMarkdown(path)` 分支，
  复用现有保存成功流程（m_path 更新、dirty 清理、autosave 清理）
- **测试**：tst_documentmanager 加 `markdownExport` 用例（内容断言）

## 3. 功能引导（设置页按钮，不默认首启）

- **不默认弹窗**（用户明确要求）
- 设置页新增「帮助与引导」卡片：按钮「功能引导」→ FluContentDialog 弹窗
  （contentDelegate 静态文本清单：编辑器/翻译/批注/术语表/自动保存/TTS/快捷键/大文件模式）
- 纯静态内容，无状态

## 4. 测试计划

- `tst_history`（新目标）：record/entries 顺序与上限 500/clear/成功失败标记
- `tst_documentmanager`：markdownExport 内容断言（原文/译文引用块/无批注行）
- 两处注册：`CMakeLists.txt` translex 目标 + `tests/CMakeLists.txt` SERVICE_SOURCES

## 5. 限制（明示）

- 翻译历史仅内存（会话级），重启清空；上限 500 条（环形覆盖）
- Markdown 导出为单向（不支持导入）
- 引导内容为静态清单（后续可扩展为分步引导）