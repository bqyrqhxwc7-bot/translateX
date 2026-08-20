#include "pdfparser.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QBuffer>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>

#include <cstring>

#include <zlib.h>

#include "documentmodel.h"
#include "commentservice.h"

namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

// 行文本规范化：模型行是单行文本，保留行首缩进（代码块/列表/段落缩进对
// 可读性重要），仅去除行尾空白；行内原始空白保留（原文保真——
// 压缩内部空白会改变内容，如 "Document #:    P2899R1"）。空行保留。
QString normalizeLineText(const QString &raw)
{
    int end = raw.size();
    while (end > 0 && raw.at(end - 1).isSpace()) {
        --end;
    }
    return raw.left(end);
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

// v3 富文本导入（定义见文件后部，pdf-service.md §9.2）：
// 自研内容流解析提取样式/图片/链接 → 填充模型 rich/image 显示层。
// 失败返回 false（调用方回退纯文本按行导入）。
bool importRichPdf(const QByteArray &data, DocumentModel *model,
                   QVariantMap &meta, QString *error);

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

    // v3（2026-08-19）：先尝试富文本导入（自研内容流解析：样式/图片/链接），
    // 失败回退纯文本按行导入（不阻塞打开，见 pdf-service.md §9.2.4）
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        QVariantMap richMeta;
        QString richError;
        if (importRichPdf(data, model, richMeta, &richError)) {
            if (comments) {
                comments->clear();
            }
            meta = richMeta;
            meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
            return true;
        }
    }

    QStringList lines;
    const int n = doc.pageCount();
    for (int i = 0; i < n; ++i) {
        // 按视觉行导入（2026-08-19 改进）：getAllText 已按行分割（\n 分隔），
        // 每行一个模型行。原「每页一行」会把整页文本挤成一行（千字符级），
        // 无法逐段编辑/翻译；按行导入后每行 20-100 字符，翻译单位合理。
        // 空行保留（段落间隔；空页 → 单个空行，页映射语义保留）。
        const QStringList pageLines = doc.getAllText(i).text().split(QLatin1Char('\n'));
        for (const QString &ln : pageLines) {
            lines.append(normalizeLineText(ln));
        }
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

    // QPdfWriter 在析构时才 finalize 写盘，故用作用域块包住绘制，
    // 块结束后（writer 析构）再执行文本层修复。
    QString textSeq;
    QStringList warnings;   // 导出告警（图片缺失/解码失败等，meta.exportWarnings 返回）
    {
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
        // QFontMetrics/QTextDocument 按 96dpi 度量，而 QPdfWriter painter 是设备像素
        // （1200dpi）——行高必须按分辨率比例放大，否则行距缩小 12.5 倍、行重叠，
        // 再导入时被 groupIntoLines 合并成一行（writeFullRoundTrip 曾 lines=1）。
        const qreal resScale = writer.resolution() / 96.0;
        const int lineHeight = qRound(fm.height() * resScale);
        const int wrapFlags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;

        // 图片索引：meta.images[{id, format, dataBase64}] → id → <img data URI>
        QHash<QString, QString> imgData;
        const QVariantList metaImages = meta.value(QStringLiteral("images")).toList();
        for (const QVariant &iv : metaImages) {
            const QVariantMap im = iv.toMap();
            const QString id = im.value(QStringLiteral("id")).toString();
            const QString b64 = im.value(QStringLiteral("dataBase64")).toString();
            if (!id.isEmpty() && !b64.isEmpty()) {
                const QString fmt = im.value(QStringLiteral("format")).toString();
                imgData.insert(id,
                               QStringLiteral("data:image/%1;base64,%2").arg(fmt, b64));
            }
        }

        // 连续排版：多行一页、自动分页（导出为正常文档；再导入按视觉行拆分，
        // 行结构保真——原「每行一页」导出 342 行 = 342 页，惨不忍睹）
        // 同时收集导出文本字符序列（含空格）供 ToUnicode CMap 对齐
        const int n = model->lineCount();
        int y = 0;
        for (int i = 0; i < n; ++i) {
            const QString display = model->displayAt(i);
            const QString rich = model->richAt(i);
            QString text = model->lineText(i);
            if (comments) {
                const QString c = comments->commentAt(i);
                if (!c.isEmpty()) {
                    text += QStringLiteral("（批注：%1）").arg(c);
                }
            }

            int h = 0;
            if (display == QLatin1String("rich") && !rich.isEmpty()) {
                // rich 行：QTextDocument 渲染（样式/链接/图片恢复，pdf-service.md §9.4）。
                // [图片] 占位替换为 <img data URI>（按原始宽高比缩放适配行宽）
                QString html = rich;
                if (html.contains(QStringLiteral("[图片]"))) {
                    for (const QString &imgId : model->imageIdsAt(i)) {
                        if (imgData.contains(imgId)) {
                            const QString uri = imgData.value(imgId);
                            const QString b64 = uri.section(QLatin1Char(','), 1);
                            const QImage img =
                                QImage::fromData(QByteArray::fromBase64(b64.toLatin1()));
                            if (!img.isNull()) {
                                int iw = img.width(), ih = img.height();
                                if (iw > width) {
                                    ih = ih * width / iw;
                                    iw = width;
                                }
                                html.replace(QStringLiteral("[图片]"),
                                             QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\">")
                                                 .arg(uri).arg(iw).arg(ih));
                            } else {
                                html.replace(QStringLiteral("[图片]"),
                                             QStringLiteral("<img src=\"%1\" height=\"80\">").arg(uri));
                            }
                            break;
                        }
                    }
                }
                QTextDocument doc;
                doc.setHtml(html);
                doc.setPageSize(QSizeF(width, pageHeight));
                doc.setDefaultFont(font);
                h = qCeil(doc.size().height() * resScale);
                if (y + h > pageHeight) {
                    writer.newPage();
                    y = 0;
                }
                // drawContents 用现有 painter 绘制（print() 会重复 begin painter 失败）。
                // 注意：drawContents 把文档画在 (0,0) 并裁剪到给定 rect——必须先把
                // painter 平移到本行 y 位置、rect 用 (0,0) 起，否则 y>0 的行的
                // 裁剪区与文档内容不重叠，整行文本被裁掉（曾致导出只剩第一行）。
                painter.save();
                painter.translate(0, y);
                doc.drawContents(&painter, QRectF(0, 0, width, h));
                painter.restore();
                y += h + 2;
                if (!text.isEmpty()) {
                    textSeq += text;
                }
            } else if (display == QLatin1String("image") && !model->imageIdsAt(i).isEmpty()) {
                // 纯图行：图片缩放适配行宽（保持宽高比，防超大图溢出页面）
                const QString imgId = model->imageIdsAt(i).first();
                if (imgData.contains(imgId)) {
                    const QString b64 =
                        imgData.value(imgId).section(QLatin1Char(','), 1);
                    const QImage img = QImage::fromData(QByteArray::fromBase64(b64.toLatin1()));
                    if (!img.isNull()) {
                        QImage scaled = img;
                        if (img.width() > width) {
                            scaled = img.scaledToWidth(width, Qt::SmoothTransformation);
                        }
                        h = scaled.height();
                        if (y + h > pageHeight) {
                            writer.newPage();
                            y = 0;
                        }
                        painter.drawImage(QRect(0, y, scaled.width(), scaled.height()), scaled);
                        y += h + 2;
                    } else {
                        warnings.append(QStringLiteral("第 %1 行图片解码失败").arg(i + 1));
                    }
                } else {
                    warnings.append(QStringLiteral("第 %1 行图片缺失").arg(i + 1));
                }
            } else if (!text.isEmpty()) {
                // 纯文本：折行高度（TextWordWrap 下多行文本占用的像素高度）
                const QRect bounds = fm.boundingRect(0, 0, width, INT_MAX, wrapFlags, text);
                h = qRound(bounds.height() * resScale);
                if (y + h > pageHeight) {
                    writer.newPage();
                    y = 0;
                }
                painter.drawText(QRect(0, y, width, h), wrapFlags, text);
                y += h + 2;
                textSeq += text;
            } else {
                // 空行：占一行高度（段落间隔；原「每行一页」时空行=空页）。
                // 画一个空格占位——否则无文本运行，再导入时该行丢失
                // （writeFullRoundTrip 曾 lines=3 缺空行）；空格经
                // normalizeLineText 去尾空白后还原为空行。
                if (y + lineHeight > pageHeight) {
                    writer.newPage();
                    y = 0;
                }
                painter.drawText(QRect(0, y, width, lineHeight),
                                 Qt::AlignLeft | Qt::AlignTop, QStringLiteral(" "));
                y += lineHeight;
                textSeq += QLatin1Char(' ');
            }
        }
        painter.end();
    }   // QPdfWriter 析构 → PDF 文件落盘

    // 文本层修复：内容流 CID ↔ 导出文本对齐，重建 ToUnicode CMap
    //（Qt 6.5.3 中文映射到康熙部首区的缺陷），使导出 PDF 文本可被任何阅读器提取
    repairTextLayer(path, textSeq);

    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("pdf"));
    meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
    if (!warnings.isEmpty()) {
        meta.insert(QStringLiteral("exportWarnings"), warnings);
    }
    return true;
}

