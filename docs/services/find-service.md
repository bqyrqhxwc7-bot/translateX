# FindService 查找替换服务设计文档

> 状态：v0.2 已实现
> 定位：大文件场景的查找/替换（对齐 ARCHITECTURE.md 的 "查找替换（异步）"）。
> 虚拟化编辑器只渲染可见行，查找必须基于 `DocumentModel` 数据而非 UI。

## 1. 职责

- 全文查找：返回匹配行号、匹配计数、下一个/上一个
- 替换：单行替换 / 全部替换（返回替换次数）
- 大小写、整词选项
- 同步实现（大文件可放线程池调用；后续可加异步包装）

## 2. API

```cpp
class FindService : public QObject {
    Q_OBJECT
public:
    explicit FindService(QObject *parent = nullptr);
    void setDocument(DocumentModel *model);

    // 查找
    Q_INVOKABLE QList<int> find(const QString &query, bool caseSensitive = false, bool wholeWord = false) const;
    Q_INVOKABLE int count(const QString &query, bool caseSensitive = false, bool wholeWord = false) const;
    Q_INVOKABLE int findNext(const QString &query, int fromLine, bool caseSensitive = false,
                             bool wholeWord = false, bool wrap = true) const;
    Q_INVOKABLE int findPrevious(const QString &query, int fromLine, bool caseSensitive = false,
                                 bool wholeWord = false, bool wrap = true) const;

    // 替换
    Q_INVOKABLE bool replaceLine(int lineNumber, const QString &query, const QString &replacement,
                                 bool caseSensitive = false, bool wholeWord = false);
    Q_INVOKABLE int replaceAll(const QString &query, const QString &replacement,
                               bool caseSensitive = false, bool wholeWord = false);

signals:
    void searchCompleted(int resultCount);   // find/count 完成后（供 UI 更新状态）
};
```

## 3. 行为

- 基于 `DocumentModel::lineText` 逐行扫描（大文件：循环 + 可取消；本轮同步）
- 整词匹配：`\b` 边界正则
- `replaceAll`：对所有匹配行 `updateLineText`，返回替换总次数
- 匹配计数为出现次数（跨行不匹配，仅行内）

## 4. 测试（tst_find）

- find 命中行号、计数
- 大小写 / 整词选项
- findNext / findPrevious（含 wrap）
- replaceLine / replaceAll 后模型内容正确
- 空查询安全
