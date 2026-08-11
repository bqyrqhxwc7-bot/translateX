# Ribbon 工具栏 + Service 双呈现架构设计

> 状态：阶段1-3 已实现（2026-08-11）；阶段4 收尾待做
> 关联：`qml/TranslateHomePage.qml`、`docs/ui/translate-panel.md`、`src/services/*`
> 原则（AGENTS.md）：文档先行 → 用户确认 → 小步实现

## 1. 目标

1. **顶部工具栏重构为 PPT 式 Ribbon**：去掉下拉菜单栏 + 文档工具栏，改为「文件 / 开始 / 翻译 / …」标签页 + 功能区（每个标签下是一组按钮，类似 PowerPoint）。
2. **Service 双呈现模型**：每个 service 可声明自己的呈现方式——
   - `ppt`：Ribbon 标签页（功能区）
   - `floating`：可拖动的独立浮窗
   - 内置文档/编辑 service 只提供 `ppt`；**翻译 service 同时提供 `ppt` + `floating` 两种，可切换**。
3. **浮窗位置友好化**：不提供预设、不暴露 X/Y 数字设置，**只记住用户拖动后的位置**。
4. **覆盖全部 service**：文件、编辑、翻译、章节、批注、查找均纳入 Ribbon。

## 2. Ribbon 结构

顶部用 `FluPivot`（FluentUI 标签页控件），每个标签 = 一个 service 的 PPT 式功能区：

| 标签 | 功能区内容 | 关联 service |
| --- | --- | --- |
| **文件** | 新建 / 打开 / 最近 / 保存 / 另存为 / 加载示例 / 清除译文 | DocumentManager |
| **开始** | 撤销 / 重做 | DocumentModel |
| **翻译** | 翻译当前行 / 全部待译 / 选中行 + 进度 + 取消 + **「浮窗」切换按钮** | TranslationService |
| **章节** | 章节列表 / 跳转（基础版） | ChapterService |
| **批注** | 批注计数 / 清空 / 导出导入（基础版） | CommentService |
| **查找** | 查找 / 替换（基础版） | FindService |

每个功能区 = `RowLayout` 内的图标/文字按钮组（参考旧版 QtWidgets 的 toolbar 布局）。

## 3. Service 呈现模型（QML 侧约定）

```
每个 service 的 UI 由两部分组成：
├── RibbonPage.qml   # PPT 式：标签页功能区内容（组件，可被 FluPivot 加载）
└── FloatingPanel.qml # 浮窗内容（可选；仅翻译实现）
```

- 翻译：`RibbonPage`（翻译标签）+ `FloatingPanel`（浮窗），模式切换按钮在翻译标签上。
- 文件/开始/章节/批注/查找：只有 `RibbonPage`。

页面 `TranslateHomePage.qml` 负责：
- 顶部 `FluPivot` 承载所有 Ribbon 标签（当前选中标签切换功能区）
- 翻译为 `floating` 时显示浮层（现有 `panelOverlay` 机制），`ppt` 时显示翻译标签功能区
- 浮窗可见性由「翻译」标签上的「浮窗」开关 + 视图入口控制

## 4. 浮窗位置策略（去掉数字设置）

- **删除** `ui.translatePanelX` / `ui.translatePanelY` 两个 number 设置项（不再在设置页暴露）。
- 拖动标题栏时内部记录位置（保留 `panelX/panelY` 属性，持久化到 config 但**不渲染为设置项**）。
- 启动/打开浮窗时恢复到上次拖动位置；无历史时用合理默认（右上角内容区）。
- 设置页「翻译面板」折叠区只保留：模式（PPT/浮窗）+ 启动时显示。

## 5. 配置项调整（ui.json）

| key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `translatePanelMode` | enum | `ppt` | `ppt`=Ribbon 标签（默认）；`floating`=浮窗 |
| `translatePanelVisible` | bool | `false` | 启动时是否显示浮窗（floating 模式） |
| `translatePanelX/Y` | number | （隐藏） | 仅内部记忆拖动位置，不出现在设置页 |

> 模式默认值从 `floating` 改为 `ppt`（Ribbon 作为默认，浮窗为可选），符合"PPT 式为主、浮窗为辅"。

## 6. 实现步骤（小步可验证）

- **阶段 1 — Ribbon 框架**：`FluPivot` 替换顶部菜单栏+工具栏；「文件」「开始」标签功能区；行数显示移到状态栏。
- **阶段 2 — 翻译双模式**：翻译标签功能区 + 「浮窗」切换；浮窗位置只记拖动（删数字设置项）。
- **阶段 3 — 其他 service 标签**：章节 / 批注 / 查找基础功能区。
- **阶段 4 — 收尾**：清理 docked 旧实现与诊断代码、测试、更新文档。