namespace {

// ---- 文本层修复（ToUnicode CMap 重建），详见 pdf-service.md §3.3 ----

QByteArray toHex4(int v)
{
    return QByteArray::number(v, 16).rightJustified(4, '0').toUpper();
}

// PDF 对象：N 0 obj <<dict>> stream...endstream endobj
struct PdfObject {
    int number = -1;
    qint64 off = -1;     // 对象在文件中的偏移（xref 定位）
    QByteArray raw;      // 对象完整原始字节（含 dict/stream，不含 "N 0 obj\n" 前缀与 endobj 后缀）
    QByteArray dict;     // <<...>>
    QByteArray stream;   // 原始流字节（可能压缩）
    bool hasStream = false;
};

// zlib 解压（PDF FlateDecode 流为纯 zlib 格式，qUncompress 需 4 字节头不适用）
QByteArray inflateStream(const QByteArray &compressed)
{
    if (compressed.size() < 2 || static_cast<uchar>(compressed.at(0)) != 0x78) {
        return QByteArray();   // 非 zlib 头
    }
    uLongf outLen = compressed.size() * 8 + 1024;
    QByteArray out;
    for (int attempt = 0; attempt < 3; ++attempt) {
        out.resize(static_cast<int>(outLen));
        const int ret = uncompress(
            reinterpret_cast<Bytef *>(out.data()), &outLen,
            reinterpret_cast<const Bytef *>(compressed.constData()),
            static_cast<uLong>(compressed.size()));
        if (ret == Z_OK) {
            out.resize(static_cast<int>(outLen));
            return out;
        }
        if (ret != Z_BUF_ERROR) {
            return QByteArray();
        }
        outLen *= 4;
    }
    return QByteArray();
}

// 从内容流提取 CID 序列（按 Tj 出现顺序，含重复）
QList<int> contentCidSequence(const QByteArray &content)
{
    QList<int> cids;
    const QRegularExpression re("<([0-9A-Fa-f]{4})> Tj");
    QRegularExpressionMatchIterator it =
        re.globalMatch(QString::fromLatin1(content));
    while (it.hasNext()) {
        bool ok = false;
        const int cid = it.next().captured(1).toInt(&ok, 16);
        if (ok) {
            cids.append(cid);
        }
    }
    return cids;
}

// 生成 ToUnicode CMap：内容流 CID 序列与导出文本序列对齐
//（QPdfWriter 实测：CID 按文本字符首次出现顺序分配、重复字符复用、空格也有 CID）
// 对齐失败（CID 分配与文本不一致）返回空 → 放弃修复。
QByteArray buildCMapFromText(const QList<int> &cids, const QString &textSeq)
{
    if (cids.isEmpty() || textSeq.isEmpty()) {
        return QByteArray();
    }
    QHash<int, QChar> map;   // CID → 字符
    int i = 0;               // CID 索引
    int j = 0;               // 文本索引
    while (i < cids.size()) {
        if (j >= textSeq.size()) {
            return QByteArray();   // 文本耗尽但 CID 未完 → 无法对齐
        }
        const int c = cids.at(i);
        const QChar t = textSeq.at(j);
        if (!map.contains(c)) {
            map.insert(c, t);
            ++i;
            ++j;
        } else if (map.value(c) == t) {
            ++i;
            ++j;
        } else {
            // 冲突：尝试跳过当前文本字符（QPdfWriter 可能不发某些字符的 CID）
            ++j;
        }
    }
    if (map.isEmpty()) {
        return QByteArray();
    }

    QByteArray cmap;
    cmap += "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n";
    cmap += "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n";
    cmap += "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n";
    cmap += "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    cmap += QByteArray::number(map.size()) + " beginbfchar\n";
    QList<int> keys = map.keys();
    std::sort(keys.begin(), keys.end());
    for (const int c : keys) {
        cmap += "<" + toHex4(c) + "> <" + toHex4(map.value(c).unicode()) + ">\n";
    }
    cmap += "endbfchar\n";
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return cmap;
}

// 解析 PDF 对象列表：xref 权威定位。
// 对象边界 = 下一个对象的 xref 偏移（流内二进制可能含 "obj"/"endobj" 字节，
// 不能按字节扫描）；流长度用 /Length（支持间接引用 "/Length N 0 R"）精确截取。
bool parsePdfObjects(const QByteArray &data, QVector<PdfObject> &objects)
{
    // ---- 1) xref：编号 → 偏移（行式解析，兼容 \r\n 与 \n）----
    QVector<qint64> xrefOffsets;
    const QRegularExpression startxrefRe("startxref\\s+(\\d+)");
    const QRegularExpressionMatch sx = startxrefRe.match(QString::fromLatin1(data));
    if (!sx.hasMatch()) {
        return false;
    }
    const QByteArray xrefArea = data.mid(sx.captured(1).toInt());
    const QRegularExpression headRe("xref\\s+0\\s+(\\d+)");
    const QRegularExpressionMatch hm = headRe.match(QString::fromLatin1(xrefArea));
    if (!hm.hasMatch()) {
        return false;
    }
    const int count = hm.captured(1).toInt();
    int lineStart = hm.capturedEnd();
    for (int i = 0; i < count; ++i) {
        while (lineStart < xrefArea.size()
               && (xrefArea.at(lineStart) == '\r' || xrefArea.at(lineStart) == '\n')) {
            ++lineStart;
        }
        const int lineEnd = xrefArea.indexOf('\n', lineStart);
        if (lineEnd < 0) {
            break;
        }
        const QList<QByteArray> parts =
            xrefArea.mid(lineStart, lineEnd - lineStart).simplified().split(' ');
        lineStart = lineEnd + 1;
        if (parts.size() >= 3) {
            const bool inUse = parts.at(2).contains('n');
            xrefOffsets.append(inUse ? parts.at(0).toLongLong() : -1);
        } else {
            xrefOffsets.append(-1);
        }
    }
    if (xrefOffsets.isEmpty()) {
        return false;
    }

    // ---- 2) 按偏移排序，对象边界 = 下一对象偏移 ----
    struct Entry {
        int num;
        qint64 off;
        int headEnd;      // 对象头行尾
        int dictStart;
        int dictEnd;
        bool hasDict = false;
        int lengthRefObj = -1;
        int lengthDirect = -1;
    };
    QVector<Entry> entries;
    for (int num = 1; num < xrefOffsets.size(); ++num) {
        if (xrefOffsets.at(num) >= 0) {
            entries.append({ num, xrefOffsets.at(num), 0, -1, -1, false, -1, -1 });
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.off < b.off; });

    const QRegularExpression lengthRe("/Length\\s+(\\d+)(?:\\s+(\\d+)\\s+R)?");
    for (int i = 0; i < entries.size(); ++i) {
        Entry &e = entries[i];
        // 锚定对象头（偏移处必须是 "num 0 obj"）
        const QRegularExpression headRe(QStringLiteral("\\b%1\\s+0\\s+obj").arg(e.num));
        const QRegularExpressionMatch h = headRe.match(
            QString::fromLatin1(data.mid(static_cast<int>(e.off), 96)));
        if (!h.hasMatch()) {
            continue;
        }
        int cursor = static_cast<int>(e.off) + h.capturedEnd();
        while (cursor < data.size() && data.at(cursor) != '\n' && data.at(cursor) != '\r') {
            ++cursor;
        }
        if (cursor < data.size() && data.at(cursor) == '\r') {
            ++cursor;
        }
        if (cursor < data.size() && data.at(cursor) == '\n') {
            ++cursor;
        }
        e.headEnd = cursor;
        // 对象边界 = 下一对象偏移（物理顺序）
        const qint64 boundary =
            i + 1 < entries.size() ? entries.at(i + 1).off : data.size();

        // dict（若存在）：深度匹配 << >>（直接基于 data 绝对偏移，不限窗口）
        // 注意：紧凑格式（PDFium）dict 紧跟在 "N 0 obj" 后，与头部同处一行，
        // headEnd 已越过 dict，必须从对象头结束处（capturedEnd）开始查找。
        const int headOffset = static_cast<int>(e.off) + h.capturedEnd();
        const int dictStart = data.indexOf("<<", headOffset);
        if (dictStart >= 0 && dictStart < boundary) {
            int depth = 0;
            int dictEnd = -1;
            const int dictAreaEnd = qMin<qint64>(data.size(), boundary);
            for (int j = dictStart; j < dictAreaEnd; ++j) {
                if (data.at(j) == '<' && j + 1 < dictAreaEnd && data.at(j + 1) == '<') {
                    ++depth;
                    ++j;
                } else if (data.at(j) == '>' && j + 1 < dictAreaEnd && data.at(j + 1) == '>') {
                    --depth;
                    ++j;
                    if (depth == 0) {
                        dictEnd = j;
                        break;
                    }
                }
            }
            if (dictEnd >= 0) {
                e.hasDict = true;
                e.dictStart = dictStart - static_cast<int>(e.off);
                e.dictEnd = dictEnd - static_cast<int>(e.off);
                const QRegularExpressionMatch lm = lengthRe.match(
                    QString::fromLatin1(data.mid(dictStart, dictEnd - dictStart + 1)));
                if (lm.hasMatch()) {
                    if (lm.captured(2).isEmpty()) {
                        e.lengthDirect = lm.captured(1).toInt();
                    } else {
                        e.lengthRefObj = lm.captured(1).toInt();
                    }
                }
            }
        }
    }

    // ---- 3) 间接 Length 值：Length 值对象内容为纯数字 ----
    QHash<int, int> lengthValues;
    for (const Entry &e : entries) {
        if (e.lengthRefObj < 0) {
            continue;
        }
        int val = -1;
        for (const Entry &c : entries) {
            if (c.num == e.lengthRefObj) {
                const qint64 boundary =
                    (std::find_if(entries.begin(), entries.end(),
                                  [&c](const Entry &x) { return x.off > c.off; })
                     != entries.end())
                        ? (std::find_if(entries.begin(), entries.end(),
                                        [&c](const Entry &x) { return x.off > c.off; }))
                              ->off
                        : data.size();
                const QByteArray content =
                    data.mid(c.headEnd, static_cast<int>(boundary - c.headEnd));
                // 内容形如 "123\nendobj\n..."，提取首个数字
                const QRegularExpression numRe("(\\d+)");
                const QRegularExpressionMatch nm =
                    numRe.match(QString::fromLatin1(content));
                val = nm.hasMatch() ? nm.captured(1).toInt() : -1;
                break;
            }
        }
        lengthValues.insert(e.num, val);
    }

    // ---- 4) 组装对象：流按 Length 精确截取 ----
    for (int i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        if (e.headEnd == 0) {
            continue;
        }
        const qint64 boundary =
            i + 1 < entries.size() ? entries.at(i + 1).off : data.size();
PdfObject obj;
        obj.number = e.num;
        obj.off = e.off;
        obj.dict = e.hasDict
                       ? data.mid(e.off + e.dictStart, e.dictEnd - e.dictStart + 1)
                       : QByteArray();
        int streamLen = e.lengthDirect;
        if (e.lengthRefObj >= 0 && lengthValues.contains(e.num)) {
            streamLen = lengthValues.value(e.num);
        }
        if (streamLen > 0 && e.hasDict) {
            const int streamKw = data.indexOf("stream", e.off + e.dictEnd + 1);
            if (streamKw >= 0 && streamKw - (e.off + e.dictEnd) < 128
                && streamKw + 6 < boundary) {
                int s = streamKw + 6;
                if (s < data.size() && data.at(s) == '\r') {
                    ++s;
                }
                if (s < data.size() && data.at(s) == '\n') {
                    ++s;
                }
                if (s + streamLen <= boundary) {
                    obj.hasStream = true;
                    obj.stream = data.mid(s, streamLen);
                    obj.raw = data.mid(e.headEnd, s + streamLen - e.headEnd);
                    objects.append(obj);
                    continue;
                }
            }
        }
        obj.raw = data.mid(e.headEnd, static_cast<int>(boundary - e.headEnd));
        objects.append(obj);
    }
    return !objects.isEmpty();
}

// 从字典中提取 /ToUnicode N 0 R 引用
int toUnicodeObjNumber(const QByteArray &dict)
{
    const QByteArray key = "/ToUnicode";
    const int idx = dict.indexOf(key);
    if (idx < 0) {
        return -1;
    }
    const int refStart = idx + key.size();
    const int refEnd = dict.indexOf("R", refStart);
    if (refEnd < 0) {
        return -1;
    }
    const QList<QByteArray> parts = dict.mid(refStart, refEnd - refStart).simplified().split(' ');
    if (parts.size() >= 1) {
        return parts.first().toInt();
    }
    return -1;
}

// 从字典中提取 /FontFile2 N 0 R 引用（内嵌字体流）
int fontFileObjNumber(const QByteArray &dict)
{
    const QByteArray key = "/FontFile2";
    const int idx = dict.indexOf(key);
    if (idx < 0) {
        return -1;
    }
    const int refStart = idx + key.size();
    const int refEnd = dict.indexOf("R", refStart);
    if (refEnd < 0) {
        return -1;
    }
    const QList<QByteArray> parts = dict.mid(refStart, refEnd - refStart).simplified().split(' ');
    if (parts.size() >= 1) {
        return parts.first().toInt();
    }
    return -1;
}

} // namespace

