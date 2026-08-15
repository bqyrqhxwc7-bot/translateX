# 大文件受限模式（Large File Limited Mode）设计文档 v1

> 状态：已实现（2026-08-15）· 任务 D
> 来源：`file-service.md` §8 阶段 D（阈值 5 万行 / 200MB，确认点 F 用户已确认）
> 关联：`DocumentModel` / `DocumentManager` / `qml/TranslateHomePage.qml`
> 目标：超大文档打开后进入「受限模式」——**数据不丢、不崩溃、常用功能保留**，禁止重功能（富文本/图片渲染、批注编辑、翻译）以防卡死。

## 1. 触发阈值（默认，测试可调）

| 指标 | 阈值 | 判定时机 |
| --- | --- | --- |
| 行数 | > 50,000 | 解析完成后（`model->lineCount()`） |
| 文件体积 | > 200MB | `QFileInfo(path).size()`（打开前即可判定） |

- 阈值存放在 `DocumentManager` 静态成员（`s_maxLines`/`s_maxBytes`），`setLargeFileLimits(maxLines, maxBytes)` 供测试注入小阈值（**测试用完必须还原**）；传 0 表示禁用该项。
- 任意一项超限 → 进入受限模式。

## 2. 受限模式行为

| 能力 | 受限模式下 |
| --- | --- |
| 显示层（rich/image） | **渲染回退纯文本**（编辑层文本照常显示；显示层数据保留，.trx 往返不丢） |
| 批注编辑（增/改/删/清空） | **禁用**（已有批注只读显示） |
| 翻译（当前行/全部/选中/快捷键/浮窗） | **禁用**，操作提示「大文件受限模式：已禁用翻译与批注编辑」 |
| 编辑（行内编辑/插行/删行/剪切粘贴/撤销重做） | **保留** |
| 查找/替换/章节/滚动 | **保留** |
| 保存/另存为 | 保留（保存不影响受限状态） |

## 3. 实现

### 3.1 DocumentModel（标志 + 渲染掩蔽）

- `limitedMode` 属性（`Q_PROPERTY` + `limitedModeChanged` 信号）
- `data()` 对 `DisplayRole`/`RichTextRole`/`ImageIdsRole` 在受限模式下返回 `"plain"`/空/空——**渲染层天然回退纯文本**，QML 无需改渲染分支
- 显示层数据本身不动（`displayAt/richAt/imageIdsAt` 原样），`.trx` 保存仍完整

### 3.2 DocumentManager（触发）

- `applyLargeFileLimit(path)`：所有 openFile 成功路径（txt/trx/docx/pdf）末尾调用；`newDocument` 重置为 false
- 行数判定在解析之后（txt 拆分、docx/pdf/trx 解析结果）

### 3.3 QML（入口守卫 + 提示条）

- 页面属性 `limited`（绑定 `documentModel.limitedMode`）
- `limitedBlocked()`：统一状态栏提示
- 守卫：`translateCurrent/translateAllPending/translateSelected/focusComment/deleteCommentAt/clearAllComments`
- Ribbon 翻译三按钮 `enabled: !page.limited`；右键菜单批注项（添加/删除）禁用
- **提示条**：Ribbon 与编辑器之间常驻琥珀色横条「大文件受限模式：已禁用富文本/图片渲染、批注编辑与翻译（编辑/查找/章节正常）」
- 快捷键（Ctrl+Alt+T 等）与浮窗按钮最终都进守卫函数，无需单独处理

## 4. 测试计划（tst_documentmanager）

| 用例 | 断言 |
| --- | --- |
| 5 万行 + 1 行文本文件 → openFile | `limitedMode()==true`（行数路径） |
| 小文件（10 行）→ openFile | `limitedMode()==false` |
| `setLargeFileLimits(100000, 512)` + 内容 >512B 的文件 | `limitedMode()==true`（体积路径） |
| 打开大文件 → newDocument | 复位 `limitedMode()==false` |
| cleanup | 还原默认阈值 |

## 5. 限制（明示）

- 受限状态在**重新打开/新建**前保持不变（另存为更小文件不自动退出受限）
- 超大文件打开仍是全量读入内存（虚拟化渲染已缓解行渲染；体积级超大文件后续可做流式打开——超出本次范围）
- 阈值不可在设置页调整（v1 硬编码默认值，测试可调）
