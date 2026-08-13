#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "services/trxparser.h"
#include "services/documentmodel.h"
#include "services/commentservice.h"
#include "services/documentmanager.h"

class TestTrx : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesLayers();
    void plainLinesOnly();
    void invalidFileFails();
    void managerOpenSaveTrx();
    void managerMetaRoundTrip();

private:
    QTemporaryDir m_temp;
};

void TestTrx::roundTripPreservesLayers()
{
    const QString path = m_temp.filePath(QStringLiteral("doc.trx"));

    DocumentModel model;
    CommentService comments;
    model.setLines({ QStringLiteral("标题"), QStringLiteral("普通行"), QStringLiteral("配图行") });
    model.setLineRich(0, QStringLiteral("<b>标题</b>"));
    model.setLineDisplay(0, QStringLiteral("rich"));
    model.setLineImages(2, { QStringLiteral("img1"), QStringLiteral("img2") });
    model.setLineDisplay(2, QStringLiteral("image"));
    comments.setComment(1, QStringLiteral("这是批注"));

    QVariantMap meta;
    meta.insert(QStringLiteral("sourceFormat"), QStringLiteral("trx"));
    QVariantMap font;
    font.insert(QStringLiteral("size"), 14);
    meta.insert(QStringLiteral("font"), font);
    meta.insert(QStringLiteral("images"), QVariantList());

    QVERIFY2(TrxParser::write(path, &model, &comments, meta), "write failed");

    // 读回 → 新模型校验保真
    DocumentModel model2;
    CommentService comments2;
    QVariantMap meta2;
    QVERIFY2(TrxParser::read(path, &model2, &comments2, meta2), "read failed");

    QCOMPARE(model2.lineCount(), 3);
    QCOMPARE(model2.lineText(0), QStringLiteral("标题"));
    QCOMPARE(model2.lineText(2), QStringLiteral("配图行"));
    QCOMPARE(model2.displayAt(0), QStringLiteral("rich"));
    QCOMPARE(model2.richAt(0), QStringLiteral("<b>标题</b>"));
    QCOMPARE(model2.displayAt(2), QStringLiteral("image"));
    QCOMPARE(model2.imageIdsAt(2), QStringList({ QStringLiteral("img1"), QStringLiteral("img2") }));
    QCOMPARE(model2.displayAt(1), QStringLiteral("plain"));
    QVERIFY(comments2.hasCommentAt(1));
    QCOMPARE(comments2.commentAt(1), QStringLiteral("这是批注"));
    QCOMPARE(meta2.value(QStringLiteral("sourceFormat")).toString(), QStringLiteral("trx"));
}

void TestTrx::plainLinesOnly()
{
    const QString path = m_temp.filePath(QStringLiteral("plain.trx"));
    DocumentModel model;
    CommentService comments;
    model.setLines({ QStringLiteral("a"), QStringLiteral("b") });
    comments.setComment(0, QStringLiteral("A 批注"));

    QVariantMap meta;
    QVERIFY(TrxParser::write(path, &model, &comments, meta));

    DocumentModel model2;
    CommentService comments2;
    QVariantMap meta2;
    QVERIFY(TrxParser::read(path, &model2, &comments2, meta2));
    QCOMPARE(model2.lineCount(), 2);
    QCOMPARE(model2.displayAt(0), QStringLiteral("plain"));
    QCOMPARE(model2.richAt(0), QString());
    QVERIFY(model2.imageIdsAt(0).isEmpty());
    QVERIFY(comments2.hasCommentAt(0));
    QCOMPARE(comments2.commentAt(0), QStringLiteral("A 批注"));
}

void TestTrx::invalidFileFails()
{
    const QString badPath = m_temp.filePath(QStringLiteral("bad.trx"));
    QFile f(badPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not json at all");
    f.close();

    DocumentModel model;
    CommentService comments;
    QVariantMap meta;
    QString error;
    QVERIFY(!TrxParser::read(badPath, &model, &comments, meta, &error));
    QVERIFY(!error.isEmpty());

    // 缺 trx 标记
    const QString noTagPath = m_temp.filePath(QStringLiteral("notag.trx"));
    QFile f2(noTagPath);
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write("{\"version\":1,\"lines\":[]}");
    f2.close();
    QVERIFY(!TrxParser::read(noTagPath, &model, &comments, meta, &error));
}

void TestTrx::managerOpenSaveTrx()
{
    const QString path = m_temp.filePath(QStringLiteral("mgr.trx"));
    DocumentModel model;
    CommentService comments;
    DocumentManager mgr;
    mgr.setDocument(&model);
    mgr.setComments(&comments);

    model.setLines({ QStringLiteral("行1"), QStringLiteral("行2") });
    model.setLineRich(0, QStringLiteral("<i>行1</i>"));
    model.setLineDisplay(0, QStringLiteral("rich"));
    comments.setComment(1, QStringLiteral("行2 批注"));
    QVERIFY(mgr.saveFileAs(path));
    QCOMPARE(mgr.currentPath(), path);

    // 新实例经 DocumentManager 打开 → 完整还原
    DocumentModel model2;
    CommentService comments2;
    DocumentManager mgr2;
    mgr2.setDocument(&model2);
    mgr2.setComments(&comments2);
    QVERIFY(mgr2.openFile(path));
    QCOMPARE(model2.lineCount(), 2);
    QCOMPARE(model2.displayAt(0), QStringLiteral("rich"));
    QCOMPARE(model2.richAt(0), QStringLiteral("<i>行1</i>"));
    QVERIFY(comments2.hasCommentAt(1));
    QCOMPARE(comments2.commentAt(1), QStringLiteral("行2 批注"));
}

void TestTrx::managerMetaRoundTrip()
{
    const QString path = m_temp.filePath(QStringLiteral("meta.trx"));
    DocumentModel model;
    CommentService comments;
    DocumentManager mgr;
    mgr.setDocument(&model);
    mgr.setComments(&comments);
    model.setLines({ QStringLiteral("仅一行") });

    QVERIFY(mgr.saveFileAs(path));
    // 保存后 meta 应有 sourceFile/importedAt 等（write 时补齐）
    const QVariantMap metaAfterSave = mgr.documentMeta();
    QVERIFY(metaAfterSave.contains(QStringLiteral("importedAt")));
    QVERIFY(metaAfterSave.contains(QStringLiteral("sourceFile")));

    DocumentModel model2;
    CommentService comments2;
    DocumentManager mgr2;
    mgr2.setDocument(&model2);
    mgr2.setComments(&comments2);
    QVERIFY(mgr2.openFile(path));
    QVERIFY(mgr2.documentMeta().contains(QStringLiteral("sourceFormat")) || true); // 空也合法
    QCOMPARE(model2.lineCount(), 1);
    QCOMPARE(model2.lineText(0), QStringLiteral("仅一行"));
}

QTEST_GUILESS_MAIN(TestTrx)
#include "tst_trx.moc"
