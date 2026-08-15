#pragma once

#include <QString>
#include <QVariantMap>

class DocumentModel;
class CommentService;

// .trx 解析器：Translex 自研格式（JSON 容器），实现富文本/图片显示层与批注完整往返。
// 格式见 docs/services/file-service.md §3：
//   { trx, version, meta{sourceFile,sourceFormat,importedAt,font,images[]},
//     lines[{text, display, rich, imageIds[]}], comments{行号:文本} }
// 图片统一存 meta.images（A3 阶段无图片源，read/write 原样保真）。
class TrxParser
{
public:
    // 读：清空模型并填充 lines/display/rich/imageIds + comments；meta 回填到 out。
    // 成功返回 true；失败返回 false 并尽量给出 error 描述。
    static bool read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error = nullptr);

    // 写：完整往返（lines + 显示层 + comments + meta）。
    // meta 非 const：write 会补齐 sourceFile/importedAt 并回填（供调用方保存最新元数据）。
    static bool write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta,
                      QString *error = nullptr);
};
