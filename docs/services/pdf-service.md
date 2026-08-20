# PDF 导入/导出（PdfParser）设计文档 v1

> 状态：设计定稿（2026-08-15）· 阶段 C 任务
> 决策：**库选型 QPdf + QPdfWriter（Qt 自带，用户已确认 2026-08-15）**——零依赖、无许可风险（Qt 许可范围内）、Qt6Pdf 已随部署（`D:/Software/Qt/6.5.3/msvc2019_64` 含 Qt6Pdf/Qt6PdfWidgets lib + cmake 配置）。
> 关联：`DocumentModel` / `CommentService` / `DocumentManager` / `QPdfDocument`（QtPdf）/ `QPdfWriter`（QtGui）
> 定位：**.pdf 残缺导入 + 导出**（文本按阅读顺序，版式近似，非 WYSIWYG——见 file-service.md §1）

## 1. 能力边界

| 方向 | 能力 | 限制 |
| --- | --- | --- |
| 导入 | 每页一行文本（按阅读顺序）；页数映射保留 | **无 OCR**：扫描版 PDF 无文本层 → 行文本为空（提示用户）；忽略页眉/页脚/分栏版式/超链接/内嵌图 |
| 导出 | 编辑层文本按行重建为文本页，自动分页 | 无原版式还原（非 WYSIWYG）；批注以「（批注：xxx）」尾注追加 |
| 批注 | 导入时清空（与 docx 一致）；导出时行尾附注 | 无独立批注层写入 |

## 2. 导入设计（QPdfDocument）

### 2.1 流程

1. `QPdfDocument doc; doc.load(path)` → `Error`：
   - `FileNotFound` → 「文件不存在」；`InvalidFileFormat` → 「不是有效的 PDF 文件」；`IncorrectPassword` → 「PDF 已加密，暂不支持密码文件」（v1 不实现密码输入）；其他 → 通用错误
2. `pageCount()` → 逐页 `getAllText(page).text()`
3. 文本规范化：每页 = 一行
   - 页内 `\n`/连续空白 → 压缩为单个空格（模型行是单行文本，TextEdit 不渲染换行）
   - 首尾 trim；空页 → 空行（**保留页映射**，导出时可逆）
4. `model->setLines(lines)`；`comments->clear()`；`meta.sourceFormat = "pdf"`、`meta.sourceFile = 文件名`

### 2.2 显示层决策

**v1 不渲染页图像进显示层**：
- 文本层已经提取，页面图像对「翻译/批注」核心价值无增量
- `render()` 全量渲染慢、内存/trx 体积暴涨（A3 图片 1MB 内嵌策略下更糟）
- 页文本为空的页 → 显示层留空（用户可见空行，状态栏提示「扫描版 PDF 无文本层」）

> 后续可选：按需渲染当前页图像（懒加载），届时复用 A3 display=image 层。

## 3. 导出设计（QPdfWriter + QPainter）

### 3.0 ⚠️ Qt 6.5.3 文本层缺陷（已修复，v2）

**缺陷详情（2026-08-16 深挖确认）**：QPdfWriter 导出 PDF 的文本层：
- ToUnicode CMap 结构存在且 ASCII 映射完全正确（"Second" 可完整还原）
- **中文被映射到康熙部首区（U+2F00 系列）而非 CJK 汉字区（U+4E00）**——字体子集化时 cmap 取错子表（Qt 缺陷），**所有阅读器**提取中文都错（非 QPdfDocument 读取 bug）
- 内容流文字操作符为 CID（Identity-H，2 字节），**CID 按"文本字符首次出现顺序"分配且复用**；**空格也有 CID**（U+0020 映射）
- 内嵌子集字体**无 cmap 表**（OS/2,glyf,head,hhea,hmtx,loca,maxp,name——子集化时被剔除），无法从字体侧恢复映射
- 一页多行文本提取后天然合并为一行（行结构丢失，独立于乱码问题）

**v2 修复（2026-08-16 已实现，用户确认完整修复往返）**：

