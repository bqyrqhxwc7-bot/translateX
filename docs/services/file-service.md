# 多格式文件 + 行内批注（.trx 核心）设计文档 v2

> 状态：设计中（2026-08-12，v2 细化，待确认点以 ✅ 标注）
> 关联：`DocumentModel` / `CommentService` / `DocumentManager` / `qml/TranslateHomePage.qml`
> 原则（AGENTS.md）：文档先行 → 用户确认 → 小步实现

## 1. 定位与能力分层（用户已确认）

应用核心 = **行内批注/翻译写作工具，不是 Word 替代品**。

| 格式 | 定位 | 能力 |
| --- | --- | --- |
| `.txt` | 完美编辑 | 全文可编辑、批注、翻译 |
| `.trx`（自研） | 完美编辑 | 富文本/图片显示 + 批注层完整往返（默认推荐格式） |
| `.docx` | 残缺导入 | 段落→行 + 行内图片 + 基础格式，**仅显示不完整编辑** |
| `.pdf` | 残缺导入 | 文本按阅读顺序 + 图片，版式近似（已确认非 WYSIWYG） |

- 性能：VS Code 式——常规规模全功能；超大文件降级可强制打开
- 允许第三方库（按需、评估许可）
- 批注/翻译保留行号语义

---

## 2. 核心模型：行 + 显示层（详细）

`DocumentModel` 每行扩展角色：

```
Line = { text: string          // 编辑层（权威，用户可编辑）
         display: "plain" | "rich" | "image"   // 渲染模式
         rich?: string         // HTML 片段（rich 模式显示层）
         imageIds?: string[]   // 引用 meta.images 中的图片（image 模式）}
```

**三种行渲染（ListView delegate 分支）：**

| 模式 | 渲染 | 进入编辑时 |
| --- | --- | --- |
| `plain` | 现有 TextEdit（可编辑） | — |
| `rich` | `Text { textFormat: Text.RichText }`（只读显示） | 切换到 plain TextEdit（**rich 显示层丢弃**，编辑即降级纯文本） |
| `image` | 居中 Image（等比缩放）+ 可选 caption 文本；点击放大 | 只读；删除行=删除整行（含图片） |

- ✅ **确认点 A**：富文本行"编辑即降级纯文本"是否接受？（保证编辑语义简单，不做富文本编辑）
- ✅ **确认点 B**：图片行是否允许"转为纯文本行"（删图保留 caption）？

**DocumentModel 扩展**（最小侵入）：
- 新增角色：`DisplayRole`、`RichTextRole`、`ImageIdsRole`
- 新增方法：`setLineDisplay(i, mode)`、`setLineRich(i, html)`、`setLineImages(i, ids)`
- 现有行模型/虚拟化/撤销重做逻辑不变（display 层不参与 undo）

---

## 3. `.trx` 格式（v1 详细 schema）

```json
{
  "trx": "translateX",
  "version": 1,
  "meta": {
    "sourceFile": "原文件名或空",
    "sourceFormat": "txt | docx | pdf | trx | empty",
    "importedAt": "ISO8601",
    "font": { "family": "", "size": 0 },
    "images": [
      { "id": "img1", "format": "png", "mode": "inline", "dataBase64": "..." },
      { "id": "img2", "format": "jpg", "mode": "external", "file": "img2.jpg" }
    ]
  },
  "lines": [
    { "text": "第一章：开始" },
    { "text": "这是富文本", "display": "rich", "rich": "<b>这是</b>富文本", "imageIds": ["img1"] },
    { "text": "配图", "display": "image", "imageIds": ["img2"] }
  ],
  "comments": { "0": "章节标题批注", "1": "译文批注" }
}
```

规则：
- **图片统一存 `meta.images` 表**（去重）；单张 ≤1MB 内嵌 base64（`inline`）；超限降级 `external`：复制到 trx 同目录 `*.images/` 子目录，`file` 相对引用
- `text` 永远是编辑层；`rich`/`display` 为显示层（用户编辑后清除）
- 纯文本行只需 `text`（兼容 .txt 往返）
- `version` 预留迁移
- ✅ **确认点 C**：图片"1MB 内嵌 / 超限外部"阈值是否合理？或全部内嵌（单文件自包含优先，可接受体积大）？

---

## 4. 批注锚定与编辑 UI

- **锚定**：保持行号（`CommentService` 不变，insert/remove 自动 shift）
- **批注编辑**：`FluContentDialog`（标题=行内容预览截断，正文=`FluMultilineTextBox`，按钮 保存/删除/取消）
- 行内标记：保留现有批注行底色；批注行右侧加「批注」小标记（可点击打开编辑）
- ✅ **确认点 D**：批注是"每行一条"（替换式，简单）还是"支持多条/富文本"？（建议：每行一条纯文本，够用且迁移零成本）

---

## 5. 编辑器补齐（阶段 A 清单，详细）

