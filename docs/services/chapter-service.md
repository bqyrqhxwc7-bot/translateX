# ChapterService 章节服务设计文档

> 状态：v0.2 已实现
> 定位：按标题行识别文档章节，为翻译提供章节级上下文、跳转、分块边界（对齐 ARCHITECTURE.md 的 "章节索引"）。

## 1. 职责

- 扫描文档，按章节标题模式识别章节边界
- 提供章节列表（标题 + 起始行）与"某行属于哪个章节"查询
- 支持自定义标题模式（正则）

## 2. 章节标题模式（默认）

默认识别（正则交替）：
- 中文：`第[一二三四五六七八九十百千万0-9]+[章节篇回]`
- Markdown：`^#{1,6}\s`
- 重装饰线：`^[=-]{3,}\s*$`（前一行作为标题）
- 英文：`^[A-Z][A-Za-z0-9 ]{2,}$`（可选，可能误报，默认关闭或谨慎）

实现上提供 `setChapterPattern(regex)`，默认用一个保守组合；测试验证。

### 2.1 富文本样式特征（2026-08-19 增强）

默认正则只匹配 markdown `#` 标题和中「第X章」，对 PDF/docx 导入的**富文本标题**（"1 Introduction"、"Abstract"、"Contents" 等加粗/大字号行）完全检测不出。`rebuild()` 在正则未命中时增加样式特征判定：

1. **统计正文基准字号**：先遍历全文档 rich 行的 font-size 众数（正文基准，`maxRichFontSize` 取行内最大字号）。
2. **标题判定**（正则未命中时）：
   - 字号 ≥ 基准 × 1.15 → 大字标题（章节/文档主标题）
   - 或 短行（≤60 字符）加粗（rich 含 `<b>`，如 "Abstract"/"Contents"）
3. **目录条目过滤**：样式命中的行若「数字开头 + 行尾紧贴页码」（如 "1 Introduction4"）→ 排除（正文章节标题无尾随页码）。

原正则匹配逻辑保留（txt/md 场景不回归）。验证：p2899r1.pdf 打开后章节面板显示「共 8 章」（之前 0）。

## 3. API

```cpp
class ChapterService : public QObject {
    Q_OBJECT
public:
    explicit ChapterService(QObject *parent = nullptr);
    void setDocument(DocumentModel *model);

    Q_INVOKABLE void setChapterPattern(const QString &regex);
    Q_INVOKABLE QString chapterPattern() const;

    Q_INVOKABLE void rebuild();              // 全量重建（打开文档后）
    Q_INVOKABLE int chapterCount() const;
    Q_INVOKABLE QString chapterTitle(int index) const;
    Q_INVOKABLE int chapterStartLine(int index) const;
    Q_INVOKABLE int chapterAtLine(int lineNumber) const;   // 所属章节 index（-1 无章节）
    Q_INVOKABLE QStringList chapterTitles() const;

signals:
    void chaptersChanged();
};
```

## 4. 行为

- `rebuild()`：遍历 `DocumentModel::lineCount()` 行，正则匹配标题行 → 章节列表（标题行号 + 标题文本）
- `chapterAtLine`：二分查找最后一个 startLine ≤ line
- 行插入/删除后的增量更新：本轮提供 `rebuild()`（文档打开/手动触发）；增量监听留待后续（documentmodel 已提供 insert/remove 信号，可扩展）
- 与翻译集成（后续 UI）：分块边界优先章节边界

## 5. 测试（tst_chapter）

- 默认模式识别中文"第X章"、Markdown `#` 标题
- chapterCount / chapterTitle / chapterStartLine
- chapterAtLine 归属正确
- 自定义模式（如纯数字标题）生效
