# docx 导出批注（迭代2）设计

> 状态：✅ 已实现（2026-08-17）
> 路线图：HANDOVER.md §3 迭代2（用户已拍板 inline/native 两种都做）
> 关联：`docs/services/file-service.md` §7（导出三态）、§11（docx 导入）

## 1. 目标

`.docx` 由"仅导入"补全为"导出对称"：导出 = 原文行 + 译文（批注）行，译文呈现方式由配置 `docxCommentStyle` 决定：

- `inline`：译文内联到原文段落，**黄色高亮**（Word 内置 highlight 效果），便于直接阅读/二次编辑
- `native`：译文写成 **Word 原生批注**（`w:comment`），原文保持干净，批注可折叠/审阅

## 2. 语义与边界

- 数据源：`lineText(i)`（原文编辑层）+ `commentAt(i)`（译文，CommentService 单一数据源）
- 有批注的行 = 带译文；无批注的行 = 仅原文段落
- 等价 `ExportMode::WithComments`（原文+译文）；`Original`（纯原文）模式后续再补
- **富文本/图片不导出样式**：rich 行导出编辑层纯文本；image 行导出 `[图片]` 占位文本（与导入端 rich 混排占位一致）；显示层降级语义同步
- 空行 → 空段落

## 3. 接口（与 DocxParser::read 同构）

```cpp
class DocxParser {
public:
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);
    // style: "inline" | "native"
    static bool write(const QString &path, DocumentModel *model,
                      CommentService *comments, const QString &style,
                      QVariantMap *meta = nullptr, QString *error = nullptr);
};
```

- `DocumentManager::writeDocument` 追加 `.docx` 分支：`isDocx(path)` → 读 `configService.get("translation", "docxCommentStyle")` → 调 `DocxParser::write`
- 样式名走配置；QML 侧 saveFileAs 对话框 `saveFilters` 已含 `Office 文档 (*.docx)`（导入时已加，另存为共用）

## 4. 输出 docx 结构（最小 Word 文档）

```
[Content_Types].xml
_rels/.rels
word/document.xml
word/comments.xml          ← native 模式
word/_rels/document.xml.rels ← native 模式
```

- 写 zip：`QuaZip zip(path); zip.open(QuaZip::mdCreate)` + `QuaZipFile`（与 read 的 mdUnzip 对称）
- XML：全部用 `QXmlStreamWriter`（自动转义 `& < >`），每行一个 `<w:p>` 段落

### 4.1 inline 模式 document.xml

```xml
<w:p>
  <w:r><w:t xml:space="preserve">原文行</w:t></w:r>
  <w:r><w:br/></w:r>
  <w:r><w:rPr><w:highlight w:val="yellow"/></w:rPr><w:t xml:space="preserve">译文行</w:t></w:r>
</w:p>
```

无批注行：仅原文 run（无 br/译文 run）。

### 4.2 native 模式

document.xml 段落（整段原文为批注范围，Word 原生锚定）：

```xml
<w:p>
  <w:commentRangeStart w:id="0"/>
  <w:r><w:t xml:space="preserve">原文行</w:t></w:r>
  <w:commentRangeEnd w:id="0"/>
  <w:r><w:rPr><w:rStyle w:val="CommentReference"/></w:rPr><w:commentReference w:id="0"/></w:r>
</w:p>
```

word/comments.xml：

```xml
<w:comments xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:comment w:id="0" w:author="Translex" w:initials="TL"
             w:date="2026-08-17T00:00:00Z">
    <w:p><w:r><w:t>译文行</w:t></w:r></w:p>
  </w:comment>
</w:comments>
```

配套（native 模式必需，否则 Word 打开报错）：
- `[Content_Types].xml` 加 `<Override PartName="/word/comments.xml" ContentType="...wordprocessingml.comments+xml"/>`
- `word/_rels/document.xml.rels` 加 `comments` 关系（`.../relationships/comments` → `comments.xml`）

### 4.3 公共壳

- `_rels/.rels`：officeDocument 关系 → `word/document.xml`
- `[Content_Types].xml`：rels/xml Default + document.xml/comments.xml Override

## 5. 配置

`src/services/config/translation.json` 追加（ConfigService 只认 schema 内 key）：

```json
{
  "key": "docxCommentStyle",
  "displayName": "docx 导出批注样式",
  "description": "inline=译文内联黄色高亮；native=Word 原生批注（原文干净）",
  "type": "enum",
  "options": ["inline", "native"],
  "default": "inline",
  "group": "导出"
}
```

设置页由 ConfigSectionCard 的 enum 控件自动渲染（无需改 QML）。

## 6. 测试计划（tst_docx）

1. `writeInline`：构造模型（原文+批注若干行）→ inline 导出 → QuaZip 解压 document.xml 断言：
   - 译文文本出现、`w:highlight w:val="yellow"` 存在
   - 无批注行无 highlight
2. `writeNative`：native 导出 → 断言 comments.xml 存在且含译文文本；document.xml 含 `w:commentRangeStart/End/Reference`；Content_Types 含 comments Override
3. `roundTrip`：write（inline）→ read → 断言 lineText/commentAt 与源一致（批注=译文往返保真）

## 7. 限制（明示）

- 不保留 rich 样式/图片（导出为编辑层纯文本 + `[图片]` 占位）
- 无页眉/页脚/页码/分页（Word 默认版式）
- native 批注锚定整段原文（Word 打开可正常显示/审阅）
- `Original`（纯原文导出）未实现，后续按需补
