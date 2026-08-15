#pragma once

#include <QString>
#include <QVariantMap>

class DocumentModel;
class CommentService;

// .docx 解析器：残缺导入（仅读），docx = zip + XML。
// 用 QuaZip 解压 + QXmlStreamReader 解析 word/document.xml。
// 设计见 docs/services/file-service.md §11：
//   - 段落(w:p) → 行
//   - run 文本合并；w:tab→\t；w:br→\n
//   - 基础格式(b/i/color/sz/rFonts) → 显示层 rich(HTML)
//   - 行内图片(w:drawing/w:pict) → 内嵌 base64 data URI；纯图段落 → image 行
//   - 限制：忽略页眉/页脚/脚注/批注/修订/样式定义/编号/表格结构（残缺导入）
class DocxParser
{
public:
    // 读：清空模型并填充 lines/显示层(rich/image) + meta(sourceFormat=docx, images 内嵌 base64)。
    // 成功返回 true；失败返回 false 并尽量给出 error 描述。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);
};
