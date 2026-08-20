#include "docxparser.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#include "documentmodel.h"
#include "commentservice.h"

namespace {

// ---- docx 命名空间 ----
const QString kWordNs    = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kRelNs     = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString kDrawNs    = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kVmlNs     = QStringLiteral("urn:schemas-microsoft-com:vml");
const QString kPkgRelNs  = QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships");

// 一个 run（w:r）累积的文本 + 格式 + 行内图片引用
struct RunPiece {
    QString text;        // 纯文本（\t 已转义、\n 表示换行）
    bool bold = false;
    bool italic = false;
    QString color;       // #RRGGBB（无 # 前缀时补）
    int szHalf = 0;      // 半磅（w:sz val）
    QString font;        // rFonts ascii
    QString imageId;     // rels 的 rId（行内图片）
    bool hasImage = false;
};

bool isNs(const QXmlStreamReader &r, const QString &ns, const char *local)
{
    return r.namespaceUri() == ns && r.name() == QLatin1String(local);
}

// 解析 w:rPr 的直接 run 属性（b/i/color/sz/rFonts）。当前位于 w:rPr StartElement。
void parseRunProps(QXmlStreamReader &r, RunPiece &piece)
{
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            if (r.namespaceUri() != kWordNs) {
                continue;
            }
            if (r.name() == QLatin1String("b")) {
                piece.bold = true;
            } else if (r.name() == QLatin1String("i")) {
                piece.italic = true;
            } else if (r.name() == QLatin1String("color")) {
                // 属性带 w: 前缀（qualifiedName=w:val），须按 namespace+localName 读取
                QString v = r.attributes().value(kWordNs, QLatin1String("val")).toString();
                if (!v.isEmpty() && !v.startsWith(QLatin1Char('#'))) {
                    v.prepend(QLatin1Char('#'));
                }
                piece.color = v;
            } else if (r.name() == QLatin1String("sz")) {
                piece.szHalf = r.attributes().value(kWordNs, QLatin1String("val")).toInt();
            } else if (r.name() == QLatin1String("rFonts")) {
                piece.font = r.attributes().value(kWordNs, QLatin1String("ascii")).toString();
            }
        } else if (t == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
}

// 从 w:drawing / w:pict 内查找图片引用 rId。当前位于容器 StartElement。
QString findImageId(QXmlStreamReader &r, const char *containerLocal)
{
    Q_UNUSED(containerLocal)
    int depth = 1;
    while (!r.atEnd() && depth > 0) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            if (r.name() == QLatin1String("blip") && r.namespaceUri() == kDrawNs) {
                const QString embed = r.attributes().value(kRelNs, QLatin1String("embed")).toString();
                if (!embed.isEmpty()) {
                    return embed;
                }
                const QString link = r.attributes().value(kRelNs, QLatin1String("link")).toString();
                if (!link.isEmpty()) {
                    return link;
                }
            } else if (r.name() == QLatin1String("imagedata") && r.namespaceUri() == kVmlNs) {
                const QString id = r.attributes().value(kRelNs, QLatin1String("id")).toString();
                if (!id.isEmpty()) {
                    return id;
                }
            }
        } else if (t == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    return QString();
}

// 解析 word/_rels/document.xml.rels：rId → media 路径
QHash<QString, QString> readRelationships(const QByteArray &xml)
{
    QHash<QString, QString> map;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("Relationship")
            && r.namespaceUri() == kPkgRelNs) {
            const QString id = r.attributes().value(QLatin1String("Id")).toString();
            QString target = r.attributes().value(QLatin1String("Target")).toString();
            if (id.isEmpty() || target.isEmpty()) {
                continue;
            }
            if (target.startsWith(QLatin1Char('/'))) {
                target.remove(0, 1);
            }
            map.insert(id, target);
        }
    }
    return map;
}

bool readFileFromZip(QuaZip &zip, const QString &name, QByteArray &out)
{
    if (!zip.setCurrentFile(name)) {
        return false;
    }
    QuaZipFile file(&zip);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    out = file.readAll();
    file.close();
    return true;
}

QString escapeHtml(QString t)
{
    t.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    t.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    t.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    t.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return t;
}

// 解析 word/comments.xml → { w:id → 批注文本 }（native 导出模式）
QHash<int, QString> readComments(const QByteArray &xml)
{
    QHash<int, QString> map;
    QXmlStreamReader r(xml);
    int curId = -1;
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (isNs(r, kWordNs, "comment")) {
                curId = r.attributes().value(kWordNs, QLatin1String("id")).toInt();
            } else if (isNs(r, kWordNs, "t") && curId >= 0) {
                map.insert(curId, r.readElementText());
            }
        } else if (t == QXmlStreamReader::EndElement && isNs(r, kWordNs, "comment")) {
            curId = -1;
        }
    }
    return map;
}

} // namespace

