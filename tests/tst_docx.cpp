#include <QtTest/QtTest>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <quazip/quazip.h>
#include <quazip/quazipfile.h>

#include "documentmodel.h"
#include "docxparser.h"

namespace {

// 用 QuaZip 构造最小 docx（document.xml + rels + 可选 png）
bool writeDocx(const QString &path, const QByteArray &documentXml,
               const QByteArray &relsXml, const QByteArray &png)
{
    QuaZip zip(path);
    if (!zip.open(QuaZip::mdCreate)) {
        return false;
    }
    {
        QuaZipFile f(&zip);
        if (!f.open(QIODevice::WriteOnly, QuaZipNewInfo(QStringLiteral("word/document.xml")))) {
            return false;
        }
        f.write(documentXml);
        f.close();
    }
    {
        QuaZipFile f(&zip);
        if (!f.open(QIODevice::WriteOnly,
                    QuaZipNewInfo(QStringLiteral("word/_rels/document.xml.rels")))) {
            return false;
        }
        f.write(relsXml);
        f.close();
    }
    if (!png.isEmpty()) {
        QuaZipFile f(&zip);
        if (!f.open(QIODevice::WriteOnly,
                    QuaZipNewInfo(QStringLiteral("word/media/image1.png")))) {
            return false;
        }
        f.write(png);
        f.close();
    }
    zip.close();
    return true;
}

QByteArray pngImage()
{
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    buf.close();
    return bytes;
}

const QByteArray kRelsXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId5\" Type=\".../image\" Target=\"media/image1.png\"/>"
    "</Relationships>";

} // namespace