| 缺陷 | 修复 |
| --- | --- |
| 中文映射错（康熙部首区） | 导出后处理：解析内容流 CID 序列（Tj 顺序含重复）↔ 与导出文本序列对齐（CID 首次出现分配、重复复用、空格有 CID 的实测规律），重建正确 ToUnicode CMap，重写 PDF（%PDF 头 + 对象按编号重序列化 + 重建 xref；xref 权威定位对象、/Length 支持间接引用） |
| 一页多行合并 | **每行一页**：每行（含空行）后 `newPage()`，再导入时每页=一行，行结构保真 |

对齐算法（`buildCMapFromText`）：逐字符推进 CID 与文本，新 CID 映射当前字符、旧 CID 必须匹配已映射字符（不匹配时跳过文本字符防御）；**对齐失败 → 放弃修复**（保留原始输出，不破坏文件）。

限制：
- 对齐依赖 QPdfWriter 的 CID 分配规律（实测稳定）；多字体 fallback 场景对齐失败时跳过修复
- 修复后文本层对**所有阅读器**可提取；再导入 = 行结构 + 内容完整往返（含中文/英文/批注尾注/空行）

### 3.1 流程（v2：每行一页 + CMap 后处理）

1. `QPdfWriter writer(path)`：A4 + 边距 20mm（`setPageSize(QPageSize(QPageSize::A4))`、`setPageMargins(QMarginsF(20,20,20,20), QPageLayout::Millimeter)`）、`setTitle/setCreator("Translex")`
2. `QPainter painter(&writer)`；字体 **DengXian 12pt**（须为可嵌入 TTF，见 §3.2）
3. 逐行绘制，**每行一页**（保证再导入行结构保真）：
   - 行文本（含批注尾注）→ 折行绘制 → `writer.newPage()`
   - 空行 → 直接 `newPage()`（空白页，再导入为空行）
   - 绘制同时**收集文本字符序列**（去空格）供 CMap 重建
4. `painter.end()` 后调用 **文本层修复**（见 §3.3）：重建 ToUnicode CMap + 重写 PDF

### 3.2 说明

- `writer.width()/height()` 返回**可用区像素**（页尺寸 - 边距），直接用
- 字体：**DengXian（等线）TTF**——不能用 TTC 集合字体（微软雅黑/宋体），TTC 嵌入退化为字形路径更糟；DengXian 是 Windows 10+ 自带 TTF，视觉现代
- 导出内容是**编辑层文本**（用户编辑后的结果），与导入页映射无关（行数可能已变）

### 3.3 文本层修复（ToUnicode CMap 重建）

导出后处理（`PdfParser` 内实现，约 150 行 + zlib 解压）：

1. **解析 PDF 对象**：`N M obj <<dict>> stream...endstream endobj` 模式扫描（QuaZip 自带 zlib 可解压 FlateDecode 流）
2. **定位 ToUnicode 对象**：从字体字典 `/ToUnicode N 0 R` 引用找到目标对象
3. **生成正确 CMap**：`<0001> <0014> [<7B2C> ...]` 形式 bfrange——
   - CID 序列 = 内容流中按序出现的 CID（严格递增且复用）
   - 字符序列 = 导出时收集的文本字符（**去空格**，与内容流对齐；已实测空格无 CID）
   - 逐字符建立 CID→Unicode，同 CID 复用同一字符
4. **重写 PDF**：替换 ToUnicode 对象内容 → 所有对象重新序列化 → 重建 xref 表 + trailer
5. **防御**：CID 数与字符序列数不一致（多字体 fallback/缺字）→ 放弃修复（保留原始 CMap），写日志警告，不破坏 PDF

## 4. 接口（与 TrxParser/DocxParser 同构）

```cpp
class PdfParser {
public:
    // 读：每页一行填充模型，清空批注，回填 meta（sourceFormat=pdf）。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);
    // 写：编辑层文本 → PDF 文本页（自动分页，批注尾注）。
    static bool write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta, QString *error = nullptr);
};
```

## 5. 集成

- `DocumentManager::openFile`：新增 `isPdf()` 分发 → `PdfParser::read`（失败 `operationFailed`，成功设置 m_path/dirty/recent）
- `DocumentManager::writeDocument`：`.pdf` → `PdfParser::write`
- `qml/TranslateHomePage.qml`：打开对话框 `nameFilters` 追加 `PDF 文档 (*.pdf)`；另存为对话框追加 `PDF 文档 (*.pdf)`
- `CMakeLists.txt`：`translex` 目标 + `tests/CMakeLists.txt` 的 `translex_services` 静态库 `SERVICE_SOURCES` **两处**注册 `pdfparser.cpp`；`find_package(Qt6)` 组件追加 `Pdf`（顶层 + tests）