bool DocxParser::read(const QString &path, DocumentModel *model,
                      CommentService *comments, QVariantMap &meta, QString *error)
{
    if (!model) {
        if (error) {
            *error = QStringLiteral("未关联文档模型");
        }
        return false;
    }

    QuaZip zip(path);
    if (!zip.open(QuaZip::mdUnzip)) {
        if (error) {
            *error = QStringLiteral("无法打开 docx（不是有效的 zip）：%1").arg(path);
        }
        return false;
    }

    QByteArray docXml;
    if (!readFileFromZip(zip, QStringLiteral("word/document.xml"), docXml)) {
        zip.close();
        if (error) {
            *error = QStringLiteral("docx 缺少 word/document.xml，不是有效的 Word 文档");
        }
        return false;
    }
    QHash<QString, QString> rels;
    QByteArray relsXml;
    if (readFileFromZip(zip, QStringLiteral("word/_rels/document.xml.rels"), relsXml)) {
        rels = readRelationships(relsXml);
    }
    // 原生批注（native 导出模式）：comments.xml → { id → 文本 }
    QHash<int, QString> commentById;
    QByteArray commentsXml;
    if (readFileFromZip(zip, QStringLiteral("word/comments.xml"), commentsXml)) {
        commentById = readComments(commentsXml);
    }

    // ---- 解析 document.xml → 段落列表 ----
    QVector<QVector<RunPiece>> paragraphs;
    QVector<RunPiece> cur;
    int curCommentId = -1; // 当前段落的原生批注 id（commentRangeStart）
    QXmlStreamReader r(docXml);
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            if (isNs(r, kWordNs, "p")) {
                cur.clear();
            } else if (isNs(r, kWordNs, "commentRangeStart")) {
                curCommentId = r.attributes().value(kWordNs, QLatin1String("id")).toInt();
            } else if (isNs(r, kWordNs, "r")) {
                cur.append(RunPiece());
            } else if (isNs(r, kWordNs, "t")) {
                if (!cur.isEmpty()) {
                    cur.last().text += r.readElementText();
                }
            } else if (isNs(r, kWordNs, "tab")) {
                if (!cur.isEmpty()) {
                    cur.last().text += QLatin1Char('\t');
                }
            } else if (isNs(r, kWordNs, "br")) {
                if (!cur.isEmpty()) {
                    cur.last().text += QLatin1Char('\n');
                }
            } else if (isNs(r, kWordNs, "rPr")) {
                if (!cur.isEmpty()) {
                    parseRunProps(r, cur.last());
                }
            } else if ((r.namespaceUri() == kWordNs || r.namespaceUri() == kDrawNs)
                       && (r.name() == QLatin1String("drawing") || r.name() == QLatin1String("pict"))) {
                if (!cur.isEmpty()) {
                    const QString id = findImageId(r, nullptr);
                    if (!id.isEmpty()) {
                        cur.last().imageId = id;
                        cur.last().hasImage = true;
                    }
                }
            }
        } else if (t == QXmlStreamReader::EndElement && isNs(r, kWordNs, "p")) {
            if (comments && curCommentId >= 0 && commentById.contains(curCommentId)) {
                comments->setComment(paragraphs.size(), commentById.value(curCommentId));
            }
            curCommentId = -1;
            paragraphs.append(cur);
            cur.clear();
        }
    }

    // ---- 图片读取（rId → media 路径 → base64；需在 zip 关闭前）----
    QStringList imageIdOrder; // 出现顺序去重
    for (const QVector<RunPiece> &par : paragraphs) {
        for (const RunPiece &pc : par) {
            if (pc.hasImage && !imageIdOrder.contains(pc.imageId)) {
                imageIdOrder.append(pc.imageId);
            }
        }
    }

    QVariantList imageList;
    QHash<QString, QByteArray> imageData;      // rId → base64 字节（data URI 用）
    const QString wordPrefix = QStringLiteral("word/");
    for (const QString &rId : imageIdOrder) {
        const QString target = rels.value(rId);
        if (target.isEmpty()) {
            continue;
        }
        QString mediaPath = target;
        if (!mediaPath.startsWith(wordPrefix)) {
            mediaPath = wordPrefix + mediaPath;
        }
        QByteArray bytes;
        if (!readFileFromZip(zip, mediaPath, bytes) || bytes.isEmpty()) {
            continue;
        }
        const QString suffix = QFileInfo(mediaPath).suffix();
        const QByteArray b64 = bytes.toBase64();
        imageData.insert(rId, b64);

        QVariantMap img;
        img.insert(QStringLiteral("id"), rId);
        img.insert(QStringLiteral("format"), suffix);
        img.insert(QStringLiteral("mode"), QStringLiteral("inline"));
        img.insert(QStringLiteral("dataBase64"), QString::fromLatin1(b64));
        imageList.append(img);
    }

    zip.close();

    // ---- 行生成：plain / rich / image ----
    QStringList texts;
    QVector<QPair<int, QString>> richLines;       // 行号 → rich HTML
    QVector<QPair<int, QStringList>> imageLines;  // 行号 → imageIds

    for (int pi = 0; pi < paragraphs.size(); ++pi) {
        const QVector<RunPiece> &par = paragraphs.at(pi);

        QString text;
        QStringList ids;
        bool hasFormat = false;
        for (const RunPiece &pc : par) {
            text += pc.text;
            if (pc.hasImage && !ids.contains(pc.imageId)) {
                ids.append(pc.imageId);
            }
            hasFormat = hasFormat || pc.bold || pc.italic
                        || !pc.color.isEmpty() || pc.szHalf > 0 || !pc.font.isEmpty();
        }
        texts.append(text);

        const bool hasImage = !ids.isEmpty();
        if (hasImage && text.trimmed().isEmpty()) {
            // 纯图段落 → image 行
            imageLines.append(qMakePair(pi, ids));
        } else if (hasImage || hasFormat) {
            // 有文本（含图/格式）→ rich 行：HTML 文本 span + 内嵌 data URI 图片
            QString html;
            for (const RunPiece &pc : par) {
                if (pc.hasImage && imageData.contains(pc.imageId)) {
                    // 图文混排：QML Text 富文本不支持 <img>（data URI 不加载），
                    // 嵌图会导致布局错乱/重叠 → 用文本占位符（图片本身不显示）
                    html += QStringLiteral("[图片]");
                }
                if (!pc.text.isEmpty()) {
                    QString inner = escapeHtml(pc.text);
                    inner.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
                    if (pc.bold) {
                        inner = QStringLiteral("<b>") + inner + QStringLiteral("</b>");
                    }
                    if (pc.italic) {
                        inner = QStringLiteral("<i>") + inner + QStringLiteral("</i>");
                    }
                    QString style;
                    if (!pc.color.isEmpty()) {
                        style += QStringLiteral("color:%1;").arg(pc.color);
                    }
                    if (pc.szHalf > 0) {
                        style += QStringLiteral("font-size:%1px;").arg(pc.szHalf / 2);
                    }
                    if (!pc.font.isEmpty()) {
                        style += QStringLiteral("font-family:'%1';").arg(pc.font);
                    }
                    if (!style.isEmpty()) {
                        html += QStringLiteral("<span style=\"%1\">%2</span>").arg(style, inner);
                    } else {
                        html += inner;
                    }
                }
            }
            if (!html.isEmpty()) {
                richLines.append(qMakePair(pi, html));
            }
        }
    }

    // ---- meta（先于模型填充：setLines 触发 QML delegate 绑定求值时
    //       imageSource 需要 meta.images 已就绪）----
    meta.insert(QStringLiteral("sourceFile"), QFileInfo(path).fileName());
    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("docx"));
    meta.insert(QStringLiteral("importedAt"),
                QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!imageList.isEmpty()) {
        meta.insert(QStringLiteral("images"), imageList);
    }

    // ---- 填充模型 ----
    model->setLines(texts);
    const int n = qMin<int>(paragraphs.size(), model->lineCount());
    for (int i = 0; i < n; ++i) {
        if (!richLines.isEmpty() && richLines.first().first == i) {
            model->setLineRich(i, richLines.first().second);
            model->setLineDisplay(i, QStringLiteral("rich"));
            richLines.removeFirst();
        } else if (!imageLines.isEmpty() && imageLines.first().first == i) {
            model->setLineImages(i, imageLines.first().second);
            model->setLineDisplay(i, QStringLiteral("image"));
            imageLines.removeFirst();
        }
    }

    if (error) {
        error->clear();
    }
    return true;
}