class TstDocx : public QObject
{
    Q_OBJECT

private slots:
    // 纯文本段落 → 普通行
    void plainParagraphs()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("plain.docx"));
        const QByteArray xml =
            "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:body>"
            "<w:p><w:r><w:t>第一段</w:t></w:r></w:p>"
            "<w:p><w:r><w:t>第二段</w:t></w:r><w:r><w:t>续</w:t></w:r></w:p>"
            "<w:p/>"
            "</w:body></w:document>";
        QVERIFY(writeDocx(path, xml, kRelsXml, QByteArray()));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(DocxParser::read(path, &model, nullptr, meta, &error), qPrintable(error));
        QCOMPARE(model.lineCount(), 3);
        QCOMPARE(model.lineText(0), QStringLiteral("第一段"));
        QCOMPARE(model.lineText(1), QStringLiteral("第二段续")); // 同段多 run 合并
        QCOMPARE(model.lineText(2), QString());                  // 空段落 → 空行
        QCOMPARE(model.displayAt(0), QStringLiteral("plain"));
        QCOMPARE(meta.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("docx"));
    }

    // 基础格式（粗体/斜体/字号）→ rich HTML 显示层
    void richFormat()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("rich.docx"));
        const QByteArray xml =
            "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:body>"
            "<w:p><w:r><w:rPr><w:b/><w:i/><w:color w:val=\"FF0000\"/><w:sz w:val=\"28\"/></w:rPr>"
            "<w:t>红色粗斜</w:t></w:r></w:p>"
            "<w:p><w:r><w:t>普通</w:t></w:r></w:p>"
            "</w:body></w:document>";
        QVERIFY(writeDocx(path, xml, kRelsXml, QByteArray()));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(DocxParser::read(path, &model, nullptr, meta, &error), qPrintable(error));
        QCOMPARE(model.lineCount(), 2);
        QCOMPARE(model.displayAt(0), QStringLiteral("rich"));
        const QString rich = model.richAt(0);
        QVERIFY(rich.contains(QStringLiteral("<b>")));
        QVERIFY(rich.contains(QStringLiteral("<i>")));
        QVERIFY(rich.contains(QStringLiteral("FF0000")));
        QVERIFY(rich.contains(QStringLiteral("14px"))); // 28 半磅 → 14px
        QCOMPARE(model.displayAt(1), QStringLiteral("plain"));
    }

    // 纯图段落 → image 行 + meta.images 内嵌 base64
    void imageLine()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("img.docx"));
        const QByteArray xml =
            "<?xml version=\"1.0\"?><w:document "
            "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
            "<w:body>"
            "<w:p><w:r><w:drawing><wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
            "<a:blip r:embed=\"rId5\"/></wp:inline></w:drawing></w:r></w:p>"
            "<w:p><w:r><w:t>图后文本</w:t></w:r></w:p>"
            "</w:body></w:document>";
        QVERIFY(writeDocx(path, xml, kRelsXml, pngImage()));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(DocxParser::read(path, &model, nullptr, meta, &error), qPrintable(error));
        QCOMPARE(model.lineCount(), 2);
        QCOMPARE(model.displayAt(0), QStringLiteral("image"));
        QCOMPARE(model.imageIdsAt(0), QStringList(QStringLiteral("rId5")));
        QCOMPARE(model.lineText(1), QStringLiteral("图后文本"));

        const QVariantList images = meta.value(QStringLiteral("images")).toList();
        QCOMPARE(images.size(), 1);
        const QVariantMap img = images.first().toMap();
        QCOMPARE(img.value(QStringLiteral("id")).toString(), QStringLiteral("rId5"));
        QCOMPARE(img.value(QStringLiteral("format")).toString(), QStringLiteral("png"));
        QCOMPARE(img.value(QStringLiteral("mode")).toString(), QStringLiteral("inline"));
        QVERIFY(!img.value(QStringLiteral("dataBase64")).toString().isEmpty());
    }

    // 文本+图片段落 → rich 行（data URI 内嵌）
    void textWithImage()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("mix.docx"));
        const QByteArray xml =
            "<?xml version=\"1.0\"?><w:document "
            "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
            "<w:body>"
            "<w:p><w:r><w:t>开头</w:t></w:r>"
            "<w:r><w:drawing><wp:inline xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
            "<a:blip r:embed=\"rId5\"/></wp:inline></w:drawing></w:r>"
            "<w:r><w:t>结尾</w:t></w:r></w:p>"
            "</w:body></w:document>";
        QVERIFY(writeDocx(path, xml, kRelsXml, pngImage()));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(DocxParser::read(path, &model, nullptr, meta, &error), qPrintable(error));
        QCOMPARE(model.lineCount(), 1);
        QCOMPARE(model.lineText(0), QStringLiteral("开头结尾"));
        QCOMPARE(model.displayAt(0), QStringLiteral("rich"));
        QVERIFY(model.richAt(0).contains(QStringLiteral("data:image/png;base64,")));
        QVERIFY(model.richAt(0).contains(QStringLiteral("开头")));
    }

    // 非法文件（非 zip / 缺 document.xml）→ 返回 false + error
    void invalidFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.docx"));
        {
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write("not a zip");
        }
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY(!DocxParser::read(path, &model, nullptr, meta, &error));
        QVERIFY(!error.isEmpty());
    }

    // 仓库自带示例 samples/demo.docx（生成脚本 samples/gen_docx.py）：
    // 覆盖章节/富文本/图片/混排/空段，作为长期回归
    void sampleFile()
    {
        const QString path = QStringLiteral("../../samples/demo.docx");
        if (!QFile::exists(path)) {
            QSKIP("samples/demo.docx 不存在（可运行 samples/gen_docx.py 生成）");
        }
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(DocxParser::read(path, &model, nullptr, meta, &error), qPrintable(error));
        QVERIFY(model.lineCount() > 30);

        bool hasRich = false;
        bool hasImage = false;
        bool hasChapter = false;
        for (int i = 0; i < model.lineCount(); ++i) {
            if (model.displayAt(i) == QStringLiteral("rich")) {
                hasRich = true;
            }
            if (model.displayAt(i) == QStringLiteral("image")) {
                hasImage = true;
            }
            if (model.lineText(i).contains(QStringLiteral("第一章"))) {
                hasChapter = true;
            }
        }
        QVERIFY(hasRich);   // 富文本行（含颜色/字号）
        QVERIFY(hasImage);  // 纯图段落 → image 行
        QVERIFY(hasChapter); // 章节标题保留文本
        QCOMPARE(meta.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("docx"));
    }
};

QTEST_GUILESS_MAIN(TstDocx)
#include "tst_docx.moc"