// ---- v3 富文本导入（pdf-service.md §9.2）：自研内容流解析器 ----

namespace {

// 内容流 token：数字/名称/字符串/数组/操作符
struct ContentToken {
    enum Type { Number, Name, String, Array, ArrayEnd, Operator, Eof } type = Eof;
    double number = 0;
    QByteArray name;   // Name：不含前导 /；Operator：操作符名
    QByteArray str;    // String：解码后字节
    QVector<ContentToken> array;
};

// PDF 内容流词法分析器（操作符流 token 化）
class ContentLexer
{
public:
    explicit ContentLexer(const QByteArray &data)
        : m_data(data)
    {
    }

    ContentToken next()
    {
        skipWhitespace();
        if (m_pos >= m_data.size()) {
            return {};
        }
        const char c = m_data.at(m_pos);
        if (c == '/') {
            return { ContentToken::Name, 0, readName(), {}, {} };
        }
        if (c == '(') {
            return { ContentToken::String, 0, {}, readString(), {} };
        }
        if (c == '<') {
            if (m_pos + 1 < m_data.size() && m_data.at(m_pos + 1) == '<') {
                // 字典：内容流内罕见，跳过到 >>
                m_pos += 2;
                while (m_pos + 1 < m_data.size()
                       && !(m_data.at(m_pos) == '>' && m_data.at(m_pos + 1) == '>')) {
                    ++m_pos;
                }
                m_pos = qMin(m_pos + 2, m_data.size());
                return next();
            }
            return { ContentToken::String, 0, {}, readHexString(), {} };
        }
        if (c == '[') {
            ++m_pos;
            ContentToken t;
            t.type = ContentToken::Array;
            while (true) {
                const ContentToken e = next();
                if (e.type == ContentToken::Eof || e.type == ContentToken::ArrayEnd) {
                    break;
                }
                t.array.append(e);
            }
            return t;
        }
        if (c == ']') {
            ++m_pos;
            ContentToken t;
            t.type = ContentToken::ArrayEnd;
            return t;
        }
        if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) {
            return { ContentToken::Number, readNumber(), {}, {}, {} };
        }
        // 操作符：字母序列（含 ' " * 变体）
        const int start = m_pos;
        while (m_pos < m_data.size()) {
            const char ch = m_data.at(m_pos);
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || ch == '\'' || ch == '"' || ch == '*') {
                ++m_pos;
            } else {
                break;
            }
        }
        if (m_pos == start) {
            ++m_pos;   // 未知字符跳过
            return next();
        }
        return { ContentToken::Operator, 0, m_data.mid(start, m_pos - start), {}, {} };
    }

private:
    const QByteArray &m_data;
    int m_pos = 0;

    void skipWhitespace()
    {
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c == '%') {
                while (m_pos < m_data.size() && m_data.at(m_pos) != '\n') {
                    ++m_pos;
                }
            } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    QByteArray readName()
    {
        ++m_pos;   // 跳过 /
        QByteArray out;
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f'
                || c == '(' || c == ')' || c == '<' || c == '>' || c == '['
                || c == ']' || c == '{' || c == '}' || c == '/' || c == '%') {
                break;
            }
            if (c == '#' && m_pos + 2 < m_data.size()) {
                bool ok = false;
                const int v = QByteArray::fromHex(m_data.mid(m_pos + 1, 2)).toInt(&ok, 16);
                if (ok) {
                    out.append(char(v));
                }
                m_pos += 3;
                continue;
            }
            out.append(c);
            ++m_pos;
        }
        return out;
    }

    QByteArray readString()
    {
        ++m_pos;   // 跳过 (
        QByteArray out;
        int depth = 1;
        while (m_pos < m_data.size() && depth > 0) {
            const char c = m_data.at(m_pos);
            if (c == '\\') {
                ++m_pos;
                if (m_pos >= m_data.size()) {
                    break;
                }
                const char e = m_data.at(m_pos);
                switch (e) {
                case 'n': out.append('\n'); break;
                case 'r': out.append('\r'); break;
                case 't': out.append('\t'); break;
                case 'b': out.append('\b'); break;
                case 'f': out.append('\f'); break;
                case '(': out.append('('); break;
                case ')': out.append(')'); break;
                case '\\': out.append('\\'); break;
                default:
                    if (e >= '0' && e <= '7') {
                        int v = 0;
                        int cnt = 0;
                        while (m_pos < m_data.size() && cnt < 3
                               && m_data.at(m_pos) >= '0' && m_data.at(m_pos) <= '7') {
                            v = v * 8 + (m_data.at(m_pos) - '0');
                            ++m_pos;
                            ++cnt;
                        }
                        out.append(char(v));
                        continue;
                    }
                    out.append(e);
                    break;
                }
                ++m_pos;
            } else if (c == '(') {
                ++depth;
                out.append(c);
                ++m_pos;
            } else if (c == ')') {
                --depth;
                if (depth > 0) {
                    out.append(c);
                }
                ++m_pos;
            } else {
                out.append(c);
                ++m_pos;
            }
        }
        return out;
    }

    QByteArray readHexString()
    {
        ++m_pos;   // 跳过 <
        QByteArray hex;
        while (m_pos < m_data.size() && m_data.at(m_pos) != '>') {
            const char c = m_data.at(m_pos);
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                hex.append(c);
            }
            ++m_pos;
        }
        if (m_pos < m_data.size()) {
            ++m_pos;   // 跳过 >
        }
        if (hex.size() % 2 != 0) {
            hex.append('0');
        }
        return QByteArray::fromHex(hex);
    }

    double readNumber()
    {
        const int start = m_pos;
        while (m_pos < m_data.size()) {
            const char c = m_data.at(m_pos);
            if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
                ++m_pos;
            } else {
                break;
            }
        }
        return QByteArray(m_data.mid(start, m_pos - start)).toDouble();
    }
};

// PDF 简单字体标准编码（/Encoding 指定；Unknown 时按 WinAnsi 系兜底）
enum class PdfEncoding { Unknown, WinAnsi, Standard, MacRoman };

// 字体信息：BaseFont（去子集前缀）+ 粗斜体 + ToUnicode CMap（CID 字体解码）
struct FontInfo {
    QString family;
    bool bold = false;
    bool italic = false;
    int codeBytes = 1;               // 简单字体 1 字节（Type1/TrueType），Type0 2 字节
    PdfEncoding encoding = PdfEncoding::Unknown;   // 无 ToUnicode 时的标准编码表
    QHash<int, QChar> toUnicode;     // 码 → 字符（空 = 简单字体按编码表解码）
};

// 文本运行：一次 Tj/TJ 显示的文本 + 坐标 + 样式
struct TextRun {
    double x = 0, y = 0;
    QString text;
    QString fontFamily;
    double fontSize = 0;
    QString color;
    bool bold = false;
    bool italic = false;
};

// 图片引用：/Do 绘制的 XObject（阶段 2 提取位图）
struct ImageRef {
    QString name;
    double x = 0, y = 0;    // CTM 平移（页面坐标，图片左下角）
    double sx = 1, sy = 1;  // CTM 缩放（显示尺寸 = 像素尺寸 × 缩放）
};

struct PageContent {
    QVector<TextRun> runs;
    QVector<ImageRef> images;
};

QString rgbToHex(double r, double g, double b)
{
    auto to255 = [](double v) {
        return qBound(0, qRound(v * 255.0), 255);
    };
    return QStringLiteral("#%1%2%3")
        .arg(to255(r), 2, 16, QLatin1Char('0'))
        .arg(to255(g), 2, 16, QLatin1Char('0'))
        .arg(to255(b), 2, 16, QLatin1Char('0'));
}

QString cmykToHex(double c, double m, double y, double k)
{
    return rgbToHex(1.0 - qMin(1.0, c + k), 1.0 - qMin(1.0, m + k), 1.0 - qMin(1.0, y + k));
}

// ---- PDF 标准编码表（WinAnsi/Standard/MacRoman 的 0x80-0x9F/0xA0-0xBF 特殊区）----
// 0x20-0x7E 一律直通 ASCII；0xC0-0xFF 直通 Latin-1。返回空 = 未定义码（跳过，
// 避免产生控制字符/□）。

QChar winAnsiChar(int code)
{
    switch (code) {
    case 0x80: return QChar(0x20AC); case 0x82: return QChar(0x201A);
    case 0x83: return QChar(0x0192); case 0x84: return QChar(0x201E);
    case 0x85: return QChar(0x2026); case 0x86: return QChar(0x2020);
    case 0x87: return QChar(0x2021); case 0x88: return QChar(0x02C6);
    case 0x89: return QChar(0x2030); case 0x8A: return QChar(0x0160);
    case 0x8B: return QChar(0x2039); case 0x8C: return QChar(0x0152);
    case 0x8E: return QChar(0x017D); case 0x91: return QChar(0x2018);
    case 0x92: return QChar(0x2019); case 0x93: return QChar(0x201C);
    case 0x94: return QChar(0x201D); case 0x95: return QChar(0x2022);
    case 0x96: return QChar(0x2013); case 0x97: return QChar(0x2014);
    case 0x98: return QChar(0x02DC); case 0x99: return QChar(0x2122);
    case 0x9A: return QChar(0x0161); case 0x9B: return QChar(0x203A);
    case 0x9C: return QChar(0x0153); case 0x9E: return QChar(0x017E);
    case 0x9F: return QChar(0x0178);
    default: return QChar();
    }
}