## 6. 测试计划（tests/tst_pdf.cpp，16 个测试目标之一）

| 用例 | 断言 |
| --- | --- |
| 手写最小 PDF（Type1 Helvetica + WinAnsi 干净文本层）→ read | 行数 = 页数、页内多行合并单行、空页保空行、meta.sourceFormat=pdf |
| 长文本页（页内多行折行合并） | 页内换行压缩为空格、无残留 \n |
| exportProducesValidPdf | 导出文件可加载（Error::None）、pageCount=1、渲染暗像素 >500（视觉冒烟，含 CJK） |
| 错误：文件不存在 / 非 PDF 内容 | read 返回 false 且 error 非空 |
| writeEmpty（0 行导出） | 不崩溃、文件存在 |
| sampleFile 回归 | `samples/demo.pdf`（手写干净文本层）存在则 read 成功且内容可提取；缺失时自动生成后入库 |

> 不用 QPdfWriter 生成夹具：其文本层缺陷（§3.0）会让导入断言失效。

## 7. 限制（明示）

- 无 OCR（扫描版仅空行）；无密码 PDF（v1 报错提示）
- ~~导出文本层不可提取~~ **已修复（v2）**：导出后自动重建 ToUnicode CMap（§3.0/§3.3），文本可提取、可再导入，往返完整
- 版式近似：无原字体/字号/颜色/分栏还原，统一 DengXian 12pt 导出
- 导出不含原图（仅文本层 + 批注尾注；批注尾注随文本层修复后可提取）
- 超大 PDF（页数巨多）走阶段 D 大文件降级策略（打开时行数探测复用）

## 8. 验证清单

- `cmake --build build-vs2026-x64 --config Debug` 通过
- `ctest -R tst_pdf` 全绿；全量 `ctest` 不回归
- 应用打开/另存为对话框含 `.pdf`；手动打开 demo.pdf 验证页→行

---

# v3：富文本导入/导出（2026-08-19）

> 状态：**已实现并全部验证通过**（2026-08-19）· 决策：用户已确认方案 A（纯 Qt 零新依赖，自研内容流解析）
> 决策：**不引入 Poppler/MuPDF**（GPL/AGPL 许可 + Windows 构建成本）；**自研 PDF 内容流解析器**提取样式/图片/链接（复用 v2 的 `parsePdfObjects`/`inflateStream` 基础设施）；**导入生成 trx 临时文件**持久化富文本信息；**导出尽力恢复，恢复不了的告警用户**。
> 目标：PDF 导入保留富文本与图片（占位、不可编辑，编辑降级纯文本——复用 docx 的 rich/image 显示层机制），尽量保留原文结构与格式（字体/字号/颜色/粗斜体/链接）；导出 PDF 恢复富文本/图片/链接。
> 本小节为**实现后回填**（原为设计，与实现有出入处已按实现现状修正，见 §9.8 踩坑记录）。

## 9.1 能力边界（v3 增量）

| 方向 | 能力 | 限制 |
| --- | --- | --- |
| 导入 | 文本视觉行 + **样式**（字体名→CSS 通用族/字号 pt/颜色/粗斜体）+ **行首缩进** + **嵌入图片**（占位行/纯图行，图文混排每图一占位）+ **链接**（行内 `<a>`，**渲染侧可点击**） | 无 OCR；内容流解析失败回退纯文本按行导入；分栏版式按阅读顺序近似 |
| 导出 | 编辑层文本 + **富文本样式恢复**（drawContents）+ **图片恢复**（纯图行 `drawImage`）+ **链接**（仅视觉样式） | 非 WYSIWYG；**链接可点击注释未实现**；恢复不了的项写 **exportWarnings 告警** |
| 中间格式 | 导入成功后自动写 **trx 临时文件**（`DocumentManager::writePdfTrxTemp`，含 rich/images，复用 TrxParser::write） | 临时文件放系统临时目录，不污染文档目录；失败静默；应用退出不清理 |

## 9.2 导入：自研内容流解析器