// ==================== 导出（对称补全，设计见 docs/services/docx-comment-export.md） ====================

namespace {

// 写一个 zip 条目（QuaZipFile 写入，失败返回 false；mdCreate 模式用 QuaZipNewInfo 指定条目名）
bool writeZipEntry(QuaZip &zip, const QString &name, const QByteArray &data)
{
    QuaZipFile file(&zip);
    if (!file.open(QIODevice::WriteOnly, QuaZipNewInfo(name))) {
        return false;
    }
    file.write(data);
    file.close();
    return true;
}

// 最小 docx 公共壳（_rels/.rels + [Content_Types].xml）
QByteArray buildRootRels()
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"), true);
    w.writeStartElement(QStringLiteral("Relationships"));
    w.writeAttribute(QStringLiteral("xmlns"), kPkgRelNs);
    w.writeStartElement(QStringLiteral("Relationship"));
    w.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1"));
    w.writeAttribute(QStringLiteral("Type"),
                      QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"));
    w.writeAttribute(QStringLiteral("Target"), QStringLiteral("word/document.xml"));
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray buildContentTypes(bool withComments)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"), true);
    w.writeStartElement(QStringLiteral("Types"));
    w.writeAttribute(QStringLiteral("xmlns"),
                      QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types"));
    w.writeStartElement(QStringLiteral("Default"));
    w.writeAttribute(QStringLiteral("Extension"), QStringLiteral("rels"));
    w.writeAttribute(QStringLiteral("ContentType"),
                      QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("Default"));
    w.writeAttribute(QStringLiteral("Extension"), QStringLiteral("xml"));
    w.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/xml"));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("Override"));
    w.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/word/document.xml"));
    w.writeAttribute(QStringLiteral("ContentType"),
                      QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"));
    w.writeEndElement();
    if (withComments) {
        w.writeStartElement(QStringLiteral("Override"));
        w.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/word/comments.xml"));
        w.writeAttribute(QStringLiteral("ContentType"),
                          QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml"));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray buildDocumentRels(bool withComments)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"), true);
    w.writeStartElement(QStringLiteral("Relationships"));
    w.writeAttribute(QStringLiteral("xmlns"), kPkgRelNs);
    if (withComments) {
        w.writeStartElement(QStringLiteral("Relationship"));
        w.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1"));
        w.writeAttribute(QStringLiteral("Type"),
                          QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments"));
        w.writeAttribute(QStringLiteral("Target"), QStringLiteral("comments.xml"));
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

// 写一个 w:t run（xml:space=preserve 保空白）
void writeTextRun(QXmlStreamWriter &w, const QString &text)
{
    w.writeStartElement(QStringLiteral("w:r"));
    w.writeStartElement(QStringLiteral("w:t"));
    w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
    w.writeCharacters(text);
    w.writeEndElement();
    w.writeEndElement();
}

// 行文本：rich/image 行降级为编辑层文本；image 行补 "[图片]" 占位
QString exportLineText(DocumentModel *model, int i)
{
    QString text = model->lineText(i);
    if (model->displayAt(i) == QStringLiteral("image") && text.trimmed().isEmpty()) {
        text = QStringLiteral("[图片]");
    }
    return text;
}

} // namespace

bool DocxParser::write(const QString &path, DocumentModel *model,
                       CommentService *comments, const QString &style,
                       QVariantMap *meta, QString *error)
{
    if (!model) {
        if (error) {
            *error = QStringLiteral("未关联文档模型");
        }
        return false;
    }
    const bool native = (style == QStringLiteral("native"));

    // ---- 收集批注（行号 → 译文）----
    QHash<int, QString> commentTexts;
    if (comments) {
        const QVariantMap all = comments->allComments();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            bool ok = false;
            const int ln = it.key().toInt(&ok);
            if (ok && ln >= 0 && ln < model->lineCount()) {
                commentTexts.insert(ln, it.value().toString());
            }
        }
    }

    // ---- document.xml ----
    QByteArray docXml;
    {
        QXmlStreamWriter w(&docXml);
        w.setAutoFormatting(true);
        w.writeStartDocument(QStringLiteral("1.0"), true);
        w.writeStartElement(QStringLiteral("w:document"));
        w.writeAttribute(QStringLiteral("xmlns:w"), kWordNs);
        w.writeStartElement(QStringLiteral("w:body"));

        int commentId = 0;
        for (int i = 0; i < model->lineCount(); ++i) {
            const QString text = exportLineText(model, i);
            const QString comment = commentTexts.value(i);
            const bool hasComment = !comment.isEmpty();

            w.writeStartElement(QStringLiteral("w:p"));
            if (native && hasComment) {
                w.writeStartElement(QStringLiteral("w:commentRangeStart"));
                w.writeAttribute(QStringLiteral("w:id"), QString::number(commentId));
                w.writeEndElement();
            }
            if (!text.isEmpty()) {
                writeTextRun(w, text);
            }
            if (native && hasComment) {
                w.writeStartElement(QStringLiteral("w:commentRangeEnd"));
                w.writeAttribute(QStringLiteral("w:id"), QString::number(commentId));
                w.writeEndElement();
                w.writeStartElement(QStringLiteral("w:r"));
                w.writeStartElement(QStringLiteral("w:rPr"));
                w.writeStartElement(QStringLiteral("w:rStyle"));
                w.writeAttribute(QStringLiteral("w:val"), QStringLiteral("CommentReference"));
                w.writeEndElement();
                w.writeEndElement();
                w.writeStartElement(QStringLiteral("w:commentReference"));
                w.writeAttribute(QStringLiteral("w:id"), QString::number(commentId));
                w.writeEndElement();
                w.writeEndElement();
            } else if (!native && hasComment) {
                // inline：换行 + 黄色高亮译文
                w.writeStartElement(QStringLiteral("w:r"));
                w.writeStartElement(QStringLiteral("w:br"));
                w.writeEndElement();
                w.writeEndElement();
                w.writeStartElement(QStringLiteral("w:r"));
                w.writeStartElement(QStringLiteral("w:rPr"));
                w.writeStartElement(QStringLiteral("w:highlight"));
                w.writeAttribute(QStringLiteral("w:val"), QStringLiteral("yellow"));
                w.writeEndElement();
                w.writeEndElement();
                w.writeStartElement(QStringLiteral("w:t"));
                w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                w.writeCharacters(comment);
                w.writeEndElement();
                w.writeEndElement();
            }
            w.writeEndElement(); // w:p

            if (hasComment) {
                ++commentId;
            }
        }

        w.writeStartElement(QStringLiteral("w:sectPr"));
        w.writeEndElement();
        w.writeEndElement(); // w:body
        w.writeEndElement(); // w:document
        w.writeEndDocument();
    }

    // ---- comments.xml（native）----
    QByteArray commentsXml;
    if (native && !commentTexts.isEmpty()) {
        QXmlStreamWriter w(&commentsXml);
        w.setAutoFormatting(true);
        w.writeStartDocument(QStringLiteral("1.0"), true);
        w.writeStartElement(QStringLiteral("w:comments"));
        w.writeAttribute(QStringLiteral("xmlns:w"), kWordNs);
        int commentId = 0;
        for (int i = 0; i < model->lineCount(); ++i) {
            const QString comment = commentTexts.value(i);
            if (comment.isEmpty()) {
                continue;
            }
            w.writeStartElement(QStringLiteral("w:comment"));
            w.writeAttribute(QStringLiteral("w:id"), QString::number(commentId));
            w.writeAttribute(QStringLiteral("w:author"), QStringLiteral("Translex"));
            w.writeAttribute(QStringLiteral("w:initials"), QStringLiteral("TL"));
            w.writeAttribute(QStringLiteral("w:date"),
                             QDateTime::currentDateTime().toString(Qt::ISODate));
            w.writeStartElement(QStringLiteral("w:p"));
            writeTextRun(w, comment);
            w.writeEndElement();
            w.writeEndElement();
            ++commentId;
        }
        w.writeEndElement();
        w.writeEndDocument();
    }

    // ---- 打包 zip ----
    QuaZip zip(path);
    if (!zip.open(QuaZip::mdCreate)) {
        if (error) {
            *error = QStringLiteral("无法创建 docx（zip 打开失败）：%1").arg(path);
        }
        return false;
    }
    bool ok = writeZipEntry(zip, QStringLiteral("[Content_Types].xml"), buildContentTypes(native))
              && writeZipEntry(zip, QStringLiteral("_rels/.rels"), buildRootRels())
              && writeZipEntry(zip, QStringLiteral("word/document.xml"), docXml);
    if (ok && native) {
        ok = writeZipEntry(zip, QStringLiteral("word/comments.xml"), commentsXml)
             && writeZipEntry(zip, QStringLiteral("word/_rels/document.xml.rels"),
                              buildDocumentRels(true));
    }
    zip.close();
    if (!ok) {
        if (error) {
            *error = QStringLiteral("写入 docx 失败：%1").arg(path);
        }
        return false;
    }

    if (meta) {
        meta->insert(QStringLiteral("sourceFormat"), QStringLiteral("docx"));
        meta->insert(QStringLiteral("commentStyle"), native ? QStringLiteral("native")
                                                            : QStringLiteral("inline"));
    }
    if (error) {
        error->clear();
    }
    return true;
}