// StandardEncoding 的 0xA0-0xBF 特殊区（PDF 32000-1 Table 5.9/5.10）
QChar standardChar(int code)
{
    switch (code) {
    case 0xA0: return QChar(0x2011);  // 不连字符
    case 0xA1: return QChar(0x02D8);  // breve
    case 0xA2: return QChar(0x02C7);  // caron
    case 0xA3: return QChar(0x02C6);  // circumflex
    case 0xA4: return QChar(0x02D9);  // dotaccent
    case 0xA5: return QChar(0x02DD);  // hungarumlaut
    case 0xA6: return QChar(0x02DB);  // ogonek
    case 0xA7: return QChar(0x02DA);  // ring
    case 0xA8: return QChar(0x0138);  // kra
    case 0xA9: return QChar(0x02DC);  // tilde
    case 0xAA: return QChar(0x00A8);  // diaeresis
    case 0xAB: return QChar(0x00F8);  // oslash
    case 0xAC: return QChar(0x0141);  // Lslash
    case 0xAD: return QChar(0x0152);  // OE
    case 0xAE: return QChar(0x00C6);  // AE
    case 0xAF: return QChar(0x00AA);  // ordfeminine
    case 0xB0: return QChar(0x00BA);  // ordmasculine
    case 0xB1: return QChar(0x00BF);  // questiondown
    case 0xB2: return QChar(0x00A1);  // exclamdown
    case 0xB3: return QChar(0x00AC);  // logicalnot
    case 0xB4: return QChar(0x00A4);  // currency
    case 0xB5: return QChar(0x00AF);  // macron
    case 0xB6: return QChar(0x00AB);  // guillemotleft
    case 0xB7: return QChar(0x00BB);  // guillemotright
    case 0xB8: return QChar(0x00A6);  // brokenbar
    case 0xB9: return QChar(0x00A5);  // yen
    case 0xBA: return QChar(0x00B5);  // mu
    case 0xBB: return QChar(0x00D7);  // multiply
    case 0xBC: return QChar(0x00D8);  // Oslash
    case 0xBD: return QChar(0x00DE);  // Thorn
    case 0xBE: return QChar(0x00F1);  // ntilde
    case 0xBF: return QChar(0x00FE);  // thorn
    default: return QChar();
    }
}

// MacRoman 的 0x80-0x9F（西文拉丁变音；0xA0-0xFF 与 Latin-1 有出入但近似直通）
QChar macRomanChar(int code)
{
    switch (code) {
    case 0x80: return QChar(0x00C4); case 0x81: return QChar(0x00C5);
    case 0x82: return QChar(0x00C7); case 0x83: return QChar(0x00C9);
    case 0x84: return QChar(0x00D1); case 0x85: return QChar(0x00D6);
    case 0x86: return QChar(0x00DC); case 0x87: return QChar(0x00E1);
    case 0x88: return QChar(0x00E0); case 0x89: return QChar(0x00E2);
    case 0x8A: return QChar(0x00E4); case 0x8B: return QChar(0x00E3);
    case 0x8C: return QChar(0x00E5); case 0x8D: return QChar(0x00E7);
    case 0x8E: return QChar(0x00E9); case 0x8F: return QChar(0x00E8);
    case 0x90: return QChar(0x00EA); case 0x91: return QChar(0x00EB);
    case 0x92: return QChar(0x00ED); case 0x93: return QChar(0x00EC);
    case 0x94: return QChar(0x00EE); case 0x95: return QChar(0x00EF);
    case 0x96: return QChar(0x00F1); case 0x97: return QChar(0x00F3);
    case 0x98: return QChar(0x00F2); case 0x99: return QChar(0x00F4);
    case 0x9A: return QChar(0x00F5); case 0x9B: return QChar(0x00F6);
    case 0x9C: return QChar(0x00FA); case 0x9D: return QChar(0x00F9);
    case 0x9E: return QChar(0x00FB); case 0x9F: return QChar(0x00FC);
    default: return QChar();
    }
}

// 单字节码 → Unicode（按字体编码表；Unknown 时 0x80-0x9F 走 WinAnsi 兜底，
// 其余直通 Latin-1；未定义码返回空）
QChar standardEncodingChar(int code, PdfEncoding enc)
{
    if (code < 0x20 || code > 0xFF) {
        return QChar();
    }
    if (code <= 0x7E) {
        return QChar(code);   // ASCII 直通
    }
    if (enc == PdfEncoding::Standard && code >= 0xA0 && code <= 0xBF) {
        return standardChar(code);
    }
    if (enc == PdfEncoding::WinAnsi && code >= 0x80 && code <= 0x9F) {
        return winAnsiChar(code);
    }
    if (enc == PdfEncoding::MacRoman && code >= 0x80 && code <= 0x9F) {
        return macRomanChar(code);
    }
    if (code >= 0xA0) {
        return QChar(code);   // Latin-1 直通（含 WinAnsi 的 0xA0-0xFF）
    }
    // 0x80-0x9F：WinAnsi 兜底（多数实际 PDF 为 WinAnsi 系）
    return winAnsiChar(code);
}

// 文本解码：CID 字体（Type0）走 ToUnicode CMap（codeBytes 字节/码），
// 简单字体（Type1/TrueType）按 1 字节码查表（PDFium 重写后也带 1 字节码空间 CMap）。
// CMap miss / 无 CMap：查标准编码表；仍无 → 跳过该码（不产生控制字符/□）。
QString decodeText(const QByteArray &bytes, const FontInfo &font)
{
    QString out;
    const int cb = font.codeBytes;
    for (int i = 0; i + cb <= bytes.size(); i += cb) {
        int code = 0;
        for (int k = 0; k < cb; ++k) {
            code = (code << 8) | static_cast<uchar>(bytes.at(i + k));
        }
        QChar c;
        if (!font.toUnicode.isEmpty()) {
            c = font.toUnicode.value(code);
            if (c.isNull() && cb == 1) {
                c = standardEncodingChar(code, font.encoding);
            }
        } else if (cb == 1) {
            c = standardEncodingChar(code, font.encoding);
        } else {
            c = QChar(code);   // CID 无 CMap（罕见）：尽力保留
        }
        if (!c.isNull()) {
            const ushort u = c.unicode();
            // 过滤不可见/控制字符/私有区/替换符（显示 □ 的根源）
            if (u >= 0x20 && u != 0x7F && !(u >= 0x80 && u <= 0x9F)
                && !(u >= 0xE000 && u <= 0xF8FF) && u != 0xFFFD) {
                out += c;
            }
        }
    }
    // TeX CMap 把空格码 0x20 映射为可见空格占位 U+2423（␣）→ 归一化为普通空格
    out.replace(QChar(0x2423), QChar(0x20));
    return out;
}

// 解析 ToUnicode CMap（bfchar/bfrange 分段，按行匹配）→ CID → 字符映射。
// 注意：bfrange 正则若不限定在 beginbfrange..endbfrange 段内，会跨行
// 误匹配相邻 bfchar 条目（如 "<0002> <7B2C>\n<0003> <7B2C>" 被当作 range 展开）。
QHash<int, QChar> parseToUnicodeCMap(const QByteArray &cmap)
{
    QHash<int, QChar> map;
    const QString text = QString::fromLatin1(cmap);
    const QStringList lines = text.split(QRegularExpression("[\r\n]+"));
    bool inBfchar = false;
    bool inBfrange = false;
    const QRegularExpression pairRe("<([0-9A-Fa-f]{2,4})>\\s*<([0-9A-Fa-f]{2,4})>");
    const QRegularExpression rangeRe(
        "<([0-9A-Fa-f]{2,4})>\\s*<([0-9A-Fa-f]{2,4})>\\s*<([0-9A-Fa-f]{2,4})>");
    for (const QString &line : lines) {
        if (line.contains(QStringLiteral("beginbfchar"))) {
            inBfchar = true;
            inBfrange = false;
            continue;
        }
        if (line.contains(QStringLiteral("endbfchar"))) {
            inBfchar = false;
            continue;
        }
        if (line.contains(QStringLiteral("beginbfrange"))) {
            inBfrange = true;
            inBfchar = false;
            continue;
        }
        if (line.contains(QStringLiteral("endbfrange"))) {
            inBfrange = false;
            continue;
        }
        if (inBfchar) {
            const QRegularExpressionMatch m = pairRe.match(line);
            if (m.hasMatch()) {
                bool ok1 = false, ok2 = false;
                const int cid = m.captured(1).toInt(&ok1, 16);
                const int ucs = m.captured(2).toInt(&ok2, 16);
                if (ok1 && ok2) {
                    map.insert(cid, QChar(ucs));
                }
            }
        } else if (inBfrange) {
            // 数组形式: <lo> <hi> [<d0> <d1> ...]（QPdfWriter 等生成器的紧凑表达）
            const QRegularExpression arrRe(
                "<([0-9A-Fa-f]{2,4})>\\s*<([0-9A-Fa-f]{2,4})>\\s*\\[([^\\]]*)\\]");
            const QRegularExpressionMatch am = arrRe.match(line);
            if (am.hasMatch()) {
                const bool ok1 = false, ok2 = false;
                const int lo = am.captured(1).toInt(nullptr, 16);
                const int hi = am.captured(2).toInt(nullptr, 16);
                const QRegularExpression hexRe("^\\s*<([0-9A-Fa-f]{2,4})>\\s*$");
                int idx = 0;
                const QStringList arr =
                    am.captured(3).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                for (const QString &item : arr) {
                    const QRegularExpressionMatch hm2 = hexRe.match(item);
                    if (hm2.hasMatch() && lo + idx <= hi) {
                        map.insert(lo + idx, QChar(hm2.captured(1).toInt(nullptr, 16)));
                    }
                    ++idx;
                }
                continue;
            }
            const QRegularExpressionMatch m = rangeRe.match(line);
            if (m.hasMatch()) {
                bool ok1 = false, ok2 = false, ok3 = false;
                const int lo = m.captured(1).toInt(&ok1, 16);
                const int hi = m.captured(2).toInt(&ok2, 16);
                const int dst = m.captured(3).toInt(&ok3, 16);
                if (ok1 && ok2 && ok3 && hi >= lo) {
                    for (int c = lo; c <= hi; ++c) {
                        map.insert(c, QChar(dst + (c - lo)));
                    }
                }
            }
        }
    }
    return map;
}

// 从字典提取 /Key N 0 R 引用 → 对象号（-1 = 无）
int refNumber(const QByteArray &dict, const QByteArray &key)
{
    const int idx = dict.indexOf(key);
    if (idx < 0) {
        return -1;
    }
    const int refStart = idx + key.size();
    const int refEnd = dict.indexOf("R", refStart);
    if (refEnd < 0) {
        return -1;
    }
    const QList<QByteArray> parts = dict.mid(refStart, refEnd - refStart).simplified().split(' ');
    return parts.isEmpty() ? -1 : parts.first().toInt();
}