入口：`PdfParser::read` → `importRichPdf`。解析基于 v2 的 `parsePdfObjects`（xref 权威定位对象边界）与 `inflateStream`。

### 9.2.1 对象与内容流解析

- **`parsePdfObjects`（v2 已有）**：xref 权威定位对象边界；对象头锚定 `\bN\s+0\s+obj`；dict 深度匹配 `<< >>`（**紧凑格式 PDFium 的 dict 与对象头同行**，从 capturedEnd 查找）；流按 `/Length` 精确截取；无流对象也 append（raw 非空，供间接数组解引用）。
- **`parseContentStream`**：内容流 lexer 支持 `BT`/`ET`/`Tf`/`Td`/`TD`/`TJ`/`Tj`/`'`/`"`（自动字距）/`rg`/`RG`/`g`/`G`/`k`/`K`/`cm`/`q`/`Q`/`Do`。
- **TJ 数组**：字符串 → run；数字 `|n|>100` 视为词间距（加空格），小值忽略（TeX kern）。
- **视觉行分组**：按 y 坐标分组、x 坐标排序（`groupIntoLines`）。
- **视觉行碎片合并（2026-08-19，`mergeLineFragments`）**：`groupIntoLines` 后调用。PDFium 等重写器把单词/短语拆到相邻视觉行（y 微差 + x 连续）→ 出现 "uction4" 这类孤立残片行。判据：相邻行 y 差 < 1.5 行高 且 下一行首 x 与上一行尾 x 连续（0 < 间距 < 0.5em）→ 拼接为同一逻辑行；渲染折行后下一行 x 回到行首，不满足连续性，不会误并真换行。p2899r1 行数 342 不变（无碎片，零回归）。

### 9.2.2 字体（ToUnicode 映射）

| 字体类型 | 支持 |
| --- | --- |
| Type0（CIDFontType2 + Identity-H/CMap） | 2 字节码空间 |
| Type1 简单字体 | **1 字节码空间**（`codeBytes` 字段：Type0=2 其余=1） |

- **ToUnicode CMap 解析**：支持 `bfchar` / `bfrange`（**区间形式与数组形式** `<lo> <hi> [<d0> <d1> ...]`，即 QPdfWriter 生成器格式）。
- **TeX CMap 的 U+2423（␣）归一化为空格**。

**标准编码表（2026-08-19，□ 根治）**：无 ToUnicode 的简单字体曾回退 `QChar(code)`——0x80-0x9F 是控制字符/私有区，直接当 Unicode 显示成 □（长方形符号），部分内容显示异常。修复：

- `FontInfo` 新增 `encoding` 字段（`PdfEncoding: Unknown/WinAnsi/Standard/MacRoman`），字体 dict 解析 `/Encoding` 指定（`/WinAnsiEncoding`/`/MacRomanEncoding`/`/StandardEncoding`）。
- `decodeText`：**CMap miss / 无 CMap 的 1 字节码** → `standardEncodingChar` 查标准编码表——
  - **WinAnsi 0x80-0x9F 特殊区**：`€ ‚ ƒ „ … † ‡ ˆ ‰ Š ‹ Œ Ž ' ' " " • – — ˜ ™ š › œ ž Ÿ`
  - **StandardEncoding 0xA0-0xBF 特殊区**（PDF 32000-1 Table 5.9/5.10）：不连字符/变音符（breve/caron/circumflex/dotaccent/hungarumlaut/ogonek/ring/tilde/diaeresis）、ø/Lslash/OE/AE 等
  - **MacRoman 0x80-0x9F 拉丁变音**（Ä/Å/Ç/é/ñ/ü 等）；0x20-0x7E ASCII 直通、0xC0-0xFF Latin-1 直通
  - **Unknown 时 0x80-0x9F 走 WinAnsi 兜底**（多数实际 PDF 为 WinAnsi 系）
- **仍未映射的码 → 跳过**（不产生控制字符/□）；CID 字体（Type0）miss 保留 `QChar(code)` 尽力（罕见）。
- 通用过滤：解码结果若是控制字符（0x80-0x9F）/私有区（0xE000-0xF8FF）/替换符（U+FFFD）→ 跳过。

