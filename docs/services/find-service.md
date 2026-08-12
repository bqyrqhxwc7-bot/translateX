# FindService 查找替换服务设计文档

> 状态：v0.3 已实现（2026-08-12：新增模糊查找 fuzzy）
> 定位：大文件场景的查找/替换（对齐 ARCHITECTURE.md 的 "查找替换（异步）"）。
> 虚拟化编辑器只渲染可见行，查找必须基于 `DocumentModel` 数据而非 UI。

## 1. 职责

- 全文查找：返回匹配行号、匹配计数、下一个/上一个
- 替换：单行替换 / 全部替换（返回替换次数）
- 大小写、整词、**模糊查找**选项
- 同步实现（大文件可放线程池调用；后续可加异步包装）

## 2. API

```cpp
class FindService : public QObject {
    Q_OBJECT
public:
    explicit FindService(QObject *parent = nullptr);
    void setDocument(DocumentModel *model);

    // 查找
    Q_INVOKABLE QList<int> find(const QString &query, bool caseSensitive = false, bool wholeWord = false, bool fuzzy = false) const;
    Q_INVOKABLE int count(const QString &query, bool caseSensitive = false, bool wholeWord = false, bool fuzzy = false) const;
    Q_INVOKABLE int findNext(const QString &query, int fromLine, bool caseSensitive = false,
                             bool wholeWord = false, bool wrap = true, bool fuzzy = false) const;
    Q_INVOKABLE int findPrevious(const QString &query, int fromLine, bool caseSensitive = false,
                                 bool wholeWord = false, bool wrap = true, bool fuzzy = false) const;

    // 替换
    Q_INVOKABLE bool replaceLine(int lineNumber, const QString &query, const QString &replacement,
                                 bool caseSensitive = false, bool wholeWord = false, bool fuzzy = false);
    Q_INVOKABLE int replaceAll(const QString &query, const QString &replacement,
                               bool caseSensitive = false, bool wholeWord = false, bool fuzzy = false);

signals:
    void searchCompleted(int resultCount);   // find/count 完成后（供 UI 更新状态）
};
```

## 3. 行为

- 基于 `DocumentModel::lineText` 逐行扫描（大文件：循环 + 可取消；本轮同步）
- 整词匹配：`\b` 边界正则
- **模糊查找（fuzzy）**：查询字符按顺序出现即匹配（子序列），字符间用 `.*` 连接，如查 `trn` 命中 `translation`/`transform`；fuzzy 与 wholeWord 互斥（fuzzy 优先）
- `replaceAll`：对所有匹配行 `updateLineText`，返回替换总次数（模糊替换会替换整个匹配区间为 replacement）
- 匹配计数为出现次数（跨行不匹配，仅行内）

## 3.1 UI（2026-08-12）

- Ribbon「查找」标签只保留：查找框、上一个/下一个、计数、替换框、替换、全部替换（移除顶部冗余开关）
- 大小写敏感 / 整词 / 模糊查找开关移入**设置页「查找」卡片**（`ui/findCaseSensitive`、`ui/findWholeWord`、`ui/findFuzzy`，持久化）

## 4. 测试（tst_find）

- find 命中行号、计数
- 大小写 / 整词选项
- **模糊查找**：子序列命中、与大小写组合、空查询安全
- findNext / findPrevious（含 wrap）
- replaceLine / replaceAll 后模型内容正确
- 空查询安全
