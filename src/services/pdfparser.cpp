#include "pdfparser.h"

#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>

#include "documentmodel.h"
#include "commentservice.h"

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

// 页文本规范化：模型行是单行文本，页内换行/连续空白压缩为单个空格并 trim。
// 空页 → 空行（保留页映射）。
QString normalizePageText(const QString &raw)
{
    return raw.simplified();
}

QString pdfErrorText(QPdfDocument::Error err)
{
    switch (err) {
    case QPdfDocument::Error::FileNotFound:
        return QStringLiteral("文件不存在");
    case QPdfDocument::Error::InvalidFileFormat:
        return QStringLiteral("不是有效的 PDF 文件");
    case QPdfDocument::Error::IncorrectPassword:
        return QStringLiteral("PDF 已加密，暂不支持密码保护的文件");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QStringLiteral("PDF 使用了不支持的加密方案");
    default:
        return QStringLiteral("无法读取 PDF 文件");
    }
}

} // namespace

bool PdfParser::read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error)
{
    if (!model) {
        setError(error, QStringLiteral("未关联文档模型"));
        return false;
    }

    QPdfDocument doc;
    const QPdfDocument::Error err = doc.load(path);
    if (err != QPdfDocument::Error::None) {
        setError(error, QStringLiteral("打开 %1 失败：%2")
                            .arg(QFileInfo(path).fileName(), pdfErrorText(err)));
        return false;
    }

    QStringList lines;
    const int n = doc.pageCount();
    lines.reserve(n);
    for (int i = 0; i < n; ++i) {
        lines.append(normalizePageText(doc.getAllText(i).text()));
    }

    model->setLines(lines);
    if (comments) {
        comments->clear();
    }

    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("pdf"));
    meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
    return true;
}

bool PdfParser::write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta, QString *error)
{
    if (!model) {
        setError(error, QStringLiteral("未关联文档模型"));
        return false;
    }

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
    writer.setTitle(QFileInfo(path).fileName());
    writer.setCreator(QStringLiteral("Translex"));

    QPainter painter(&writer);
    if (!painter.isActive()) {
        setError(error, QStringLiteral("无法写入 PDF 文件：%1").arg(path));
        return false;
    }

    // 中文字体：必须为 TTF 且可嵌入（微软雅黑/宋体是 TTC 集合字体，QPdfWriter
    // 无法嵌入会退化为字形路径，导出后文本不可提取）；等线 DengXian 为
    // Windows 10+ 自带的 TTF 中文字体。
    const QFont font(QStringLiteral("DengXian"), 12);
    painter.setFont(font);
    const QFontMetrics fm(font);
    const int width = writer.width();
    const int pageHeight = writer.height();
    const int lineHeight = fm.height();
    const int wrapFlags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;

    int y = 0;
    const int n = model->lineCount();
    for (int i = 0; i < n; ++i) {
        QString text = model->lineText(i);
        if (comments) {
            const QString c = comments->commentAt(i);
            if (!c.isEmpty()) {
                text += QStringLiteral("（批注：%1）").arg(c);
            }
        }

        if (text.isEmpty()) {
            // 空行：占一个行高（保持排版连续）
            if (y + lineHeight > pageHeight) {
                writer.newPage();
                y = 0;
            }
            y += lineHeight;
            continue;
        }

        // 折行高度（TextWordWrap 下多行文本占用的像素高度）
        const QRect bounds = fm.boundingRect(0, 0, width, INT_MAX, wrapFlags, text);
        const int need = bounds.height();
        if (y + need > pageHeight) {
            writer.newPage();
            y = 0;
        }
        painter.drawText(QRect(0, y, width, need), wrapFlags, text);
        y += need + 4;
    }
    painter.end();

    meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
    return true;
}
