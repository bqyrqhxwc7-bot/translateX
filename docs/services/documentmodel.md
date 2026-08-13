# DocumentModel 服务文档

> 状态：已实现
> 类型：L1 内置服务（编译进应用）

## 1. 职责

懒加载文档行模型。解决大文件性能问题：**只渲染可见行**，编辑只通知单行。

## 2. 使用（QML）

```qml
import TranslateX.Services 1.0

DocumentModel {
    id: docModel
}
```

绑定到 `ListView.model` 即自动虚拟化渲染。

## 3. API

| 方法 | 说明 |
| --- | --- |
| `setLines(QStringList)` | 批量载入（打开/导入时调用一次） |
| `lineText(lineNumber)` | 获取某行文本 |
| `lineCount()` | 总行数 |
| `updateLineText(lineNumber, text)` | 更新单行（只通知该行） |
| `insertLine(atLineNumber, text)` | 插入行（负数拒绝，返回 -1） |
| `removeLine(lineNumber)` | 删除行 |
| `appendLine(text)` | 末尾追加行 |
| `setComment(lineNumber, text)` | 设置/清空批注 |
| `hasCommentAt(lineNumber)` | 是否有批注 |
| `commentAt(lineNumber)` | 获取批注 |
| `setLineDisplay(lineNumber, mode)` | 设置显示层模式（plain/rich/image，不参与 undo） |
| `setLineRich(lineNumber, html)` | 设置富文本显示层（HTML 片段） |
| `setLineImages(lineNumber, ids)` | 设置图片显示层（引用 meta.images 的 id） |
| `displayAt(lineNumber)` | 获取显示层模式 |
| `richAt(lineNumber)` | 获取富文本 |
| `imageIdsAt(lineNumber)` | 获取图片 id 列表 |
| `clear()` | 清空 |

## 3.1 显示层（.trx 富文本/图片，A3 新增）

- `text` 永远是**编辑层**（权威）；`display/rich/imageIds` 为显示层（仅供渲染/往返，**不参与 undo**）
- `plain`（默认）：TextEdit 可编辑；`rich`：Text RichText 只读显示（成为当前行时自动降级 plain）；`image`：图片行（A3 无图片源，按文本显示）
- 编辑 rich/image 行（成为当前行）→ 自动 `setLineDisplay(plain)` 降级（显示层丢弃）

## 4. Roles（ListView delegate 使用）

| Role | 类型 | 说明 |
| --- | --- | --- |
| `lineNumber` | int | 1-based 行号 |
| `text` | string | 行文本（编辑层） |
| `isComment` | bool | 是否批注行（预留） |
| `hasComment` | bool | 该行是否有批注 |
| `commentText` | string | 批注内容 |
| `display` | string | 显示层模式 plain/rich/image |
| `rich` | string | 富文本 HTML（rich 模式） |
| `imageIds` | string[] | 图片 id 列表（image 模式） |

## 5. 性能指标（见 `tests/tst_performance.cpp`）

| 场景 | 规模 | 耗时 |
| --- | --- | --- |
| 加载 | 50 万行 | ~78ms |
| 更新 | 10 万行 | ~220ms |
| 批注 | 2.5 万条 | ~54ms |
| 随机访问 | 2 万次 | ~2ms |

## 6. 扩展点

- **批注**：当前为单字符串，后续可扩展为富文本/多段
- **语言标记**：可增加 `language` role 供高亮/翻译判断
- **章节**：可由 ChapterService 在模型上叠加索引

## 7. 测试

见 `tests/tst_documentmodel.cpp`（健康度）与 `tests/tst_performance.cpp`（性能）。