**字体名映射 + pt 字号（2026-08-19 可读性改进）**：
- **`mapPdfFontFamily`**：PDF 原始字体名（LMRoman/Helvetica/Courier/SimSun 等）映射到 CSS 通用族 `serif`/`sans-serif`/`monospace`（Latin Modern/Computer Modern/Times/宋体/仿宋/楷体/思源宋体 → serif；Courier/Consolas/Cascadia 等 → monospace；Helvetica/Arial/黑体/雅黑/等线等 → sans-serif）；**未知字体不写 family**（应用默认）。原因：嵌入 PDF 的字体名在用户机器上多不存在，直接写入 rich 会让渲染 fallback 到任意字体。
- **字号单位 pt**：rich HTML 的 `font-size` 从 `px` 改为 `pt`（PDF 字号本来就是 pt 单位；之前 9.96pt 写成 9.96px 偏小，影响可读性）。

### 9.2.3 图片提取

- 资源字典 `/XObject` `/Subtype /Image`，`/Do` 时绘制：`/Filter /DCTDecode` → JPEG 字节原样；`/FlateDecode` → 解压原始像素（RGB/Gray/CMYK）→ PNG。
- `meta.images[{id, format, mode:"inline", dataBase64}]`（与 docx 一致格式）。
- **纯图页（vlines 空）→ 虚拟空行承载图片（display=image）**；图文混排行 → rich 尾 **每张图一个** `[图片]` 占位（2026-08-19 起多图行不再丢图；与 docx 一致，渲染侧按 imageIds 顺序替换为真实 `<img>`，见 §9.9）。

### 9.2.4 链接提取

- 页面 `/Annots` `/Subtype /Link`（常规）+ **PDFium 重写格式的孤岛注释**（无 `/Annots` 挂接，按页面对象号区间归页）。
- rect 与视觉行 y 重叠 → 行内 `<a href>`。

### 9.2.5 间接数组 Contents（⚠️ 本轮修复的 bug）

`/Contents 228 0 R` 指向数组对象 `[ 33 0 R ]`（PDFium 重写格式）。该数组对象可能位于对象头**下一行**（raw 以 `[` 开头）或**与 obj 同行**（raw 丢失 → 用 `PdfObject.off` 取完整对象字节，截到 endobj，防误匹配下一对象）——**两种形态都要支持**。

### 9.2.6 降级策略

- `importRichPdf` 失败（含「PDF 无文本内容」）→ 回退 `getAllText` 纯文本按行导入（不阻塞打开）。
- 受限模式（大文件）→ 跳过富文本提取（纯文本，与 docx 一致）。
- 编辑行 → rich 降级纯文本（现有机制，见 HANDOVER.md §4；2026-08-19 起 `DocumentModel::updateLineText` 在模型层同步清空显示层，见 §9.9）。

### 9.2.7 行首缩进与回退路径保真（✅ 2026-08-19 可读性改进）

- **行首缩进**：页面左边界取「该页 run x 的众数」（正文列基线，避免居中标题/页眉被误判）；行首 x 偏移 > 0.25em 时按 0.5em/空格换算缩进（上限 6 空格，防居中排版过度）；`linePlainText` 前置空格、`lineRichHtml` 前置 `&nbsp;`（HTML 连续 nbsp 不合并，Text 渲染保真）。
- **getAllText 回退路径**：`normalizeLineText` 由 `trimmed()` 改为**保留行首空白、仅去行尾**（段落/代码缩进保真）。

## 9.3 trx 临时文件（✅ 已实现）

- PDF 导入成功后自动写 `%TEMP%/Translex/<pdf文件名>.trx`（`DocumentManager::writePdfTrxTemp`，复用 `TrxParser::write`，模型含 rich/images）。
- 失败静默；应用退出不清理。
- 用途：富文本信息（样式/图片/链接）的持久化载体；用户可手动打开 trx 恢复富文本。

## 9.4 导出：富文本恢复 + 告警（✅ 已实现，除链接注释）

入口：`PdfParser::write`。文本层修复（`repairTextLayer`：聚合内容流 CID 与 textSeq 对齐重建 ToUnicode CMap）保留（v2）。