// 解析内容流：文本运行 + 图片引用（操作符解释器，pdf-service.md §9.2.1）
bool parseContentStream(const QByteArray &content,
                        const QHash<QString, FontInfo> &fonts,
                        PageContent &out)
{
    ContentLexer lexer(content);
    QString fontName;
    double fontSize = 0;
    QString color;
    bool bold = false, italic = false;
double tx = 0, ty = 0, tl = 0;
    // 文本矩阵 a/b/c/d（Tm 的旋转/翻转分量）：Td/TD/T*/'/" 的平移量必须经
    // 该矩阵变换后再累加——QPdfWriter 用 `1 0 0 -1 0 0 Tm`（y 翻转）定位文本，
    // 不映射则 Td 的 y 偏移符号错误 → 行序反转（readQpaintImagePdf 曾把
    // "After image" 排到 "Before image" 前面）。
    double tma = 1, tmb = 0, tmc = 0, tmd = 1;
    double ctm[6] = { 1, 0, 0, 1, 0, 0 };
    bool inText = false;

    struct Saved {
        QString fontName;
        double fontSize;
        QString color;
        bool bold, italic;
        double tx, ty, tl;
        double tma, tmb, tmc, tmd;
        double ctm[6];
    };
    QVector<Saved> stack;

    QVector<ContentToken> args;
    while (true) {
        const ContentToken t = lexer.next();
        if (t.type == ContentToken::Eof) {
            break;
        }
        if (t.type != ContentToken::Operator) {
            args.append(t);
            continue;
        }
        const QByteArray op = t.name;
        auto lastString = [&args]() -> const ContentToken * {
            for (int i = args.size() - 1; i >= 0; --i) {
                if (args.at(i).type == ContentToken::String) {
                    return &args.at(i);
                }
            }
            return nullptr;
        };
auto emitRun = [&](const QByteArray &bytes) {
            const FontInfo fi = fonts.value(fontName);
            // 文本矩阵平移 (tx,ty) 经 CTM 映射到页面坐标：QPdfWriter 导出的 PDF
            // 用 cm 平移定位每行（drawContents 前 painter.translate），不映射则
            // 所有行都落在同一 (tx,ty) → 全部合并成一行（曾致往返只剩 1 行）。
            const double x = ctm[0] * tx + ctm[2] * ty + ctm[4];
            const double y = ctm[1] * tx + ctm[3] * ty + ctm[5];
            const double fs = fontSize * std::sqrt(ctm[0] * ctm[0] + ctm[1] * ctm[1]);
            out.runs.append({ x, y, decodeText(bytes, fi),
                              fi.family, fs, color, bold, italic });
        };
        // PDF 矩阵乘法：m = a × b（列主序 2D 仿射）
        auto mulMatrix = [](double m[6], const double a[6], const double b[6]) {
            m[0] = a[0] * b[0] + a[1] * b[2];
            m[1] = a[0] * b[1] + a[1] * b[3];
            m[2] = a[2] * b[0] + a[3] * b[2];
            m[3] = a[2] * b[1] + a[3] * b[3];
            m[4] = a[4] * b[0] + a[5] * b[2] + b[4];
            m[5] = a[4] * b[1] + a[5] * b[3] + b[5];
        };

        if (op == "q") {
            Saved s;
            s.fontName = fontName;
            s.fontSize = fontSize;
            s.color = color;
            s.bold = bold;
            s.italic = italic;
s.tx = tx;
            s.ty = ty;
            s.tl = tl;
            s.tma = tma;
            s.tmb = tmb;
            s.tmc = tmc;
            s.tmd = tmd;
            std::copy(ctm, ctm + 6, s.ctm);
            stack.append(s);
        } else if (op == "Q") {
            if (!stack.isEmpty()) {
                const Saved s = stack.takeLast();
                fontName = s.fontName;
                fontSize = s.fontSize;
                color = s.color;
                bold = s.bold;
                italic = s.italic;
                tx = s.tx;
                ty = s.ty;
                tl = s.tl;
                tma = s.tma;
                tmb = s.tmb;
                tmc = s.tmc;
                tmd = s.tmd;
                std::copy(s.ctm, s.ctm + 6, ctm);
            }
        } else if (op == "cm") {
            if (args.size() >= 6) {
                const double m[6] = { args.at(0).number, args.at(1).number,
                                      args.at(2).number, args.at(3).number,
                                      args.at(4).number, args.at(5).number };
                double tmp[6];
                mulMatrix(tmp, m, ctm);
                std::copy(tmp, tmp + 6, ctm);
            }
} else if (op == "BT") {
            inText = true;
            tx = 0;
            ty = 0;
            tma = 1;
            tmb = 0;
            tmc = 0;
            tmd = 1;
        } else if (op == "ET") {
            inText = false;
        } else if (op == "Tf") {
            if (args.size() >= 2 && args.at(0).type == ContentToken::Name) {
                fontName = QString::fromLatin1(args.at(0).name);
                fontSize = args.at(1).number;
                const auto it = fonts.constFind(fontName);
                bold = it != fonts.constEnd() && it->bold;
                italic = it != fonts.constEnd() && it->italic;
            }
        } else if (op == "Td" || op == "TD") {
            if (args.size() >= 2) {
                const double ox = args.at(0).number;
                const double oy = args.at(1).number;
                tx += tma * ox + tmc * oy;
                ty += tmb * ox + tmd * oy;
                if (op == "TD") {
                    tl = -oy;
                }
            }
        } else if (op == "T*") {
            tx += tmc * (-tl);
            ty += tmd * (-tl);
        } else if (op == "Tm") {
            if (args.size() >= 6) {
                tma = args.at(0).number;
                tmb = args.at(1).number;
                tmc = args.at(2).number;
                tmd = args.at(3).number;
                tx = args.at(4).number;
                ty = args.at(5).number;
            }
        } else if (op == "Tj" || op == "'") {
            if (op == "'") {
                tx += tmc * (-tl);
                ty += tmd * (-tl);
            }
            const ContentToken *s = lastString();
            if (s && inText) {
                emitRun(s->str);
            }
        } else if (op == "\"") {
            if (args.size() >= 3) {
                const double ox = args.at(0).number;
                const double oy = args.at(1).number;
                tx += tma * ox + tmc * (-oy);
                ty += tmb * ox + tmd * (-oy);
                tl = -oy;
            }
            const ContentToken *s = lastString();
            if (s && inText) {
                emitRun(s->str);
            }
        } else if (op == "TJ") {
            if (!args.isEmpty() && args.last().type == ContentToken::Array && inText) {
                for (const ContentToken &e : args.last().array) {
                    if (e.type == ContentToken::String) {
                        emitRun(e.str);
                    } else if (e.type == ContentToken::Number && qAbs(e.number) > 100) {
                        // TJ 字距调整：|数| > 100（≈0.1em）视为词间距 → 空格；
                        // 小数值是字距微调（TeX kern），不产生空格
                        emitRun(" ");
                    }
                }
            }
        } else if (op == "rg" || op == "RG") {
            if (args.size() >= 3) {
                color = rgbToHex(args.at(0).number, args.at(1).number, args.at(2).number);
            }
        } else if (op == "g" || op == "G") {
            if (!args.isEmpty()) {
                const double v = args.first().number;
                color = rgbToHex(v, v, v);
            }
        } else if (op == "k" || op == "K") {
            if (args.size() >= 4) {
                color = cmykToHex(args.at(0).number, args.at(1).number,
                                  args.at(2).number, args.at(3).number);
            }
        } else if (op == "Do") {
            if (!args.isEmpty() && args.last().type == ContentToken::Name) {
                out.images.append({ QString::fromLatin1(args.last().name),
                                    ctm[4], ctm[5], qAbs(ctm[0]), qAbs(ctm[3]) });
            }
        }
        args.clear();
    }
    return true;
}

// 视觉行分组：y 相近的 runs 合并为一行，行内按 x 排序
struct LineLink {
    double x1 = 0, x2 = 0;   // 行内 x 区间（页面坐标）
    QString uri;
};

// 页面链接注释（/Annots /Link）：页面坐标矩形 + URI
struct PageLink {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    QString uri;
};

struct VisualLine {
    QVector<TextRun> runs;
    QVector<LineLink> links;   // 行内链接（/Annots /Link 提取，pdf-service.md §9.2.3）
    double y = 0;
};

QVector<VisualLine> groupIntoLines(QVector<TextRun> runs)
{
    std::sort(runs.begin(), runs.end(),
              [](const TextRun &a, const TextRun &b) { return a.y > b.y; });
    QVector<VisualLine> lines;
    for (const TextRun &r : runs) {
        if (lines.isEmpty()
            || qAbs(lines.last().y - r.y) > r.fontSize * 0.8) {
            lines.append({ {}, {}, r.y });
        }
        lines.last().runs.append(r);
    }
    for (VisualLine &l : lines) {
        std::sort(l.runs.begin(), l.runs.end(),
                  [](const TextRun &a, const TextRun &b) { return a.x < b.x; });
    }
    return lines;
}

// 合并相邻视觉行碎片：PDFium 等重写器把单词/短语拆到相邻视觉行
//（y 微差 + x 连续——渲染折行后下一行 x 回到行首，不满足连续性）。
// 判据：y 差 < 1.5 行高 且 下一行首 x 与上一行尾 x 连续（0 < 间距 < 0.5em）
// → 拼接为同一逻辑行，避免出现 "uction4" 这类孤立残片行。
void mergeLineFragments(QVector<VisualLine> &vlines)
{
    for (int i = 1; i < vlines.size(); ++i) {
        VisualLine &prev = vlines[i - 1];
        const VisualLine &cur = vlines.at(i);
        if (prev.runs.isEmpty() || cur.runs.isEmpty()) {
            continue;
        }
        const double fs = prev.runs.first().fontSize > 0
                              ? prev.runs.first().fontSize
                              : 10.0;
        const double yGap = cur.y - prev.y;
        const double xGap = cur.runs.first().x - prev.runs.last().x;
        if (yGap > 0 && yGap < fs * 1.5 && xGap > 0 && xGap < fs * 0.5) {
            prev.runs += cur.runs;   // cur 首 x > prev 尾 x，拼接后仍有序
            prev.y = (prev.y + cur.y) / 2.0;
            vlines.removeAt(i);
            --i;
        }
    }
}

QString escapeHtml(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
        .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return out;
}

