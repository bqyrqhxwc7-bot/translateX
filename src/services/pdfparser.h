#pragma once

#include <QString>
#include <QVariantMap>

class DocumentModel;
class CommentService;

// PDF 解析器：残缺导入（读）+ 文本页导出（写），Qt 自带 QPdfDocument/QPdfWriter（零依赖）。
// 设计见 docs/services/pdf-service.md：
//   - 导入：每页一行（阅读顺序文本，页内空白压缩为单空格；空页保留空行 → 页映射可逆）
//   - 无 OCR：扫描版 PDF 文本层为空 → 空行；密码 PDF 报错
//   - 导出：编辑层文本按行重建文本页，**每行一页**（再导入行结构保真）
//   - 批注：导入清空；导出以「（批注：xxx）」尾注追加
//   - 文本层修复：写后重建 ToUnicode CMap（Qt 6.5.3 中文映射到康熙部首区的缺陷）
class PdfParser
{
public:
    // 读：清空模型并填充 lines + meta(sourceFormat=pdf, sourceFile)。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);

    // 写：编辑层文本 → PDF 文本页（每行一页），写后自动修复文本层。成功回填 meta。
    static bool write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta, QString *error = nullptr);

private:
    // 文本层修复：内容流 CID 与导出文本对齐重建 ToUnicode CMap 并重写 PDF
    //（见 pdf-service.md §3.3）。textSeq = 导出文本字符序列（含空格）。
    // 失败返回 false（保留原始输出）。
    static bool repairTextLayer(const QString &path, const QString &textSeq);
};