1. 每行一页（行结构保真）。
2. **rich 行**：`QTextDocument setHtml` + **`drawContents(&painter)`**——注意 **`print(&writer)` 会在 painter 已 begin 时重复 begin 失败**，必须用 `drawContents`（见 §9.8 踩坑）；`[图片]` 占位替换为 `<img src="data:image/...;base64,...">`（从 `meta.images` 取）。
3. **纯图行**（display=image）：`painter.drawImage` 渲染；图片缺失/解码失败 → 告警。
4. **告警**：收集到 `meta["exportWarnings"]`（QStringList，如「第 N 行图片缺失」）。
5. **链接可点击注释**（/Annots 写回）：**未实现**（保持视觉样式，不写注释对象）。

## 9.5 测试计划（tst_pdf 扩展，✅ 16 用例全绿）

| 用例 | 断言 |
| --- | --- |
| ✅ `compactRichPdf` | 紧凑格式 + 1 字节 CMap + TJ 字距 + 孤岛链接 |
| ✅ `readQpaintImagePdf` | QPdfWriter bfrange 数组 + JPEG + `[图片]` 占位 |
| ✅ `readFlateImagePdf` | FlateDecode 纯图行 |
| ✅ `exportRichRoundTrip` | 富文本往返保真 |
| ✅ `exportImageRoundTrip` | 图片往返保真 |
| ✅ `readWinAnsiPdf` | 无 ToUnicode 简单字体按 `/Encoding` 解码：WinAnsi 0x85/0x96/0x97/0x93/0x94 → … – — “ ” |
| ✅ `readCmapMissSkipsBadCode` | 1 字节 CMap miss 码（0x90）→ 跳过，输出 "Hi"（不产生控制字符/□） |
| ✅ 复杂 PDF 解析失败 | 回退纯文本按行导入（不失败） |
| ✅ 导出告警 | 图片缺失时告警列表非空 |

- **测试夹具教训（2026-08-19 补充）**：`readWinAnsiPdf`/`readCmapMissSkipsBadCode` 夹具必须用 `buildPdf`（**带 xref**）——无 xref 的夹具会走 `getAllText` 回退（pdfium 提取），测不到 `decodeText`。
- **测试夹具教训**：生成 PDF FlateDecode 数据**不能用 qCompress**（输出带 4 字节长度前缀，非纯 zlib）——夹具用 python `zlib.compress` 输出的固定字节（见 §9.8 踩坑）。
- `tst_documentmanager` 21 用例（新增 `pdfImportWritesTrxTemp`）。
- 全量 ctest 16/16 通过。
- **真实验证**：`samples/p2899r1.pdf`（342 行全 rich、5 行 `<a href` 链接、ń/— 精确、**章节面板「共 8 章」——之前 0**）、`testimg.pdf`（2 张 JPEG + 图文混排）、`flateimg.pdf`（1 张 png 纯图行）。

## 9.6 限制（明示）

- 样式保真度依赖内容流解析质量：Type1/TrueType 字体名映射、颜色空间（DeviceRGB/CMYK）覆盖；复杂渲染（透明度/渐变/矢量图）不提取
- 图片为嵌入位图（JPEG/PNG 原始数据），矢量图（/Form XObject）不提取
- **链接可点击注释未实现**（仅视觉样式保留，不写 /Annots 注释对象）
- 分栏/页眉页脚按阅读顺序近似（与 v1 一致）

## 9.7 验证清单（✅ 已完成）

- `cmake --build build-vs2026-x64 --config Debug` 通过；`ctest -R tst_pdf` 全绿；全量 `ctest` 不回归
- 打开 samples/p2899r1.pdf：样式/图片/链接可见；编辑行降级纯文本
- **2026-08-19 可读性改进（截图验证）**：字体映射（serif/sans-serif/monospace）、pt 字号、行首缩进；富文本行链接点击、图文混排图片内嵌渲染；编辑降级后 .trx 往返无旧样式错位
- **2026-08-19 显示修复（截图验证）**：无 ToUnicode 简单字体按 `/Encoding` 标准编码表解码（0x85/0x96 等 → … – 等，□ 消失）；视觉行碎片合并（p2899r1 行数 342 不变，零回归）；章节面板「共 8 章」（之前 0）
- 导出 PDF：富文本/图片恢复；告警正确
- 导入生成 trx 临时文件；手动打开 trx 恢复富文本

## 9.8 踩坑记录（实现期）