// PDF 字体名 → 通用字族映射：嵌入 PDF 的字体名（LMRoman/Helvetica 等）在用户
// 机器上多不存在，直接写入 rich 会让 QML/导出端 fallback 到任意默认字体。
// 映射到 CSS 通用族（serif/sans-serif/monospace），渲染可控且与排版语义一致；
// 未知字体不写 family（用应用默认字体）。
QString mapPdfFontFamily(const QString &name)
{
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("courier")) || n.contains(QStringLiteral("menlo"))
        || n.contains(QStringLiteral("consolas")) || n.contains(QStringLiteral("cascadia"))
        || n.contains(QStringLiteral("monospace")) || n.contains(QStringLiteral("cmtt"))
        || n.contains(QStringLiteral("cmss")) || n.contains(QStringLiteral("lmtt"))
        || n.contains(QStringLiteral("lmss"))) {
        return QStringLiteral("monospace");
    }
    if (n.contains(QStringLiteral("helvetica")) || n.contains(QStringLiteral("arial"))
        || n.contains(QStringLiteral("nimbussans"))
        || n.contains(QStringLiteral("liberationsans"))
        || n.contains(QStringLiteral("calibri")) || n.contains(QStringLiteral("segoe"))
        || n.contains(QStringLiteral("simhei")) || n.contains(QStringLiteral("heiti"))
        || n.contains(QStringLiteral("yahei")) || n.contains(QStringLiteral("msyh"))
        || n.contains(QStringLiteral("dengxian")) || n.contains(QStringLiteral("lmsans"))
        || n.contains(QStringLiteral("lms")) || n.contains(QStringLiteral("cms"))) {
        return QStringLiteral("sans-serif");
    }
    if (n.contains(QStringLiteral("times")) || n.contains(QStringLiteral("nimbusrom"))
        || n.contains(QStringLiteral("georgia")) || n.contains(QStringLiteral("simsun"))
        || n.contains(QStringLiteral("fangsong")) || n.contains(QStringLiteral("kaiti"))
        || n.contains(QStringLiteral("stxihei")) || n.contains(QStringLiteral("stkaiti"))
        || n.contains(QStringLiteral("stsong")) || n.contains(QStringLiteral("notoserif"))
        || n.contains(QStringLiteral("sourcehan")) || n.contains(QStringLiteral("songti"))
        || n.contains(QStringLiteral("ming")) || n.contains(QStringLiteral("palatino"))
        || n.contains(QStringLiteral("bookman")) || n.contains(QStringLiteral("garamond"))
        || n.contains(QStringLiteral("lmroman")) || n.contains(QStringLiteral("latinmodern"))
        || n.contains(QStringLiteral("computer modern")) || n.contains(QStringLiteral("cmr"))
        || n.contains(QStringLiteral("cmbx")) || n.contains(QStringLiteral("cmti"))
        || n.contains(QStringLiteral("cmsy")) || n.contains(QStringLiteral("cmex"))) {
        return QStringLiteral("serif");
    }
    return QString();
}

QString linePlainText(const VisualLine &line, int indentSpaces)
{
    QString t;
    for (int i = 0; i < indentSpaces; ++i) {
        t += QLatin1Char(' ');
    }
    for (const TextRun &r : line.runs) {
        t += r.text;
    }
    return t;
}

// rich HTML（与 docx 格式一致：<b>/<i>/<span style>；行内链接包 <a>）。
// 字号用 pt（PDF 原始单位，QML/QTextDocument 均认），字族经 mapPdfFontFamily 映射；
// 行首缩进用 &nbsp; 表达（HTML 连续 nbsp 不合并，Text 渲染保真）。
QString lineRichHtml(const VisualLine &line, int indentSpaces)
{
    QString html;
    for (int i = 0; i < indentSpaces; ++i) {
        html += QStringLiteral("&nbsp;");
    }
    double rx = line.runs.isEmpty() ? 0.0 : line.runs.first().x;
    const double fontS = line.runs.isEmpty() ? 12.0 : line.runs.first().fontSize;
    for (const TextRun &r : line.runs) {
        QString inner = escapeHtml(r.text);
        QString uri;
        // run 无精确宽度（字体度量未追踪）→ 用 0.5em/字符近似累计 x
        const double rw = r.text.size() * fontS * 0.5;
        for (const LineLink &lk : line.links) {
            if (rx <= lk.x2 && rx + rw >= lk.x1) {
                uri = lk.uri;
                break;
            }
        }
        rx += rw;
        if (r.bold) {
            inner = QStringLiteral("<b>") + inner + QStringLiteral("</b>");
        }
        if (r.italic) {
            inner = QStringLiteral("<i>") + inner + QStringLiteral("</i>");
        }
        QString style;
        if (!r.color.isEmpty()) {
            style += QStringLiteral("color:%1;").arg(r.color);
        }
        if (r.fontSize > 0) {
            style += QStringLiteral("font-size:%1pt;").arg(r.fontSize);
        }
        const QString fam = mapPdfFontFamily(r.fontFamily);
        if (!fam.isEmpty()) {
            style += QStringLiteral("font-family:'%1';").arg(fam);
        }
        if (!style.isEmpty()) {
            inner = QStringLiteral("<span style=\"%1\">%2</span>").arg(style, inner);
        }
        if (!uri.isEmpty()) {
            inner = QStringLiteral("<a href=\"%1\">%2</a>").arg(escapeHtml(uri), inner);
        }
        html += inner;
    }
    return html;
}

// 解码嵌入图片对象 → (format, 编码字节)：DCTDecode = JPEG 原样；
// FlateDecode = 原始像素按 ColorSpace/BitsPerComponent 组装 QImage → PNG。
// 不支持的格式返回空（该图跳过）。
QPair<QString, QByteArray> decodeImageObject(const PdfObject &obj)
{
    const QByteArray &d = obj.dict;
    if (!d.contains("/Subtype") || !d.contains("/Image")) {
        return {};
    }
    if (d.contains("/ImageMask")) {
        return {};
    }
    if (d.contains("/DCTDecode")) {
        return { QStringLiteral("jpg"), obj.stream };
    }
    if (!d.contains("/FlateDecode") || obj.stream.isEmpty()) {
        return {};
    }
const QRegularExpression wRe("/Width\\s*(\\d+)");
    const QRegularExpression hRe("/Height\\s*(\\d+)");
    const QRegularExpression bRe("/BitsPerComponent\\s*(\\d+)");
    const QRegularExpression csRe("/ColorSpace\\s*/([A-Za-z0-9]+)");
    const QRegularExpressionMatch wm = wRe.match(QString::fromLatin1(d));
    const QRegularExpressionMatch hm = hRe.match(QString::fromLatin1(d));
    const QRegularExpressionMatch bm = bRe.match(QString::fromLatin1(d));
    if (!wm.hasMatch() || !hm.hasMatch() || !bm.hasMatch()) {
        return {};
    }
    const int w = wm.captured(1).toInt();
    const int h = hm.captured(1).toInt();
    const int bpc = bm.captured(1).toInt();
    if (w <= 0 || h <= 0 || w * h > 50 * 1024 * 1024) {
        return {};
    }
    const QByteArray raw = inflateStream(obj.stream);
    const QString cs = csRe.match(QString::fromLatin1(d)).captured(1);
    QImage img;
    if (cs == QStringLiteral("DeviceRGB") && bpc == 8) {
        const int stride = w * 3;
        if (raw.size() < stride * h) {
            return {};
        }
        img = QImage(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            std::memcpy(img.scanLine(y), raw.constData() + y * stride, stride);
        }
    } else if (cs == QStringLiteral("DeviceGray") && bpc == 8) {
        if (raw.size() < w * h) {
            return {};
        }
        img = QImage(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            std::memcpy(img.scanLine(y), raw.constData() + y * w, w);
        }
    } else if (cs == QStringLiteral("DeviceCMYK") && bpc == 8) {
        const int stride = w * 4;
        if (raw.size() < stride * h) {
            return {};
        }
        img = QImage(w, h, QImage::Format_RGB888);
        for (int y = 0; y < h; ++y) {
            const uchar *src = reinterpret_cast<const uchar *>(raw.constData() + y * stride);
            uchar *dst = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const double c = src[x * 4] / 255.0;
                const double m = src[x * 4 + 1] / 255.0;
                const double yy = src[x * 4 + 2] / 255.0;
                const double k = src[x * 4 + 3] / 255.0;
                dst[x * 3] = uchar((1.0 - qMin(1.0, c + k)) * 255.0);
                dst[x * 3 + 1] = uchar((1.0 - qMin(1.0, m + k)) * 255.0);
                dst[x * 3 + 2] = uchar((1.0 - qMin(1.0, yy + k)) * 255.0);
            }
        }
    } else {
        return {};   // 1/16bit、Indexed、ICCBased 等暂不支持
    }
    if (img.isNull()) {
        return {};
    }
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    buf.close();
    return { QStringLiteral("png"), png };
}

// 从对象字典提取 /Rect [x1 y1 x2 y2] 数字（链接注释用）
QVector<double> rectNumbers(const QByteArray &dict)
{
    const QRegularExpression re("/Rect\\s*\\[([^\\]]+)\\]");
    const QRegularExpressionMatch m = re.match(QString::fromLatin1(dict));
    if (!m.hasMatch()) {
        return {};
    }
    QVector<double> nums;
    const QRegularExpression numRe("[-+]?\\d+(?:\\.\\d+)?");
    QRegularExpressionMatchIterator it = numRe.globalMatch(m.captured(1));
    while (it.hasNext()) {
        nums.append(it.next().captured().toDouble());
    }
    return nums;
}

} // namespace

