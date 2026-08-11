# CommentService 批注服务设计文档

> 状态：v0.2 已实现（核心类 + DocumentModel provider 集成 + QML 接线 + 测试）
> 定位：把批注从 `DocumentModel` 中独立为**可插拔服务**，作为批注数据的**单一数据源**，
> 供翻译服务写入译文、用户手写批注、第三方插件扩展（对齐 SERVICE-ARCHITECTURE.md）。

## 1. 现状与问题

- 批注目前内嵌在 `DocumentModel.LineEntry`（`comment`/`hasComment` 字段 + `setComment`/`commentAt`/`hasCommentAt`）
- 缺点：批注与行文本耦合，无法独立复用/持久化/扩展；第三方插件拿不到批注管理能力

## 2. 目标

- **单一数据源**：批注由 `CommentService` 持有（`QHash<行号, 文本>`）
- **可插拔**：任何模块（翻译服务、编辑器 UI、第三方插件）经 `CommentService` 读写批注
- **行号随编辑正确平移**：插入/删除行时批注跟随
- **可持久化**：导出/导入 JSON（为 DocumentManager 打开/保存准备）
- **兼容**：`DocumentModel` 现有批注 API 签名不变，内部委托 provider（无 provider 时退回内部存储）

## 3. 架构

```
QML (TranslateHomePage / 编辑器)
  │  翻译结果 → commentService.setComment(line, text)
  ▼
CommentService (单一数据源, QHash<int,QString>)
  │  commentChanged(line) / commentsReset() 信号
  ▼
DocumentModel (可选关联 commentProvider)
  ├─ 有 provider：HasComment/CommentText 角色 + setComment/commentAt 委托 CommentService
  └─ 无 provider：内部存储（保持独立可用/单测兼容）
```

## 4. CommentService API

```cpp
class CommentService : public QObject {
    Q_OBJECT
public:
    explicit CommentService(QObject *parent = nullptr);

    // 读写（空文本 = 删除）
    Q_INVOKABLE void setComment(int lineNumber, const QString &text);
    Q_INVOKABLE void removeComment(int lineNumber);
    Q_INVOKABLE QString commentAt(int lineNumber) const;
    Q_INVOKABLE bool hasCommentAt(int lineNumber) const;

    // 统计 / 全量
    Q_INVOKABLE int count() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap allComments() const;   // { "行号": "文本" }

    // 行号平移（DocumentModel 插入/删除行后调用，保持批注跟随内容）
    Q_INVOKABLE void shiftLines(int fromLineNumber, int delta);

    // 持久化（JSON：{ version, comments: { "行号": "文本" } }）
    Q_INVOKABLE bool exportToFile(const QString &path) const;
    Q_INVOKABLE bool importFromFile(const QString &path);

signals:
    void commentChanged(int lineNumber);   // 单行批注变化
    void commentsReset();                  // clear / import 后全量变化
};
```

## 5. DocumentModel 集成（provider 模式）

```cpp
// documentmodel.h 新增
Q_INVOKABLE void setCommentProvider(CommentService *provider);

// 内部行为：
// - setComment/commentAt/hasCommentAt：有 provider → 委托；无 → 内部存储
// - data() HasCommentRole/CommentTextRole：有 provider → 查 provider
// - insertLine(at)：provider->shiftLines(at, +1)
// - removeLine(line)：provider->removeComment(line); provider->shiftLines(line+1, -1)
// - 连接 provider->commentChanged → dataChanged(line)；commentsReset → 全量 dataChanged
```

## 6. QML 接线

- `main_qml.cpp`：创建 `CommentService` 暴露 context property `commentService`
- `TranslateHomePage`：`Component.onCompleted` 里 `documentModel.setCommentProvider(commentService)`；翻译写入改走 `commentService.setComment(...)`（或保持 `documentModel.setComment`，内部已委托）

## 7. 测试计划（`tst_comment`）

- 增删查/空文本删除/count/clear
- allComments 往返
- 行号平移（插入/删除后批注跟随）
- 导出/导入文件往返
- 与 DocumentModel 集成：setProvider 后角色渲染、dataChanged 触发、insert/remove 行号跟随
- 无 provider 时 DocumentModel 内部存储兼容（现有 tst_documentmodel 回归）

## 8. 分步实施

1. ~~文档确认~~ ✅
2. `CommentService` 核心类 + 测试 ✅ `src/services/commentservice.*`
3. `DocumentModel` provider 集成 + 行号平移 ✅ `setCommentProvider`/委托/平移
4. QML 接线 ✅ `main_qml.cpp` 暴露 `commentService`；主页面 `setCommentProvider` + 翻译写入走 service
5. 构建 + 回归 ✅ 7/7 通过（新增 tst_comment 6 用例）
6. 文档收尾 ✅

## 10. 实现要点

- 数据单一数据源在 `CommentService`（`QHash<int,QString>`）；空文本=删除；`shiftLines(from,delta)` 平移批注行号（平移后为负则丢弃）
- `DocumentModel::setCommentProvider(provider)`：连接 `commentChanged`→单行 `dataChanged`、`commentsReset`→全表 `dataChanged`；`HasComment/CommentText` 角色与 `setComment/commentAt/hasCommentAt` 委托 provider
- 编辑联动：`insertLine`→`shiftLines(pos,+1)`；`removeLine`→`removeComment(line)+shiftLines(line+1,-1)`；`setLines/clear`→`clear()`
- 无 provider 时退回内部存储（独立可用/兼容）
- 持久化：`exportToFile/importFromFile`（JSON `{version, comments:{行号:文本}}`），为 DocumentManager 预留

## 9. 关键决策记录

- 数据归属：**CommentService 单一数据源**（可插拔目标），DocumentModel 用 provider 委托
- 兼容：`DocumentModel` 现有批注 API 签名不变；无 provider 时内部存储（独立可用）
- 持久化：JSON 文件（`{ version, comments }`），为 DocumentManager 预留
- 批注存根：行号→文本（译文批注、用户批注统一存储，不做类型区分）