- **`QTextDocument::print` 与 painter 冲突**：导出富文本行时，`print(&writer)` 会在 painter 已 begin 时**重复 begin 失败**——必须用 `drawContents(&painter)` 渲染到 QPdfWriter。
- **qCompress 非纯 zlib**：测试夹具生成 PDF FlateDecode 数据不能用 `qCompress`（输出带 4 字节长度前缀，非纯 zlib 流），PDF 解析器无法解压；夹具须用 python `zlib.compress` 输出的固定字节。
- **间接数组 Contents 同行形态**：PDFium 重写格式的数组对象可能**与 obj 同行**，raw 字段丢失 → 用 `PdfObject.off` 取完整对象字节截到 endobj，防误匹配下一对象（两种形态都要支持）。
- **`LinkArea` 不存在**（2026-08-19）：QtQuick 无 `LinkArea` 类型，误用会致页面加载失败（`LinkArea is not a type`）——链接点击用 `Text.onLinkActivated: Qt.openUrlExternally(link)`，勿再使用。
- **setLines 先于 meta 赋值的时序问题**（2026-08-19）：openFile 时 `setLines` 先触发 delegate 绑定，`imageSource` 读 `documentMeta().images` 时 meta 尚未赋值 → `[图片]` 替换跑在 meta.images 就绪前、渲染成空。修复：QML 侧新增 `_docVersion`（`documentChanged` 后 +1，此时 meta 已就绪，强制绑定重求值）+ C++ 侧 meta（sourceFormat/images）写入移到 `setLines` 之前（docx 同步对齐；真正的 UI 修复在 QML）。
- **无 ToUnicode 简单字体回退 `QChar(code)` 显示 □**（2026-08-19）：0x80-0x9F 是控制字符/私有区，直接当 Unicode 显示成 □（长方形符号）；修复 = 按 `/Encoding` 标准编码表（WinAnsi/Standard/MacRoman）解码 + 未映射码跳过（见 §9.2.2）。
- **PDFium 重写器把单词拆到相邻视觉行**（2026-08-19）：出现 "uction4" 这类孤立残片行；`mergeLineFragments` 按 y 差 < 1.5 行高 + x 连续（0 < 间距 < 0.5em）拼接回逻辑行，真换行 x 回行首不误并（见 §9.2.1）。
- **测试夹具必须带 xref 才测得到 decodeText**（2026-08-19）：无 xref 的夹具走 `getAllText` 回退（pdfium 提取），`readWinAnsiPdf`/`readCmapMissSkipsBadCode` 等解码用例必须用 `buildPdf`（带 xref）构造。

## 9.9 QML 渲染与编辑降级一致性（✅ 2026-08-19 已实现，截图验证）

渲染侧（`qml/TranslateHomePage.qml`）与模型层（`documentmodel.cpp`）：

- **链接可点击**：rich 行 `Text` 加 `onLinkActivated: Qt.openUrlExternally(link)`——Text 原生响应 `<a href>` 点击；非链接区域点击透传给行选中 MouseArea。
- **[图片] → 真实 `<img>`**：新增 `richTextFor(row)` 把 `[图片]` 占位按 `imageIds` 顺序替换为 `<img src="data:image/...;base64,..." height="60">`（Text 原生支持 img 标签）。
- **meta 时序（`_docVersion`）**：新增页面属性 `_docVersion`，`documentChanged` 时 +1（此时 `documentMeta` 已就绪）并清图片缓存；`imageSource`/`richTextFor` 内读取 `_docVersion` 强制绑定重求值——解决「setLines 先于 meta 赋值触发绑定 → [图片] 替换跑在 meta.images 就绪前」的时序问题（配套 C++ 侧改动见 §9.8）。
- **图片缓存键**：`_imageUriCache` 键改为 `currentPath|imageId`（同一页面内切换文档不串图）。
- **图文混排行高**：delegate 高度在 rich + imageIds 时 = 36+70（文本行 + 图片区），避免图片与下文本重叠。
- **编辑降级一致性**：`DocumentModel::updateLineText` 编辑 rich/image 行 → 模型层同步清空显示层（display→plain、rich 清空、imageIds 清空）：之前 QML 只降级 display 不清 rich，编辑后 .trx 保存再打开会显示旧样式错位。
