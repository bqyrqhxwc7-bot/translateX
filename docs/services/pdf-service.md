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

### 3.0 ⚠️ Qt 6.5.3 文本层缺陷（实测确认）

QPdfWriter 导出的 PDF **渲染视觉正确，但文本层不可提取**（QPdfDocument 读回时 ASCII 字符重复、CJK 变 `?`；内嵌字体 FontFile2 + ToUnicode + Identity-H 均存在，但映射错误——Qt 已知缺陷族，6.5.3 未修）。**任何阅读器提取该文本层都不可靠**。

影响与对策：
- **导出 → 再导入 的往返流程不可行**（v1 明示）；文本以字形绘制，人眼阅读/打印正常
- 导入侧不受影响：QPdfDocument 读取**真实 PDF 的文本层**正常（测试用手写干净文本层 PDF 验证）
- 测试策略：导出只断言「有效 PDF + 渲染出字形（暗像素统计）」，不断言文本提取
- 升级 Qt（≥6.8 候选）后可复查此缺陷；届时恢复往返断言

### 3.1 流程

1. `QPdfWriter writer(path)`：A4 + 边距 20mm（`setPageSize(QPageSize(QPageSize::A4))`、`setPageMargins(QMarginsF(20,20,20,20), QPageLayout::Millimeter)`）、`setTitle/setCreator("Translex")`
2. `QPainter painter(&writer)`；字体 **DengXian 12pt**（须为可嵌入 TTF，见 §3.2）
3. 逐行绘制：`QFontMetrics::boundingRect(0,0,width,INT_MAX, Qt::TextWordWrap, line)` 求折行高度 `h`
   - `y + h > writer.height()` → `writer.newPage()`，y 归零
   - `painter.drawText(QRect(0, y, width, h), Qt::TextWordWrap, line)`；`y += h + 4`
   - 空行 → `y += lineHeight`（保持空行）
   - 行有批注 → 行文本追加 `（批注：{批注文本}）`
4. `painter.end()`（QPdfWriter 析构时写文件）

### 3.2 说明

- `writer.width()/height()` 返回**可用区像素**（页尺寸 - 边距），直接用
- 字体：**DengXian（等线）TTF**——不能用 TTC 集合字体（微软雅黑/宋体），TTC 嵌入退化为字形路径更糟；DengXian 是 Windows 10+ 自带 TTF，视觉现代
- 导出内容是**编辑层文本**（用户编辑后的结果），与导入页映射无关（行数可能已变）

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

## 6. 测试计划（tests/tst_pdf.cpp，13 目标之一）

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
- **导出文本层不可提取**（Qt 6.5.3 QPdfWriter 缺陷，§3.0）：视觉正确，但不可复制/搜索/再导入；往返流程不可行
- 版式近似：无原字体/字号/颜色/分栏还原，统一 DengXian 12pt 导出
- 导出不含原图（仅文本层 + 批注尾注；批注尾注随文本层受 §3.0 限制）
- 超大 PDF（页数巨多）走阶段 D 大文件降级策略（打开时行数探测复用）

## 8. 验证清单

- `cmake --build build-vs2026-x64 --config Debug` 通过
- `ctest -R tst_pdf` 全绿；全量 `ctest` 不回归
- 应用打开/另存为对话框含 `.pdf`；手动打开 demo.pdf 验证页→行