// v3 富文本导入主流程：解析页面对象 → 内容流 → 视觉行 → 模型 rich 显示层。
// 失败返回 false（调用方回退纯文本按行导入）。
namespace {
bool importRichPdf(const QByteArray &data, DocumentModel *model,
                   QVariantMap &meta, QString *error)
{
    QVector<PdfObject> objects;
    if (!parsePdfObjects(data, objects)) {
        setError(error, QStringLiteral("PDF 对象解析失败"));
        return false;
    }

    // 页面对象（按文件偏移顺序 = 页序近似）
    // 兼容紧凑格式（PDFium 等生成器输出 /Type/Page 无空格）
    QVector<const PdfObject *> pages;
    const QRegularExpression pageRe("/Type\\s*/Page(?:\\s|>>|/)");
    for (const PdfObject &obj : objects) {
        if (pageRe.match(QString::fromLatin1(obj.dict)).hasMatch()) {
            pages.append(&obj);
        }
    }
    if (pages.isEmpty()) {
        setError(error, QStringLiteral("未找到页面对象"));
        return false;
    }

    QHash<int, int> objIndex;
    for (int i = 0; i < objects.size(); ++i) {
        objIndex.insert(objects.at(i).number, i);
    }

    QStringList lines;
    QVector<QPair<int, QString>> richLines;
    QVector<QPair<int, QStringList>> imageLines;
    QVector<QPair<int, QStringList>> mixedImageIds;
    QVariantList imageList;
    QHash<QString, QPair<QString, QByteArray>> imageCache;   // XObject 名 → 格式,数据（跨页缓存）
    int imgSeq = 0;

    for (const PdfObject *page : pages) {
        QVector<PageLink> pageLinks;

// ---- 内容流：/Contents N 0 R 或 [/Contents N 0 R M 0 R] ----
        QByteArray content;
        const int ci = objIndex.value(refNumber(page->dict, "/Contents"), -1);
        if (ci >= 0 && objects.at(ci).hasStream) {
            content = objects.at(ci).dict.contains("/FlateDecode")
                          ? inflateStream(objects.at(ci).stream)
                          : objects.at(ci).stream;
        }
        // 间接数组：/Contents 228 0 R 指向 [ 33 0 R ... ]（PDFium 重写格式）。
        // 数组可能位于对象头下一行（raw 以 [ 开头）或与 obj 同行（raw 丢失，
        // 需从对象偏移处取完整字节，截到 endobj 防误匹配下一对象）。
        if (ci >= 0 && content.isEmpty()) {
            const PdfObject &co = objects.at(ci);
            QByteArray arr;
            if (co.dict.startsWith('[')) {
                arr = co.dict.trimmed();
            } else if (co.raw.trimmed().startsWith('[')) {
                arr = co.raw.trimmed();
            } else if (co.off >= 0) {
                const QByteArray full =
                    data.mid(static_cast<int>(co.off), co.raw.size() + 96);
                const int br = full.indexOf('[');
                const int eb = full.indexOf("endobj", br);
                if (br >= 0 && eb > br) {
                    arr = full.mid(br, eb - br).trimmed();
                }
            }
            if (arr.startsWith('[')) {
                const QRegularExpression refRe("(\\d+)\\s+0\\s+R");
                auto it = refRe.globalMatch(QString::fromLatin1(arr));
                while (it.hasNext()) {
                    const int idx = objIndex.value(it.next().captured(1).toInt(), -1);
                    if (idx >= 0 && objects.at(idx).hasStream) {
                        content += objects.at(idx).dict.contains("/FlateDecode")
                                       ? inflateStream(objects.at(idx).stream)
                                       : objects.at(idx).stream;
                    }
                }
            }
        }
        if (content.isEmpty()) {
            const int arrStart = page->dict.indexOf("/Contents");
            if (arrStart >= 0) {
                const int br = page->dict.indexOf('[', arrStart);
                const int er = page->dict.indexOf(']', br);
                if (br >= 0 && er > br) {
                    const QList<QByteArray> refs =
                        page->dict.mid(br + 1, er - br - 1).simplified().split(' ');
                    for (int k = 0; k + 2 < refs.size(); k += 3) {
                        const int idx = objIndex.value(refs.at(k).toInt(), -1);
                        if (idx >= 0 && objects.at(idx).hasStream) {
                            content += objects.at(idx).dict.contains("/FlateDecode")
                                           ? inflateStream(objects.at(idx).stream)
                                           : objects.at(idx).stream;
                        }
                    }
                }
            }
        }

        // ---- 字体字典：/Resources N 0 R 或页面内联 ----
        QHash<QString, FontInfo> fonts;
        const int ri = objIndex.value(refNumber(page->dict, "/Resources"), -1);
        const QByteArray resDict = ri >= 0 ? objects.at(ri).dict : page->dict;
        const int fontStart = resDict.indexOf("/Font");
        if (fontStart >= 0) {
            const int fs = resDict.indexOf("<<", fontStart);
            const int fe = resDict.indexOf(">>", fs);
            if (fs >= 0 && fe > fs) {
                const QByteArray fontDict = resDict.mid(fs, fe - fs + 2);
                const QRegularExpression fontRe("/([A-Za-z0-9_]+)\\s+(\\d+)\\s+0\\s+R");
                QRegularExpressionMatchIterator it = fontRe.globalMatch(
                    QString::fromLatin1(fontDict));
                while (it.hasNext()) {
                    const QRegularExpressionMatch m = it.next();
                    const QString name = m.captured(1);
                    const int fIdx = objIndex.value(m.captured(2).toInt(), -1);
                    if (fIdx < 0) {
                        continue;
                    }
                    const QByteArray fd = objects.at(fIdx).dict;
                    FontInfo fi;
                    const QRegularExpression subRe("/Subtype\\s*/([A-Za-z0-9_]+)");
                    const QRegularExpressionMatch subm = subRe.match(QString::fromLatin1(fd));
                    if (subm.hasMatch() && subm.captured(1) == QStringLiteral("Type0")) {
                        fi.codeBytes = 2;   // CID 字体多字节码
                    }
                    const QRegularExpression bfRe("/BaseFont\\s*/([A-Za-z0-9_+#.-]+)");
                    const QRegularExpressionMatch bfm = bfRe.match(QString::fromLatin1(fd));
                    if (bfm.hasMatch()) {
                        QString fam = bfm.captured(1);
                        const int plus = fam.indexOf(QLatin1Char('+'));
                        if (plus >= 0) {
                            fam = fam.mid(plus + 1);
                        }
                        fi.family = fam;
                        const QString lower = fam.toLower();
                        fi.bold = lower.contains(QStringLiteral("bold"))
                                  || lower.contains(QStringLiteral("black"))
                                  || lower.contains(QStringLiteral("heavy"));
                        fi.italic = lower.contains(QStringLiteral("italic"))
                                    || lower.contains(QStringLiteral("oblique"));
                    }
                    const int fdi = objIndex.value(refNumber(fd, "/FontDescriptor"), -1);
                    if (fdi >= 0) {
                        const QRegularExpression flRe("/Flags\\s*(\\d+)");
                        const QRegularExpressionMatch flm =
                            flRe.match(QString::fromLatin1(objects.at(fdi).dict));
                        if (flm.hasMatch()) {
                            const int flags = flm.captured(1).toInt();
                            if (flags & 2) {
                                fi.italic = true;
                            }
                            if (flags & 32) {
                                fi.bold = true;
                            }
                        }
                    }
                    const int tui = objIndex.value(refNumber(fd, "/ToUnicode"), -1);
                    if (tui >= 0 && objects.at(tui).hasStream) {
                        const QByteArray cmap = objects.at(tui).dict.contains("/FlateDecode")
                                                    ? inflateStream(objects.at(tui).stream)
                                                    : objects.at(tui).stream;
                        fi.toUnicode = parseToUnicodeCMap(cmap);
                    }
                    // 标准编码表（无 ToUnicode / CMap miss 时的解码依据）
                    if (fd.contains("/WinAnsiEncoding")) {
                        fi.encoding = PdfEncoding::WinAnsi;
                    } else if (fd.contains("/MacRomanEncoding")) {
                        fi.encoding = PdfEncoding::MacRoman;
                    } else if (fd.contains("/StandardEncoding")) {
                        fi.encoding = PdfEncoding::Standard;
                    }
                    fonts.insert(name, fi);
                }
            }
        }

        // ---- XObject 图片字典：/XObject << /Im0 5 0 R ... >> ----
        QHash<QString, int> xobjects;
        const int xoStart = resDict.indexOf("/XObject");
        if (xoStart >= 0) {
            const int xs = resDict.indexOf("<<", xoStart);
            const int xe = resDict.indexOf(">>", xs);
            if (xs >= 0 && xe > xs) {
                const QByteArray xoDict = resDict.mid(xs, xe - xs + 2);
                const QRegularExpression xoRe("/([A-Za-z0-9_]+)\\s+(\\d+)\\s+0\\s+R");
                QRegularExpressionMatchIterator xit =
                    xoRe.globalMatch(QString::fromLatin1(xoDict));
                while (xit.hasNext()) {
                    const QRegularExpressionMatch m = xit.next();
                    const int idx = objIndex.value(m.captured(2).toInt(), -1);
                    if (idx >= 0) {
                        xobjects.insert(m.captured(1), idx);
                    }
                }
            }
        }

        // ---- 链接注释：/Annots [N 0 R ...] → /Link /Rect + /A /URI ----
        // PDFium 重写格式无 /Annots 挂接：注释对象孤立存在，按对象号区间
        // （页面对象号 .. 下一页面对象号）归页（对象号随页序递增）
        auto parseLink = [](const QByteArray &ad,
                            QVector<PageLink> &out) {
            if (!ad.contains("/Link")) {
                return;
            }
            const QVector<double> rect = rectNumbers(ad);
            if (rect.size() < 4) {
                return;
            }
            const int us = ad.indexOf("/URI");
            if (us < 0) {
                return;
            }
            QString uri;
            const int up = ad.indexOf('(', us);
            if (up >= 0) {
                const int ue = ad.indexOf(')', up);
                uri = QString::fromLatin1(ad.mid(up + 1, ue - up - 1));
            } else {
                const int hp = ad.indexOf('<', us);
                if (hp >= 0) {
                    const int he = ad.indexOf('>', hp);
                    const QByteArray hex = ad.mid(hp + 1, he - hp - 1);
                    uri = QString::fromLatin1(QByteArray::fromHex(hex));
                }
            }
            if (uri.isEmpty()) {
                return;
            }
            out.append({ rect.at(0), rect.at(1), rect.at(2), rect.at(3), uri });
        };
        const int annStart = page->dict.indexOf("/Annots");
        if (annStart >= 0) {
            const int br = page->dict.indexOf('[', annStart);
            const int er = page->dict.indexOf(']', br);
            if (br >= 0 && er > br) {
                const QList<QByteArray> refs =
                    page->dict.mid(br + 1, er - br - 1).simplified().split(' ');
                for (int k = 0; k + 2 < refs.size(); k += 3) {
                    const int idx = objIndex.value(refs.at(k).toInt(), -1);
                    if (idx < 0) {
                        continue;
                    }
                    parseLink(objects.at(idx).dict, pageLinks);
                }
            }
        } else {
            // 兜底：无 /Annots（PDFium 重写）→ 扫描页面对象号区间内的 /Link 注释
            const qint64 pageNum = page->number;
            qint64 nextPageNum = INT64_MAX;
            for (const PdfObject *p : pages) {
                if (p->number > pageNum) {
                    nextPageNum = p->number;
                    break;
                }
            }
            for (const PdfObject &o : objects) {
                if (o.number <= pageNum || o.number >= nextPageNum) {
                    continue;
                }
                parseLink(o.dict, pageLinks);
            }
        }

// ---- 解析内容流 → 视觉行 ----
        PageContent pc;
        if (!content.isEmpty()) {
            parseContentStream(content, fonts, pc);
        }
        QVector<VisualLine> vlines = groupIntoLines(pc.runs);
        mergeLineFragments(vlines);   // 防御：重写器把单词拆到相邻视觉行 → 拼接
        if (vlines.isEmpty()) {
            vlines.append({ {}, {}, 0.0 });   // 纯图页 → 虚拟空行（图片归属到该行）
        }

        // ---- 链接归属：矩形 y 范围与行重叠 → 行内 <a> 区间 ----
        for (const PageLink &pl : pageLinks) {
            for (int li = 0; li < vlines.size(); ++li) {
                const double lineH = vlines.at(li).runs.isEmpty()
                                         ? 12.0
                                         : vlines.at(li).runs.first().fontSize;
                const double lineY = vlines.at(li).y;
                if (lineY + lineH / 2 > pl.y1 && lineY - lineH / 2 < pl.y2) {
                    vlines[li].links.append({ pl.x1, pl.x2, pl.uri });
                }
            }
        }

        // ---- 图片归属：中心 y 匹配最近视觉行 ----
        QVector<QStringList> lineImg(vlines.size());
        for (const ImageRef &ir : pc.images) {
            if (!xobjects.contains(ir.name)) {
                continue;
            }
            const PdfObject &imgObj = objects.at(xobjects.value(ir.name));
            auto it = imageCache.constFind(ir.name);
            QPair<QString, QByteArray> img;
            if (it != imageCache.constEnd()) {
                img = it.value();
            } else {
                img = decodeImageObject(imgObj);
                if (!img.second.isEmpty()) {
                    imageCache.insert(ir.name, img);
                }
            }
            if (img.second.isEmpty()) {
                continue;
            }
const QRegularExpression wRe("/Width\\s*(\\d+)");
            const QRegularExpression hRe("/Height\\s*(\\d+)");
            const QRegularExpressionMatch wm = wRe.match(QString::fromLatin1(imgObj.dict));
            const QRegularExpressionMatch hm = hRe.match(QString::fromLatin1(imgObj.dict));
            if (!wm.hasMatch() || !hm.hasMatch()) {
                continue;
            }
            const QString id = QStringLiteral("pdf_img_%1").arg(imgSeq++);
            QVariantMap im;
            im.insert(QStringLiteral("id"), id);
            im.insert(QStringLiteral("format"), img.first);
            im.insert(QStringLiteral("mode"), QStringLiteral("inline"));
            im.insert(QStringLiteral("dataBase64"),
                      QString::fromLatin1(img.second.toBase64()));
            imageList.append(im);

            const double dispH = hm.captured(1).toInt() * ir.sy;
            const double cy = ir.y + dispH / 2;
            int best = -1;
            double bestDist = 1e18;
            for (int li = 0; li < vlines.size(); ++li) {
                const double d = qAbs(vlines.at(li).y - cy);
                if (d < bestDist) {
                    bestDist = d;
                    best = li;
                }
            }
            if (best >= 0) {
                lineImg[best].append(id);
            }
        }

        // ---- 行首缩进：相对页面左边界（正文列的 run x 众数）的偏移 → 空格数 ----
        //（段落首行缩进/代码块/列表缩进对可读性重要；用众数避免居中标题/
        //  页眉被误判；上限 6 空格防止居中排版过度缩进；0.25em 以下视为噪声）
        QVector<int> lineIndent(vlines.size(), 0);
        QHash<int, int> xFreq;
        for (const TextRun &r : pc.runs) {
            ++xFreq[qRound(r.x)];
        }
        int pageXBase = 0;
        int bestFreq = 0;
        for (auto it = xFreq.constBegin(); it != xFreq.constEnd(); ++it) {
            if (it.value() > bestFreq) {
                bestFreq = it.value();
                pageXBase = it.key();
            }
        }
        for (int li = 0; li < vlines.size(); ++li) {
            const VisualLine &vl = vlines.at(li);
            if (vl.runs.isEmpty()) {
                continue;
            }
            const double left = vl.runs.first().x;
            const double fs = vl.runs.first().fontSize > 0 ? vl.runs.first().fontSize : 10.0;
            const double gap = left - pageXBase;
            if (gap > fs * 0.25) {
                lineIndent[li] = qMax(1, qMin(6, qRound(gap / (fs * 0.5))));
            }
        }

        // ---- 行生成 ----
        for (int li = 0; li < vlines.size(); ++li) {
            const VisualLine &vl = vlines.at(li);
            const QString plain = normalizeLineText(linePlainText(vl, lineIndent.at(li)));
            lines.append(plain);
            QString html = lineRichHtml(vl, lineIndent.at(li));
            if (!lineImg.at(li).isEmpty() && !vl.runs.isEmpty()) {
                for (int k = 0; k < lineImg.at(li).size(); ++k) {
                    html += QStringLiteral("[图片]");   // 每张图一个占位（与 docx 一致）
                }
            }
            const int lineNo = lines.size() - 1;
            if (html != escapeHtml(plain)) {
                richLines.append(qMakePair(lineNo, html));
            }
            if (!lineImg.at(li).isEmpty()) {
                if (vl.runs.isEmpty()) {
                    imageLines.append(qMakePair(lineNo, lineImg.at(li)));   // 纯图行
                } else {
                    mixedImageIds.append(qMakePair(lineNo, lineImg.at(li)));
                }
            }
        }
    }

    if (lines.isEmpty()) {
        setError(error, QStringLiteral("PDF 无文本内容"));
        return false;
    }

    // meta 先于模型填充就绪：setLines 触发 QML delegate 绑定求值时
    //（imageSource 查 meta.images 渲染 <img>）meta 必须已包含图片数据
    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("pdf"));
    if (!imageList.isEmpty()) {
        meta.insert(QStringLiteral("images"), imageList);
    }

