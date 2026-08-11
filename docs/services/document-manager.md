# DocumentManager 文档管理服务设计文档

> 状态：v0.2 已实现
> 定位：打开/保存/新建文档，是翻译工作流的入口；与 `DocumentModel`（行文本）+ `CommentService`（批注）协作，批注随文档持久化。

## 1. 职责

- 管理当前文档路径与修改状态（dirty）
- 打开文本文件 → `DocumentModel::setLines`；加载同名 `.comments.json` 批注 → `CommentService::importFromFile`
- 保存：写行文本 + 导出批注到 `<path>.comments.json`
- 新建文档

## 2. 文件格式

- 文本：UTF-8（优先 BOM 检测；无 BOM 默认 UTF-8）
- 批注：`<path>.comments.json`，格式沿用 `CommentService` 的 `{version, comments:{行号:文本}}`

## 3. API

```cpp
class DocumentManager : public QObject {
    Q_OBJECT
public:
    explicit DocumentManager(QObject *parent = nullptr);

    // 关联（QML 中 documentModel 在页面内创建，用 setter 注入）
    Q_INVOKABLE void setDocument(DocumentModel *model);
    Q_INVOKABLE void setComments(CommentService *comments);

    Q_INVOKABLE QString currentPath() const;
    Q_INVOKABLE bool isDirty() const;
    Q_INVOKABLE QString documentName() const;   // 文件名（未保存时 "未命名"）

    Q_INVOKABLE bool newDocument(const QStringList &initialLines = {});
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE bool saveFile();                 // 无路径则 saveFileAs
    Q_INVOKABLE bool saveFileAs(const QString &path);

signals:
    void documentChanged(const QString &path);
    void dirtyChanged(bool dirty);
    void operationFailed(const QString &message);
};
```

## 4. 行为

- `openFile`：读文本 → `setLines`；存在 `.comments.json` → 导入批注；`m_path` 更新；dirty=false
- `saveFile`/`saveFileAs`：写文本（`\n` 拼接行）+ 批注导出；成功后 dirty=false
- dirty 跟踪：连接 model 的 `dataChanged`/`rowsInserted`/`rowsRemoved`/`lineCountChanged` 与 comments 的 `commentChanged`/`commentsReset` → dirty=true（打开/保存/新建后复位）
- 失败：返回 false + `operationFailed(message)`

## 5. 测试（tst_documentmanager）

- newDocument / dirty 状态流转
- openFile 往返（写临时文件 → open → 行数/内容一致）
- 批注随文档持久化（save → open 新实例 → 批注还原）
- saveFileAs / 路径更新
- 失败路径（不存在的文件）
