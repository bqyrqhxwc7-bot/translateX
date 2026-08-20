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

// 空 PDF（0 页）：Catalog + 空 Pages
QByteArray emptyPdf()
{
    QByteArray pdf;
    pdf += "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [] /Count 0 >> endobj\n";
    pdf += "trailer << /Root 1 0 R /Size 3 >>\n%%EOF\n";
    return pdf;
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(data);
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

// ---- v3 夹具：紧凑格式 PDF（PDFium 风格）+ 生成器 ----

struct PObj {
    int num;
    QByteArray body;   // "N 0 obj ... endobj"（紧凑：无空格）
};

// 手写对象列表 → 合法 PDF（xref 偏移自动计算）
QByteArray buildPdf(const QVector<PObj> &objs)
{
    QByteArray pdf = "%PDF-1.4\n";
    QVector<int> offs;
    int maxNum = 0;
    for (const PObj &o : objs) {
        offs.append(pdf.size());
        pdf += o.body + "\n";
        maxNum = qMax(maxNum, o.num);
    }
    const int xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(maxNum + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    QVector<int> byNum(maxNum + 1, -1);
    for (int i = 0; i < objs.size(); ++i) {
        byNum[objs[i].num] = offs.at(i);
    }
    for (int n = 1; n <= maxNum; ++n) {
        pdf += QByteArray::number(byNum.at(n)).rightJustified(10, '0') + " 00000 n \n";
    }
    pdf += "trailer << /Root 1 0 R /Size " + QByteArray::number(maxNum + 1) + " >>\n";
    pdf += "startxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

// 紧凑格式富文本夹具（覆盖 v3 全部解析路径）：
//  - 对象头与 dict 同行无空格（/Type/Page、/Subtype/Type1）
//  - /Contents N 0 R 指向数组对象 [ M 0 R ]（间接数组内容流）
//  - Type1 字体 + 1 字节码空间 ToUnicode CMap（bfrange 连续 ASCII）
//  - TJ 字距：kern 微调（26）不产生空格，|n|>100（-302）产生空格
//  - 无 /Annots 挂接的孤岛链接注释（/Subtype/Link + /Rect + /A/URI）
// 期望：行0 "Contracts for C++"（黑）；行1 "<jb@ex.com>"（蓝 + <a href）
QByteArray compactRichPdf()
{
    const QByteArray content =
        "q 0 g 0 G BT /F1 12 Tf 72 700 Td "
        "[(Con)26(tracts)-302(for)-302(C++)]TJ "
        "0 -22 Td 0 0 1 rg [(<jb@ex.com>)]TJ ET Q\n";
    const QByteArray cmap =
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /M def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
        "1 beginbfrange\n<20> <7E> <0020>\nendbfrange\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\nend\nend\n";
    return buildPdf({
        { 1, "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj" },
        { 2, "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj" },
        { 3, "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]"
              "/Contents 4 0 R/Resources<</Font<</F1 6 0 R>>>>>>endobj" },
        { 4, "4 0 obj[ 5 0 R ]endobj" },
        { 5, "5 0 obj<</Length " + QByteArray::number(content.size()) + ">>stream\n"
              + content + "endstream endobj" },
        { 6, "6 0 obj<</BaseFont/MyFont/Subtype/Type1/ToUnicode 7 0 R/Type/Font>>endobj" },
        { 7, "7 0 obj<</Length " + QByteArray::number(cmap.size()) + ">>stream\n"
              + cmap + "endstream endobj" },
        { 8, "8 0 obj<</A<</S/URI/Type/Action/URI(mailto:jb@ex.com)>>"
              "/Border[0 0 0]/Rect[100 670 210 690]/Subtype/Link/Type/Annot>>endobj" },
    });
}

// QPdfWriter 图片夹具：Type0/Identity-H + bfrange 数组 ToUnicode CMap
// + DCTDecode(JPEG) 图片 + 图文混排 → 期望：[图片] 占位 + meta.images
bool writeQpaintImagePdf(const QString &path)
{
    QPdfWriter w(path);
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setResolution(96);
    QPainter p(&w);
    p.setFont(QFont(QStringLiteral("Arial"), 14));
    p.drawText(50, 50, QStringLiteral("Before image"));
    QImage img(120, 90, QImage::Format_RGB32);
    img.fill(QColor(60, 120, 200));
    p.drawImage(QRect(100, 120, 120, 90), img);
    p.drawText(100, 260, QStringLiteral("After image"));
    p.end();
    return QFile::exists(path);
}

// FlateDecode 原始像素夹具（无文本 → 纯图行）：
// 期望：1 行 display=image + meta.images(1, png)
QByteArray flateImagePdf()
{
    QImage img(4, 3, QImage::Format_RGB32);
    const QRgb cols[12] = { qRgb(255, 0, 0),   qRgb(0, 255, 0),   qRgb(0, 0, 255),
                            qRgb(255, 255, 255), qRgb(200, 200, 200), qRgb(100, 100, 100),
                            qRgb(50, 50, 50),  qRgb(0, 0, 0),     qRgb(255, 0, 255),
                            qRgb(0, 255, 255), qRgb(255, 255, 0), qRgb(128, 0, 128) };
    int k = 0;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            img.setPixel(x, y, cols[k++]);
        }
    }
    // RGB 行序裸像素（PDF 图片要求逐行，无行填充）
    QByteArray raw;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            const QRgb c = img.pixel(x, y);
            raw += char(qRed(c));
            raw += char(qGreen(c));
            raw += char(qBlue(c));
        }
    }
    Q_UNUSED(raw)
    // 纯 zlib 压缩数据（python zlib.compress(raw, 9) 输出，34 字节）。
    // 注意：不能用 qCompress——其输出带 4 字节未压缩长度前缀，非纯 zlib，
    // 与 PDF FlateDecode（纯 zlib 流）不兼容。
    const QByteArray comp = QByteArray::fromHex(
        "78dafbcfc0c0f01f8481e0c4891329292946464660b1ff6031860686060036a3110f");
    const QByteArray content = "q 100 0 0 100 0 0 cm /Im0 Do Q\n";
    return buildPdf({
        { 1, "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj" },
        { 2, "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj" },
        { 3, "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 150]"
              "/Contents 5 0 R/Resources<</XObject<</Im0 4 0 R>>>>>>endobj" },
        { 4, "4 0 obj<</Type/XObject/Subtype/Image/Width 4/Height 3"
              "/ColorSpace/DeviceRGB/BitsPerComponent 8/Filter/FlateDecode"
              "/Length " + QByteArray::number(comp.size()) + ">>stream\n"
              + comp + "\nendstream endobj" },
        { 5, "5 0 obj<</Length " + QByteArray::number(content.size()) + ">>stream\n"
              + content + "endstream endobj" },
    });
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

    // 导入：页内视觉行逐行拆分（2026-08-19 改进），空页保空行，meta.sourceFormat=pdf
    // 夹具 3 页：页1 两行文本 + 页2 单行 + 页3 空页 → 共 4 行
    void readBasic()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(m_samplePath, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineCount(), 4);
        QCOMPARE(meta.value(QStringLiteral("sourceFormat")).toString(),
                 QStringLiteral("pdf"));
        QVERIFY(!meta.value(QStringLiteral("sourceFile")).toString().isEmpty());
    }

    // 导入：每页按视觉行拆分为多个模型行，行内无 \n/连续空格
    void readNormalizesPageText()
    {
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY(PdfParser::read(m_samplePath, &model, nullptr, meta, &error));
        QCOMPARE(model.lineCount(), 4);

        QCOMPARE(model.lineText(0), QStringLiteral("Page one line one"));
        QCOMPARE(model.lineText(1), QStringLiteral("Page one line two"));
        QCOMPARE(model.lineText(2), QStringLiteral("Second page content"));
        for (int i = 0; i < model.lineCount(); ++i) {
            QVERIFY(!model.lineText(i).contains(QLatin1Char('\n')));
            QVERIFY(!model.lineText(i).contains(QStringLiteral("  ")));
        }

        // 空页 → 空行（页映射保留）
        QCOMPARE(model.lineText(3), QString());
    }

    // 导出：生成有效 PDF（可加载、每行一页、渲染出字形）。
    // 文本层内容断言见 writeFullRoundTrip（CMap 修复后完整往返）。
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
        // 连续排版：多行一页、自动分页（2 行内容不足一页 → 1 页；行结构保真
        // 由 writeFullRoundTrip 的再导入断言覆盖）
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

    // 完整往返（v2）：导出（每行一页 + CMap 修复）→ 再导入：
    // 行结构 + 中英文内容 + 批注尾注全部保真
    void writeFullRoundTrip()
    {
        DocumentModel model;
        model.setLines({QStringLiteral("第一行中文内容"), QStringLiteral("Second line English"),
                        QStringLiteral("第三行"), QString()});
        CommentService comments;
        comments.setComment(1, QStringLiteral("译文批注TL"));

        QVariantMap meta;
        QString error;
        const QString outPath = m_tempDir.path() + QStringLiteral("/full.pdf");
        QVERIFY2(PdfParser::write(outPath, &model, &comments, meta, &error),
                 qPrintable(error));

        DocumentModel back;
        QVariantMap meta2;
        QVERIFY2(PdfParser::read(outPath, &back, nullptr, meta2, &error),
                 qPrintable(error));
        QCOMPARE(back.lineCount(), 4);
        QCOMPARE(back.lineText(0), QStringLiteral("第一行中文内容"));
        QCOMPARE(back.lineText(1), QStringLiteral("Second line English（批注：译文批注TL）"));
        QCOMPARE(back.lineText(2), QStringLiteral("第三行"));
        QCOMPARE(back.lineText(3), QString());
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

    // 空 PDF（0 页）：不崩溃。pdfium 对 0 页文档行为不一（可能拒绝加载），
    // 断言「成功则 0 行，失败则有错误」两种可接受路径。
    void readEmptyPdf()
    {
        const QString path = m_tempDir.path() + QStringLiteral("/empty_pages.pdf");
        QVERIFY(writeBytes(path, emptyPdf()));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        const bool ok = PdfParser::read(path, &model, nullptr, meta, &error);
        if (ok) {
            QCOMPARE(model.lineCount(), 0);
        } else {
            QVERIFY(!error.isEmpty());
        }
    }

    // 密码 PDF：拒绝打开且给出错误（不误读、不崩溃）
    void readEncryptedPdf()
    {
        // 最小加密 PDF（Standard 安全字典）：QPdfDocument 无法用空密码解密
        QByteArray pdf;
        pdf += "%PDF-1.4\n";
        pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n";
        pdf += "2 0 obj << /Type /Pages /Kids [] /Count 0 >> endobj\n";
        pdf += "3 0 obj << /Filter /Standard /V 2 /R 3 /O <00000000000000000000000000000000> "
               "/U <00000000000000000000000000000000> /P -4 /Length 32 >> endobj\n";
        pdf += "trailer << /Root 1 0 R /Size 4 /Encrypt 3 0 R >>\n%%EOF\n";
        const QString path = m_tempDir.path() + QStringLiteral("/locked.pdf");
        QVERIFY(writeBytes(path, pdf));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY(!PdfParser::read(path, &model, nullptr, meta, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(model.lineCount() == 0);
    }

    // 仓库样本回归：samples/demo.pdf（缺失时自动生成，便于入库）。
    // 说明：samples/demo.pdf 已入库，此生成分支仅为首次克隆/样本被删时的兜底；
    // 样本文件本身是测试夹具（手写干净文本层 PDF），写入源码树属预期行为。
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

    // v3：紧凑格式富文本导入（PDFium 风格）——对象无空格/dict 同行/
    // 间接数组 Contents/1 字节 CMap/TJ 字距阈值/孤岛链接注释
    void readCompactRichPdf()
    {
        const QString path = m_tempDir.path() + QStringLiteral("/compact.pdf");
        QVERIFY(writeBytes(path, compactRichPdf()));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(path, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineCount(), 2);

        // TJ：kern 26 不拆词、-302 产生空格
        QCOMPARE(model.lineText(0), QStringLiteral("Contracts for C++"));
        // 孤岛链接注释 → 行内 <a href>（mailto URI）
        QCOMPARE(model.lineText(1), QStringLiteral("<jb@ex.com>"));
        const QString rich1 = model.richAt(1);
        QVERIFY(rich1.contains(QStringLiteral("<a href=\"mailto:jb@ex.com\">")));
        QVERIFY(rich1.contains(QStringLiteral("color:#0000ff")));
        QCOMPARE(model.displayAt(0), QStringLiteral("rich"));
        QCOMPARE(model.displayAt(1), QStringLiteral("rich"));
        // 行 0 无链接
        QVERIFY(!model.richAt(0).contains(QStringLiteral("<a href")));
    }

    // v3：QPdfWriter 图片夹具——Type0/Identity-H + bfrange 数组 CMap 解码 +
    // DCTDecode 图片提取 + 图文混排 [图片] 占位
    void readQpaintImagePdf()
    {
        const QString path = m_tempDir.path() + QStringLiteral("/qpaint_img.pdf");
        QVERIFY(writeQpaintImagePdf(path));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(path, &model, nullptr, meta, &error),
                 qPrintable(error));
        QVERIFY(model.lineCount() >= 2);
        QCOMPARE(model.lineText(0), QStringLiteral("Before image"));
        // 图文混排：[图片] 占位（与 docx 一致；图片归属最近行，任一行均可）
        bool anyPlaceholder = false;
        for (int i = 0; i < model.lineCount(); ++i) {
            if (model.richAt(i).contains(QStringLiteral("[图片]"))) {
                anyPlaceholder = true;
            }
        }
        QVERIFY(anyPlaceholder);
        // 图片元数据：DCTDecode → jpg
        const QVariantList images = meta.value(QStringLiteral("images")).toList();
        QCOMPARE(images.size(), 1);
        QCOMPARE(images.first().toMap().value(QStringLiteral("format")).toString(),
                 QStringLiteral("jpg"));
        QVERIFY(!images.first().toMap().value(QStringLiteral("dataBase64")).toString().isEmpty());
    }

    // v3：FlateDecode 原始像素图片 + 无文本内容流 → 纯图行（display=image, png）
    void readFlateImagePdf()
    {
        const QString path = m_tempDir.path() + QStringLiteral("/flate_img.pdf");
        QVERIFY(writeBytes(path, flateImagePdf()));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(path, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineCount(), 1);
        QCOMPARE(model.displayAt(0), QStringLiteral("image"));
        QVERIFY(model.imageIdsAt(0).contains(QStringLiteral("pdf_img_0")));
        const QVariantList images = meta.value(QStringLiteral("images")).toList();
        QCOMPARE(images.size(), 1);
        QCOMPARE(images.first().toMap().value(QStringLiteral("format")).toString(),
                 QStringLiteral("png"));
    }

    // v3 导出增强：富文本/链接行导出 → 再导入文本保真（pdf-service.md §9.4）
    void exportRichRoundTrip()
    {
        const QString src = m_tempDir.path() + QStringLiteral("/compact_src.pdf");
        QVERIFY(writeBytes(src, compactRichPdf()));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(src, &model, nullptr, meta, &error),
                 qPrintable(error));

        const QString outPath = m_tempDir.path() + QStringLiteral("/rich_out.pdf");
        QVERIFY2(PdfParser::write(outPath, &model, nullptr, meta, &error),
                 qPrintable(error));

        DocumentModel back;
        QVariantMap meta2;
        QVERIFY2(PdfParser::read(outPath, &back, nullptr, meta2, &error),
                 qPrintable(error));
        QCOMPARE(back.lineCount(), 2);
        QCOMPARE(back.lineText(0), QStringLiteral("Contracts for C++"));
        QCOMPARE(back.lineText(1), QStringLiteral("<jb@ex.com>"));
    }

    // v3 导出增强：纯图行导出 → 再导入（尽力而为：图片渲染成功与否均可接受）
    void exportImageRoundTrip()
    {
        const QString src = m_tempDir.path() + QStringLiteral("/flate_src.pdf");
        QVERIFY(writeBytes(src, flateImagePdf()));
        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(src, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.displayAt(0), QStringLiteral("image"));

        const QString outPath = m_tempDir.path() + QStringLiteral("/img_out.pdf");
        QVERIFY2(PdfParser::write(outPath, &model, nullptr, meta, &error),
                 qPrintable(error));

        DocumentModel back;
        QVariantMap meta2;
        QVERIFY2(PdfParser::read(outPath, &back, nullptr, meta2, &error),
                 qPrintable(error));
        QVERIFY(back.lineCount() >= 1);
    }

    // v3：无 ToUnicode 的简单字体按 /Encoding 标准编码表解码
    //（WinAnsi 0x80-0x9F 特殊区：… – — “ ”；回归：不再回退 QChar(code) 产生控制字符/□）
    void readWinAnsiPdf()
    {
        const QByteArray content =
            "BT /F1 12 Tf 72 700 Td (\x85quote\x96 dash\x97 em \x93hello\x94) Tj ET\n";
        const QByteArray pdf = buildPdf({
            { 1, "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj" },
            { 2, "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj" },
            { 3, "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]"
                  "/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>endobj" },
            { 4, "4 0 obj<</Length " + QByteArray::number(content.size()) + ">>stream\n"
                  + content + "endstream endobj" },
            { 5, "5 0 obj<</BaseFont/Helvetica/Subtype/Type1/Encoding/WinAnsiEncoding"
                  "/Type/Font>>endobj" },
        });
        const QString path = m_tempDir.path() + QStringLiteral("/winansi.pdf");
        QVERIFY(writeBytes(path, pdf));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(path, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineText(0), QStringLiteral("…quote– dash— em “hello”"));
    }

    // v3：1 字节 CMap miss（码未映射）→ 跳过该码（不产生控制字符/□）
    void readCmapMissSkipsBadCode()
    {
        const QByteArray content = "BT /F1 12 Tf 72 700 Td (\x48\x90\x69) Tj ET\n";
        const QByteArray cmap =
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /M def\n/CMapType 2 def\n1 begincodespacerange\n<00> <FF>\n"
            "endcodespacerange\n3 beginbfchar\n<20> <0020>\n<48> <0048>\n<69> <0069>\n"
            "endbfchar\nendcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        const QByteArray pdf = buildPdf({
            { 1, "1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj" },
            { 2, "2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj" },
            { 3, "3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]"
                  "/Contents 4 0 R/Resources<</Font<</F1 6 0 R>>>>>>endobj" },
            { 4, "4 0 obj<</Length " + QByteArray::number(content.size()) + ">>stream\n"
                  + content + "endstream endobj" },
            { 5, "5 0 obj<</Length " + QByteArray::number(cmap.size()) + ">>stream\n"
                  + cmap + "endstream endobj" },
            { 6, "6 0 obj<</BaseFont/MyFont/Subtype/Type1/ToUnicode 5 0 R/Type/Font>>endobj" },
        });
        const QString path = m_tempDir.path() + QStringLiteral("/cmap_miss.pdf");
        QVERIFY(writeBytes(path, pdf));

        DocumentModel model;
        QVariantMap meta;
        QString error;
        QVERIFY2(PdfParser::read(path, &model, nullptr, meta, &error),
                 qPrintable(error));
        QCOMPARE(model.lineText(0), QStringLiteral("Hi"));
    }

private:
    QTemporaryDir m_tempDir;
    QString m_samplePath;
};

QTEST_MAIN(TestPdfParser)
#include "tst_pdf.moc"
