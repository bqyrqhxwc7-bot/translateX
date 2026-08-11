#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>

#include "services/commentservice.h"
#include "services/documentmodel.h"

class TestCommentService : public QObject
{
    Q_OBJECT

private slots:
    void crudBasics();
    void emptyTextDeletes();
    void shiftLines();
    void exportImportRoundTrip();
    void documentModelIntegration();
    void documentModelNoProviderFallback();
    void undoRedoShiftsComments();

private:
    QTemporaryDir m_temp;
};

void TestCommentService::crudBasics()
{
    CommentService c;
    c.setComment(0, QStringLiteral("译文A"));
    c.setComment(2, QStringLiteral("译文B"));
    QCOMPARE(c.count(), 2);
    QVERIFY(c.hasCommentAt(0));
    QVERIFY(!c.hasCommentAt(1));
    QCOMPARE(c.commentAt(2), QStringLiteral("译文B"));
    QCOMPARE(c.commentAt(99), QString());

    // removeComment
    c.removeComment(0);
    QVERIFY(!c.hasCommentAt(0));
    QCOMPARE(c.count(), 1);

    // clear
    c.clear();
    QCOMPARE(c.count(), 0);
    QVERIFY(!c.hasCommentAt(2));
}

void TestCommentService::emptyTextDeletes()
{
    CommentService c;
    c.setComment(3, QStringLiteral("   "));
    QVERIFY(!c.hasCommentAt(3)); // 空白视为删除

    c.setComment(4, QStringLiteral("有效批注"));
    QVERIFY(c.hasCommentAt(4));
    c.setComment(4, QString());
    QVERIFY(!c.hasCommentAt(4)); // 空串删除
}

void TestCommentService::shiftLines()
{
    CommentService c;
    c.setComment(2, QStringLiteral("A"));
    c.setComment(5, QStringLiteral("B"));
    c.setComment(0, QStringLiteral("C"));

    // 在第 3 行前插入 → 行号 ≥3 的 +1
    c.shiftLines(3, +1);
    QVERIFY(c.hasCommentAt(0)); // 不变
    QVERIFY(c.hasCommentAt(2)); // 不变（2 < 3）
    QVERIFY(!c.hasCommentAt(3));
    QVERIFY(!c.hasCommentAt(4)); // 无批注
    QVERIFY(c.hasCommentAt(6));  // 原 5 → 6

    // 删除第 5 行位置 → 行号 ≥5 的 -1
    c.shiftLines(5, -1);
    QVERIFY(c.hasCommentAt(0));
    QVERIFY(c.hasCommentAt(2));  // 不变
    QVERIFY(c.hasCommentAt(5));  // 原 6 → 5
    QVERIFY(!c.hasCommentAt(6));
}

void TestCommentService::exportImportRoundTrip()
{
    CommentService c;
    c.setComment(1, QStringLiteral("第一行批注"));
    c.setComment(9, QStringLiteral("第九行批注"));

    const QString path = m_temp.filePath(QStringLiteral("comments.json"));
    QVERIFY(c.exportToFile(path));
    QVERIFY(QFile::exists(path));

    CommentService c2;
    c2.setComment(99, QStringLiteral("将被覆盖"));
    QVERIFY(c2.importFromFile(path));
    QCOMPARE(c2.count(), 2);
    QCOMPARE(c2.commentAt(1), QStringLiteral("第一行批注"));
    QCOMPARE(c2.commentAt(9), QStringLiteral("第九行批注"));
    QVERIFY(!c2.hasCommentAt(99));
}

void TestCommentService::documentModelIntegration()
{
    DocumentModel model;
    CommentService comments;

    model.setLines({ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d") });
    model.setCommentProvider(&comments);

    // 委托写入：角色即时反映
    model.setComment(1, QStringLiteral("批注1"));
    QVERIFY(comments.hasCommentAt(1));
    QVERIFY(model.hasCommentAt(1));
    QCOMPARE(model.commentAt(1), QStringLiteral("批注1"));
    QCOMPARE(model.data(model.index(1), DocumentModel::HasCommentRole).toBool(), true);
    QCOMPARE(model.data(model.index(1), DocumentModel::CommentTextRole).toString(), QStringLiteral("批注1"));
    QVERIFY(!model.data(model.index(0), DocumentModel::HasCommentRole).toBool());

    // 插入行：批注行号跟随（原 1 → 2）
    const int newIndex = model.insertLine(1, QStringLiteral("new"));
    Q_UNUSED(newIndex);
    QVERIFY(comments.hasCommentAt(2));
    QVERIFY(!comments.hasCommentAt(1));

    // 删除行：其后行号 -1（原 2 → 1）
    model.removeLine(0);
    QVERIFY(comments.hasCommentAt(1));
    QVERIFY(!comments.hasCommentAt(0));

    // 重新加载文档：批注清空
    model.setLines({ QStringLiteral("x"), QStringLiteral("y") });
    QCOMPARE(comments.count(), 0);
}

void TestCommentService::documentModelNoProviderFallback()
{
    // 无 provider 时退回内部存储（兼容独立使用）
    DocumentModel model;
    model.setLines({ QStringLiteral("a"), QStringLiteral("b") });
    model.setComment(0, QStringLiteral("内部批注"));
    QVERIFY(model.hasCommentAt(0));
    QCOMPARE(model.commentAt(0), QStringLiteral("内部批注"));
    QCOMPARE(model.data(model.index(0), DocumentModel::HasCommentRole).toBool(), true);
    QVERIFY(!model.hasCommentAt(1));
}

void TestCommentService::undoRedoShiftsComments()
{
    DocumentModel model;
    CommentService comments;
    model.setLines({ "a", "b", "c", "d" });
    model.setCommentProvider(&comments);
    model.setComment(1, QStringLiteral("批注1"));

    // 插入行 1 → 批注平移到 2；undo → 回到 1；redo → 再到 2
    model.insertLine(1, QStringLiteral("X"));
    QVERIFY(comments.hasCommentAt(2));
    QVERIFY(!comments.hasCommentAt(1));
    QVERIFY(model.undo());
    QCOMPARE(model.lineCount(), 4);
    QVERIFY(comments.hasCommentAt(1));
    QVERIFY(!comments.hasCommentAt(2));
    QVERIFY(model.redo());
    QVERIFY(comments.hasCommentAt(2));
    QVERIFY(!comments.hasCommentAt(1));
}

QTEST_GUILESS_MAIN(TestCommentService)
#include "tst_comment.moc"