### 阶段 1 详细：文件 / 开始 标签

| 标签 | 按钮（FluentUI 组件） | 动作 |
| --- | --- | --- |
| 文件 | `FluButton` 新建 | `newDocument()` |
| 文件 | `FluButton` 打开 | `openDocument()` |
| 文件 | `FluButton` 最近（enabled=有历史） | 弹出最近文件菜单 |
| 文件 | `FluButton` 保存 / 另存为 | `saveDocument()` / `saveDocumentAs()` |
| 文件 | `FluButton` 加载示例 / 清除译文 | `loadDemoDocument()` / `clearAllComments()` |
| 开始 | `FluButton` 撤销 / 重做（enabled 跟随 UndoStack） | `undo()` / `redo()` |

### 阶段 2 详细：翻译标签 + 浮窗

| 控件（FluentUI） | 说明 |
| --- | --- |
| `FluFilledButton` 翻译当前行 / 全部待译 / 选中行 | 调 `translateCurrent/All/Selected` |
| `FluProgressBar` + X/Y 文本 + `FluButton` 取消 | 进度（翻译中可见） |
| `FluToggleSwitch` 浮窗 | 切换 `ppt ↔ floating`，写 `ui.translatePanelMode` |
| 浮窗 | 复用现有 `panelOverlay` 浮层；位置只记拖动 |

### 阶段 3 详细：章节 / 批注 / 查找（已实现 2026-08-11）

**章节标签（ChapterService）**
- `FluButton` 上一章 / 下一章：`goPrevChapter()` / `goNextChapter()`（`chapterStartLine(i)` → `focusLine()`）
- `FluText`：当前章节标题 · 共 N 章（`chapterTitles()`；`onCurrentLineChanged` 同步章节指示）
- `FluButton` 重新检测：`rebuild()` + 刷新列表
- **偏差说明**：原计划用 `FluComboBox` 选章节，但 NoStack 模式下 `FluComboBox` 的 Popup 不可用（window=null，用户此前确认"模式切换下拉框不可用"），改用上一章/下一章按钮导航。

**批注标签（CommentService）**
- `FluText`：批注统计（`allComments()` 行数）
- `FluButton` 上一条 / 下一条批注：遍历有序批注行号跳转（`focusLine()`）
- `FluButton` 清空：`clear()`（复用页面 `clearAllComments()`）
- `FluButton` 导出 / 导入：`exportToFile()` / `importFromFile()` + 两个 `FileDialog`（.json）
- `commentChanged` / `commentsReset` 信号 → 刷新计数与导航状态

**查找标签（FindService）**
- `FluTextBox` 查找输入（Enter=下一个）+ `FluButton` 上一个 / 下一个：`findNext()` / `findPrevious()`
- `FluText`：匹配计数（`count()`，状态栏同步"找到 N 处"）
- `FluTextBox` 替换输入 + `FluButton` 替换 / 全部替换：`replaceLine()` / `replaceAll()`（替换后刷新编辑行/脏标记）
- `FluToggleSwitch` 大小写 / 整词（`clickListener` 直接切换，避免 checked 绑定干扰）
- 匹配行定位复用 `focusLine()`（当前行高亮）

### 技术选型（FluentUI 1.7.7）

`FluPivot`/`FluPivotItem`（标签）、`FluButton`/`FluFilledButton`/`FluIconButton`（按钮）、`FluTextBox`（输入）、`FluComboBox`（章节/语言）、`FluProgressBar`（进度）、`FluToggleSwitch`（开关）、`FluDivider`（分隔）、`FluScrollBar`。

> 所有标签内容用 `FluPivotItem.contentItem`（Component）承载，宿主页面 `TranslateHomePage.qml` 统一连接各标签信号到 service/页面函数，保持 service 可插拔。

## 7. 验证方式

- 构建通过、10/10 测试通过。
- 启动后：顶部为 Ribbon 标签，「文件」功能区可新建/打开/保存；「翻译」标签可翻译 + 切浮窗；浮窗拖动后位置被记住（重启恢复）。
- 像素/截图确认功能区与浮窗布局无错位、无按钮凸出。

## 8. 待确认点

- [ ] Ribbon 标签顺序与命名（文件/开始/翻译/章节/批注/查找）是否符合预期
- [ ] 翻译模式默认值改为 `ppt` 是否 OK
- [ ] 章节/批注/查找第一阶段做"基础功能区"（列表/计数/跳转）即可，还是需要完整功能
