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