    model->setLines(lines);
    for (const QPair<int, QString> &p : richLines) {
        model->setLineRich(p.first, p.second);
        model->setLineDisplay(p.first, QStringLiteral("rich"));
    }
    for (const QPair<int, QStringList> &p : imageLines) {
        model->setLineImages(p.first, p.second);
        model->setLineDisplay(p.first, QStringLiteral("image"));
    }
    for (const QPair<int, QStringList> &p : mixedImageIds) {
        model->setLineImages(p.first, p.second);   // 图文混排：图片 ID 附加（显示层保持 rich）
    }
    return true;
}
} // namespace

// 修复导出 PDF 的文本层：重建 ToUnicode CMap 并重写 PDF（对象重序列化 + 新 xref）。
// 防御：内容流 CID 唯一数与 textSeq 唯一字符数不一致（多字体 fallback/缺字）→ 放弃修复。
bool PdfParser::repairTextLayer(const QString &path, const QString &textSeq)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    file.close();

    QVector<PdfObject> objects;
    if (!parsePdfObjects(data, objects)) {
        return false;
    }

    // 1) 定位 ToUnicode 对象（被字体字典引用）
    int toUnicodeIdx = -1;
    for (int i = 0; i < objects.size(); ++i) {
        const int target = toUnicodeObjNumber(objects.at(i).dict);
        if (target < 0) {
            continue;
        }
        for (int j = 0; j < objects.size(); ++j) {
            if (objects.at(j).number == target) {
                toUnicodeIdx = j;
                break;
            }
        }
        if (toUnicodeIdx >= 0) {
            break;
        }
    }
    if (toUnicodeIdx < 0) {
        return false;
    }

    // 2) 内容流 CID 序列（全部内容流聚合，按 Tj 出现顺序含重复）
    QList<int> cids;
    for (const PdfObject &obj : objects) {
        if (!obj.hasStream || !obj.dict.contains("/FlateDecode")) {
            continue;
        }
        const QByteArray content = inflateStream(obj.stream);
        if (content.isEmpty() || !content.contains(" Tj")) {
            continue;
        }
        cids += contentCidSequence(content);
    }
    if (cids.isEmpty()) {
        return false;
    }

    // 3) 生成正确 CMap：内容流 CID ↔ 导出文本对齐
    const QByteArray correctCmap = buildCMapFromText(cids, textSeq);
    if (correctCmap.isEmpty()) {
        return false;
    }

    // 重建文件：%PDF 头 + 对象重序列化（ToUnicode 对象替换为新 CMap 流）+ 新 xref
    //（对象按编号排序输出——xref 按编号索引）
    QByteArray out = "%PDF-1.4\n";
    QVector<qint64> offsets;
    offsets.reserve(objects.size());
    QVector<int> order(objects.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&objects](int a, int b) { return objects.at(a).number < objects.at(b).number; });
    int toUnicodeOut = -1;
    for (int i = 0; i < order.size(); ++i) {
        const int idx = order.at(i);
        offsets.append(out.size());
        const PdfObject &obj = objects.at(idx);
        out += QByteArray::number(obj.number) + " 0 obj\n";
        if (idx == toUnicodeIdx) {
            // 新 CMap 流：明文写入（不压缩）——压缩流二进制可能含 "endstream" 字节
            // 序列导致解析错乱；原 QPdfWriter 的 ToUnicode 本就是明文流
            out += "<< /Length " + QByteArray::number(correctCmap.size()) + " >>\n";
            out += "stream\n" + correctCmap + "\nendstream\n";
        } else {
            out += obj.raw;
            if (obj.hasStream) {
                // raw 含 dict + 流字节，需补回 endstream 标记
                out += "\nendstream\n";
            }
        }
        out += "\nendobj\n";
        if (idx == toUnicodeIdx) {
            toUnicodeOut = offsets.last();
        }
    }

    // xref + trailer（/Root 沿用原值；跳过 "/Root" 自身的 R，从引用数字后找）
    QByteArray rootRef = "/Root 1 0 R";
    const int trailerPos = data.indexOf("trailer");
    if (trailerPos >= 0) {
        const int rootIdx = data.indexOf("/Root", trailerPos);
        if (rootIdx >= 0) {
            const int refEnd = data.indexOf("R", rootIdx + 6);
            if (refEnd >= 0) {
                rootRef = data.mid(rootIdx, refEnd - rootIdx + 1).simplified();
            }
        }
    }
    const qint64 xrefOffset = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (const qint64 off : offsets) {
        out += QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer << /Size " + QByteArray::number(objects.size() + 1)
           + " " + rootRef + " >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";

    QFile outFile(path);
    if (!outFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    outFile.write(out);
    outFile.close();
    return true;
}

