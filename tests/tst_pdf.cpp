#include <QtTest/QtTest>

#include <QFile>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QTemporaryDir>

#include "documentmodel.h"
#include "commentservice.h"
#include "pdfparser.h"

namespace {

// 手写最小 PDF（Type1 Helvetica + WinAnsi 文本），保证文本层可提取——
// 不能用 QPdfWriter 生成夹具：Qt 6.5.3 的 QPdfWriter 文本层提取损坏
// （字符重复/乱码，见 docs/services/pdf-service.md §7），但其渲染正确。
// 页 1：两行文本（提取含 \n，测页内折行合并）；页 2：单行；页 3：空页。
QByteArray simplePdf()
{
    const QByteArray s1 = "BT /F1 12 Tf 72 700 Td (Page one line one) Tj "
                          "0 -22 Td (Page one line two) Tj ET\n";
    const QByteArray s2 = "BT /F1 12 Tf 72 700 Td (Second page content) Tj ET\n";
    const QByteArray s3 = "";
    QByteArray pdf;
    pdf += "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >> endobj\n";
    pdf += "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
           "/Contents 6 0 R /Resources << /Font << /F1 7 0 R >> >> >> endobj\n";
    pdf += "4 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
           "/Contents 8 0 R /Resources << /Font << /F1 7 0 R >> >> >> endobj\n";
    pdf += "5 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
           "/Contents 9 0 R /Resources << /Font << /F1 7 0 R >> >> >> endobj\n";
    pdf += "6 0 obj << /Length " + QByteArray::number(s1.size()) + " >> stream\n" + s1 + "endstream endobj\n";
    pdf += "7 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> endobj\n";
    pdf += "8 0 obj << /Length " + QByteArray::number(s2.size()) + " >> stream\n" + s2 + "endstream endobj\n";
    pdf += "9 0 obj << /Length " + QByteArray::number(s3.size()) + " >> stream\n" + s3 + "endstream endobj\n";
    pdf += "trailer << /Root 1 0 R /Size 10 >>\n%%EOF\n";
    return pdf;
}

bool writeSimplePdf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(simplePdf());
    f.close();
    return true;
}

// 渲染页 → 统计非白像素（验证导出 PDF 确实绘制了字形，而非空白页）
int darkPixelCount(const QString &pdfPath, int page)
{
    QPdfDocument doc;
    doc.load(pdfPath);
    const QImage img = doc.render(page, QSize(600, 848));
    int dark = 0;
    for (int y = 0; y < img.height(); y += 2) {
        for (int x = 0; x < img.width(); x += 2) {
            if (qGray(img.pixel(x, y)) < 128) {
                ++dark;
            }
        }
    }
    return dark;
}

} // namespace

class TestPdfParser : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_tempDir.isValid());
        m_samplePath = m_tempDir.path() + QStringLiteral("/sample.pdf");
        QVERIFY(writeSimplePdf(m_samplePath));
    }

    // 导入：页数 = 行数，空页保空行，meta.sourceFormat=pdf
    void readBasic()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(m_samplePath, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineCount(), 3);
        QCOMPARE(meta.value(QStringLiteral("sourceFormat")).toString(),
                 QStringLiteral("pdf"));
        QVERIFY(!meta.value(QStringLiteral("sourceFile")).toString().isEmpty());
    }

    // 导入：页内多行 → 单行且无 \n（模型行是单行文本）
    void readNormalizesPageText()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY(PdfParser::read(m_samplePath, &model, nullptr, meta, &error));
        QCOMPARE(model.lineCount(), 3);

        const QString page1 = model.lineText(0);
        QVERIFY(!page1.contains(QLatin1Char('\n')));
        QVERIFY(!page1.contains(QStringLiteral("  ")));
        QVERIFY(page1.contains(QStringLiteral("Page one line one")));
        QVERIFY(page1.contains(QStringLiteral("Page one line two")));

        QCOMPARE(model.lineText(1), QStringLiteral("Second page content"));

        // 空页 → 空行（页映射保留）
        QCOMPARE(model.lineText(2), QString());
    }

    // 导出：生成有效 PDF（可加载、页数正确、渲染出字形）。
    // 文本层不可断言：Qt 6.5.3 QPdfWriter 的 ToUnicode/子集化有缺陷，
    // 提取会乱码（含 ASCII），但渲染视觉正确——见 pdf-service.md §7。
    void exportProducesValidPdf()
    {
        DocumentModel model;
        model.setLines({QStringLiteral("第一行中文"), QStringLiteral("Hello Translex")});
        CommentService comments;
        comments.setComment(1, QStringLiteral("TL note"));

        QVariantMap meta;
        QString error;
        const QString outPath = m_tempDir.path() + QStringLiteral("/out.pdf");
        QVERIFY2(PdfParser::write(outPath, &model, &comments, meta, &error),
                 qPrintable(error));
        QVERIFY(QFile::exists(outPath));

        QPdfDocument doc;
        QCOMPARE(doc.load(outPath), QPdfDocument::Error::None);
        QCOMPARE(doc.pageCount(), 1);
        // 渲染冒烟：中英文都被绘制（暗像素充足）
        QVERIFY(darkPixelCount(outPath, 0) > 500);
    }

    // 错误：文件不存在 / 非 PDF 内容
    void readErrors()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;

        QVERIFY(!PdfParser::read(m_tempDir.path() + QStringLiteral("/nope.pdf"),
                                 &model, nullptr, meta, &error));
        QVERIFY(!error.isEmpty());

        error.clear();
        const QString junk = m_tempDir.path() + QStringLiteral("/junk.pdf");
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a pdf");
        f.close();
        QVERIFY(!PdfParser::read(junk, &model, nullptr, meta, &error));
        QVERIFY(!error.isEmpty());
    }

    // 空文档导出（0 行）不崩溃且产出可加载的合法 PDF
    void writeEmpty()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;
        const QString outPath = m_tempDir.path() + QStringLiteral("/empty.pdf");
        QVERIFY(PdfParser::write(outPath, &model, nullptr, meta, &error));
        QVERIFY(QFile::exists(outPath));
        QPdfDocument doc;
        QCOMPARE(doc.load(outPath), QPdfDocument::Error::None);
    }

    // 仓库样本回归：samples/demo.pdf（缺失时自动生成，便于入库）
    void sampleFile()
    {
        const QString samplePath = QStringLiteral(SOURCE_DIR) + QStringLiteral("/samples/demo.pdf");
        if (!QFile::exists(samplePath)) {
            QVERIFY2(writeSimplePdf(samplePath), "无法生成 samples/demo.pdf");
        }
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(samplePath, &model, nullptr, meta, &error),
                 qPrintable(error));
        QVERIFY(model.lineCount() >= 3);
        QVERIFY(model.lineText(0).contains(QStringLiteral("Page one")));
        QCOMPARE(meta.value(QStringLiteral("sourceFormat")).toString(),
                 QStringLiteral("pdf"));
    }

private:
    QTemporaryDir m_tempDir;
    QString m_samplePath;
};

QTEST_MAIN(TestPdfParser)
#include "tst_pdf.moc"
