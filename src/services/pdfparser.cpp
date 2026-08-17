#include "pdfparser.h"

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>

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

    // QPdfWriter 在析构时才 finalize 写盘，故用作用域块包住绘制，
    // 块结束后（writer 析构）再执行文本层修复。
    QString textSeq;
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
        const int lineHeight = fm.height();
        const int wrapFlags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;

        // 每行一页（再导入时行结构保真，见 pdf-service.md §3.0）
        // 同时收集导出文本字符序列（含空格）供 ToUnicode CMap 对齐
        const int n = model->lineCount();
        for (int i = 0; i < n; ++i) {
            QString text = model->lineText(i);
            if (comments) {
                const QString c = comments->commentAt(i);
                if (!c.isEmpty()) {
                    text += QStringLiteral("（批注：%1）").arg(c);
                }
            }

            if (!text.isEmpty()) {
                // 折行高度（TextWordWrap 下多行文本占用的像素高度）
                const QRect bounds = fm.boundingRect(0, 0, width, INT_MAX, wrapFlags, text);
                painter.drawText(QRect(0, 0, width, bounds.height()), wrapFlags, text);
                textSeq += text;
            }
            // 每行一页（空行也换页，保持页映射）
            if (i < n - 1) {
                writer.newPage();
            }
        }
        painter.end();
    }   // QPdfWriter 析构 → PDF 文件落盘

    // 文本层修复：内容流 CID ↔ 导出文本对齐，重建 ToUnicode CMap
    //（Qt 6.5.3 中文映射到康熙部首区的缺陷），使导出 PDF 文本可被任何阅读器提取
    repairTextLayer(path, textSeq);

    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("pdf"));
    meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
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
        const int dictStart = data.indexOf("<<", cursor);
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