**右键上下文菜单（FluMenu）**：
```
剪切 / 复制 / 粘贴
─────────────
插入行（上方）    Ctrl+Shift+Enter
插入行（下方）    Ctrl+Enter
删除行            Ctrl+Shift+K
─────────────
添加批注          Ctrl+Shift+C
编辑批注
删除批注
─────────────
字号 ▸  12/14/16/18/20/24/28
字体 ▸  宋体/微软雅黑/黑体/等线/Consolas/等宽（常用子集）
```

- 菜单项按上下文 enabled（无选中行禁编辑/删除/批注项；无批注禁编辑/删除批注）
- 字号/字体：**作用于全文默认（meta.font）与当前行**（当前行 text 层包 `<font size family>`）✅ 确认点 E：是否仅"全文默认 + 当前行"，不做行内混排格式？
- 受限：NoStack 下 `FluComboBox` 不可用 → 字体/字号一律用 **FluMenu 子菜单**（已可用）

**行操作函数**（复用现有）：`splitCurrentLine`（Enter 已有）、`mergeWithPrevious`（Backspace 已有）；新增 `insertLineAbove/Below`、`deleteCurrentLine`

---

## 6. FileService 接口（详细）

```cpp
class FileService : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool open(const QString &path);                  // 按扩展名分发解析器 → DocumentModel + 批注层
    Q_INVOKABLE bool save(const QString &path);                  // 按扩展名写（含导出三态）
    Q_INVOKABLE bool saveAs(const QString &path, const QString &format);
    Q_INVOKABLE QStringList recentFiles() const;
    Q_INVOKABLE QString openFilters() const;                     // "文本文件 (*.txt);;翻译文档 (*.trx)…"
    Q_INVOKABLE QString saveFilters() const;
signals:
    void fileOpened(const QString &path, const QString &format);
    void fileSaved(const QString &path);
    void importFailed(const QString &path, const QString &error);
};
```

**解析器接口（插件化，C++ 侧注册）**：
```cpp
struct ParseResult { QStringList lines; QVariantMap richMap; QVariantMap imageMap; QVariantMap comments; QVariantMap meta; };
class IFileParser {
public:
    virtual QStringList extensions() const = 0;
    virtual bool parse(const QString &path, ParseResult &out) const = 0;   // 读
    virtual bool write(const QString &path, const DocumentModel &doc,
                       const CommentService &comments, ExportMode mode) const = 0; // 写
};
```
- 现有 `DocumentManager`：迁移为 FileService 的 `.txt` 解析器（保留其 TXT 逻辑），QML 改调 FileService；旧接口逐步废弃
- 翻译/批注/章节/查找：零耦合（只依赖 DocumentModel）

---

## 7. 导出三态

`ExportMode { Original / WithComments / Full }`（原文 / 批注=原文+译文 / 全部=原文+译文+编辑后正文差异由调用方决定）

- `.txt`：纯文本，图片输出 `[图片]` 占位
- `.pdf`：QPdfWriter 文本+图片，批注作段落尾部附注
- `.docx`：第三方写库（阶段 B 选型）
- `.trx`：完整往返

---

## 8. 大文件降级（VS Code 式）

- 打开时检测：行数 > 5 万 或 体积 > 200MB → **受限模式**：禁 rich/image 渲染（回退纯文本）、禁批注编辑、禁翻译预览；保留编辑/滚动/查找/章节
- 顶部提示条提示"大文件受限模式"
- ✅ **确认点 F**：阈值（5 万行 / 200MB）是否合适？

---

## 9. 阶段计划（修订，按核心价值排序）

| 阶段 | 内容 | 产出 |
| --- | --- | --- |
| **A1** | 批注增删改（核心价值最先）：右键菜单批注项 + 编辑对话框 + 行标记 | 批注可手动维护 |
| **A2** | 编辑补齐：插入/删除行 + 剪切/复制/粘贴 + 字号/字体 | 编辑器基本可用 |
| **A3** | `.trx` 读写：DocumentModel 扩展 + TrxParser + 打开/另存为（.trx 默认推荐） | 格式闭环 |
| **B** | FileService + `.docx` 导入（库选型先 Ask） | docx 可导入 |
| **C** | `.pdf` 导入 + `.pdf`/`.docx` 导出 | 导出闭环 |
| **D** | 大文件降级 + 全量测试 + 文档/记忆收尾 | 上线质量 |

---

## 10. 已确认决策（2026-08-12，用户确认）

- ✅ A：富文本行仅显示，纯文本行才可编辑（编辑富文本行 → 降级为纯文本）
- ✅ B：图片行可「转为纯文本行」（删图保留 caption 文字）
- ✅ C：图片**全部内嵌 base64**（单文件自包含优先，接受体积大）
- ✅ D：批注**每行一条纯文本**（与现有翻译批注一致，迁移零成本）
- ✅ E：字号/字体=**全文默认 + 当前行**；「行内混排格式」已记入开发计划（后续再说）
- ✅ F：大文件受限阈值 **5 万行 / 200MB**
- 分支：main=QML 版，`widgets` 分支=旧 Widgets 版（2026-08-12 拆分）
- 待选型（阶段 B 再 Ask）：docx / pdf 第三方库与许可证
