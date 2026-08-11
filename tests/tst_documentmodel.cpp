#include <QtTest>

#include "services/documentmodel.h"

class TestDocumentModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void emptyModel();
    void setLinesAndCount();
    void dataRoles();
    void lineText();
    void updateLineText();
    void insertLine();
    void removeLine();
    void appendLine();
    void comments();
    void clear();
    void outOfRangeSafety();
    void largeDocumentPerformance();
    void undoRedoTextEdit();
    void undoRedoInsertRemove();
    void undoHistoryClearedOnSetLines();
};

void TestDocumentModel::init()
{
    // 每个用例前重置
}

void TestDocumentModel::emptyModel()
{
    DocumentModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.lineCount(), 0);
}

void TestDocumentModel::setLinesAndCount()
{
    DocumentModel model;
    model.setLines({ "a", "b", "c" });
    QCOMPARE(model.lineCount(), 3);
    QCOMPARE(model.rowCount(), 3);
}

void TestDocumentModel::dataRoles()
{
    DocumentModel model;
    model.setLines({ "hello", "world" });

    QModelIndex idx = model.index(0);
    QVERIFY(idx.isValid());
    QCOMPARE(model.data(idx, DocumentModel::LineNumberRole).toInt(), 1);
    QCOMPARE(model.data(idx, DocumentModel::TextRole).toString(), "hello");
    QCOMPARE(model.data(idx, DocumentModel::HasCommentRole).toBool(), false);

    // roleNames 完整
    const auto roles = model.roleNames();
    QVERIFY(roles.contains(DocumentModel::LineNumberRole));
    QVERIFY(roles.contains(DocumentModel::TextRole));
    QVERIFY(roles.contains(DocumentModel::IsCommentRole));
    QVERIFY(roles.contains(DocumentModel::HasCommentRole));
    QVERIFY(roles.contains(DocumentModel::CommentTextRole));
}

void TestDocumentModel::lineText()
{
    DocumentModel model;
    model.setLines({ "first", "second" });
    QCOMPARE(model.lineText(0), "first");
    QCOMPARE(model.lineText(1), "second");
}

void TestDocumentModel::updateLineText()
{
    DocumentModel model;
    model.setLines({ "old" });
    model.updateLineText(0, "new");
    QCOMPARE(model.lineText(0), "new");
}

void TestDocumentModel::insertLine()
{
    DocumentModel model;
    model.setLines({ "a", "c" });
    const int idx = model.insertLine(1, "b");
    QCOMPARE(idx, 1);
    QCOMPARE(model.lineCount(), 3);
    QCOMPARE(model.lineText(0), "a");
    QCOMPARE(model.lineText(1), "b");
    QCOMPARE(model.lineText(2), "c");
}

void TestDocumentModel::removeLine()
{
    DocumentModel model;
    model.setLines({ "a", "b", "c" });
    const int idx = model.removeLine(1);
    QCOMPARE(idx, 1);
    QCOMPARE(model.lineCount(), 2);
    QCOMPARE(model.lineText(0), "a");
    QCOMPARE(model.lineText(1), "c");
}

void TestDocumentModel::appendLine()
{
    DocumentModel model;
    model.setLines({ "a" });
    const int idx = model.appendLine("b");
    QCOMPARE(idx, 1);
    QCOMPARE(model.lineCount(), 2);
    QCOMPARE(model.lineText(1), "b");
}

void TestDocumentModel::comments()
{
    DocumentModel model;
    model.setLines({ "a", "b", "c" });
    QVERIFY(!model.hasCommentAt(1));
    model.setComment(1, "translated");
    QVERIFY(model.hasCommentAt(1));
    QCOMPARE(model.commentAt(1), "translated");
    QVERIFY(!model.hasCommentAt(0));
    QVERIFY(!model.hasCommentAt(2));
    // 清空批注
    model.setComment(1, "   ");
    QVERIFY(!model.hasCommentAt(1));
}

void TestDocumentModel::clear()
{
    DocumentModel model;
    model.setLines({ "a", "b" });
    model.setComment(0, "c");
    model.clear();
    QCOMPARE(model.lineCount(), 0);
}

void TestDocumentModel::outOfRangeSafety()
{
    DocumentModel model;
    model.setLines({ "a" });
    // 越界访问不应崩溃
    QCOMPARE(model.lineText(-1), QString());
    QCOMPARE(model.lineText(5), QString());
    model.updateLineText(-1, "x");
    model.updateLineText(5, "x");
    model.setComment(-1, "x");
    model.setComment(5, "x");
    model.insertLine(-5, "x");
    model.removeLine(10);
    model.removeLine(-1);
    QCOMPARE(model.lineCount(), 1);
    QCOMPARE(model.lineText(0), "a");
}

void TestDocumentModel::largeDocumentPerformance()
{
    DocumentModel model;
    QStringList lines;
    const int count = 100000; // 10 万行
    lines.reserve(count);
    for (int i = 0; i < count; ++i) {
        lines.append(QStringLiteral("Line %1 some content for performance test").arg(i));
    }

    QBENCHMARK {
        model.setLines(lines);
    }
    QCOMPARE(model.lineCount(), count);

    // 随机访问不应太慢
    QBENCHMARK {
        for (int i = 0; i < 1000; ++i) {
            model.lineText(i * 97 % count);
        }
    }
}

void TestDocumentModel::undoRedoTextEdit()
{
    DocumentModel model;
    model.setLines({ "a", "b", "c" });
    model.updateLineText(1, "B-修改");
    QCOMPARE(model.lineText(1), "B-修改");
    QVERIFY(model.canUndo());

    QVERIFY(model.undo());
    QCOMPARE(model.lineText(1), "b");
    QVERIFY(model.canRedo());

    QVERIFY(model.redo());
    QCOMPARE(model.lineText(1), "B-修改");
    QVERIFY(!model.canRedo());
}

void TestDocumentModel::undoRedoInsertRemove()
{
    DocumentModel model;
    model.setLines({ "a", "b", "c" });

    // 插入 → undo → redo
    model.insertLine(1, "X");
    QCOMPARE(model.lineCount(), 4);
    QCOMPARE(model.lineText(1), "X");
    QVERIFY(model.undo());
    QCOMPARE(model.lineCount(), 3);
    QCOMPARE(model.lineText(1), "b");
    QVERIFY(model.redo());
    QCOMPARE(model.lineCount(), 4);
    QCOMPARE(model.lineText(1), "X");

    // 删除 → undo → redo
    model.removeLine(0);
    QCOMPARE(model.lineCount(), 3);
    QVERIFY(model.undo());
    QCOMPARE(model.lineCount(), 4);
    QCOMPARE(model.lineText(0), "a");
    QVERIFY(model.redo());
    QCOMPARE(model.lineCount(), 3);
}

void TestDocumentModel::undoHistoryClearedOnSetLines()
{
    DocumentModel model;
    model.setLines({ "a", "b" });
    model.updateLineText(0, "改");
    QVERIFY(model.canUndo());
    model.setLines({ "x" });
    QVERIFY(!model.canUndo());
    QVERIFY(!model.canRedo());
}

QTEST_GUILESS_MAIN(TestDocumentModel)
#include "tst_documentmodel.moc"
