#pragma once

#include <QString>
#include <QVariantMap>

class DocumentModel;
class CommentService;

// PDF 解析器：残缺导入（读）+ 文本页导出（写），Qt 自带 QPdfDocument/QPdfWriter（零依赖）。
// 设计见 docs/services/pdf-service.md：
//   - 导入：每页一行（阅读顺序文本，页内空白压缩为单空格；空页保留空行 → 页映射可逆）
//   - 无 OCR：扫描版 PDF 文本层为空 → 空行；密码 PDF 报错
//   - 导出：编辑层文本按行重建文本页（A4 + 20mm 边距，按文本高度自动分页）
//   - 批注：导入清空；导出以「（批注：xxx）」尾注追加
class PdfParser
{
public:
    // 读：清空模型并填充 lines + meta(sourceFormat=pdf, sourceFile)。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);

    // 写：编辑层文本 → PDF 文本页。成功回填 meta.sourceFile。
    static bool write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta, QString *error = nullptr);
};
