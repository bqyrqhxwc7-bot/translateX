#pragma once

#include <QString>
#include <QVariantMap>

class DocumentModel;
class CommentService;

// .docx 解析器：导入（仅读）+ 导出（对称补全）。
// docx = zip + XML。QuaZip 解压/压缩 + QXmlStreamReader/Writer 处理 word/document.xml。
// 设计见 docs/services/file-service.md §11 与 docs/services/docx-comment-export.md：
//   - 段落(w:p) → 行
//   - run 文本合并；w:tab→\t；w:br→\n
//   - 基础格式(b/i/color/sz/rFonts) → 显示层 rich(HTML)
//   - 行内图片(w:drawing/w:pict) → 内嵌 base64 data URI；纯图段落 → image 行
//   - 限制：忽略页眉/页脚/脚注/批注/修订/样式定义/编号/表格结构（残缺导入）
//   - 导出：原文行 + 译文（批注），样式 docxCommentStyle: inline(黄色高亮)/native(Word 原生批注)
class DocxParser
{
public:
    // 读：清空模型并填充 lines/显示层(rich/image) + meta(sourceFormat=docx, images 内嵌 base64)。
    // 成功返回 true；失败返回 false 并尽量给出 error 描述。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);

    // 写：原文行(lineText) + 译文(commentAt) 导出为最小 docx。
    // style: "inline"（译文内联黄色高亮）/ "native"（Word 原生批注 w:comment）。
    // 富文本/图片不导出样式：rich 行导出编辑层纯文本，image 行导出 "[图片]" 占位。
    static bool write(const QString &path, DocumentModel *model,
                      CommentService *comments, const QString &style,
                      QVariantMap *meta = nullptr, QString *error = nullptr);
};
