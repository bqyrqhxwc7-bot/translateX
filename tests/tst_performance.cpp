#include <QtTest>
#include <QElapsedTimer>

#include "services/documentmodel.h"

// 性能基准：大文件场景下的加载、更新、批注操作耗时
class TestDocumentPerformance : public QObject
{
    Q_OBJECT

private slots:
    void loadLargeDocument();
    void updateManyLines();
    void addManyComments();
    void insertDeleteStress();
    void dataAccessRandom();
};

static QStringList makeLines(int count)
{
    QStringList lines;
    lines.reserve(count);
    for (int i = 0; i < count; ++i) {
        lines.append(QStringLiteral("第 %1 行：The quick brown fox jumps over the lazy dog. 用于性能测试的内容文本。").arg(i));
    }
    return lines;
}

void TestDocumentPerformance::loadLargeDocument()
{
    DocumentModel model;
    const int count = 500000; // 50 万行（模拟大文件）
    const QStringList lines = makeLines(count);

    QElapsedTimer timer;
    timer.start();
    model.setLines(lines);
    const qint64 loadMs = timer.nsecsElapsed() / 1000000;

    qInfo() << "[PERF] 加载" << count << "行耗时:" << loadMs << "ms";
    QVERIFY2(loadMs < 3000, "50 万行加载应 < 3 秒");
    QCOMPARE(model.lineCount(), count);
}

void TestDocumentPerformance::updateManyLines()
{
    DocumentModel model;
    const int count = 100000;
    model.setLines(makeLines(count));

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < count; ++i) {
        model.updateLineText(i, QStringLiteral("updated %1").arg(i));
    }
    const qint64 ms = timer.nsecsElapsed() / 1000000;
    qInfo() << "[PERF] 更新" << count << "行耗时:" << ms << "ms";
    QVERIFY2(ms < 2000, "10 万行更新应 < 2 秒");
}

void TestDocumentPerformance::addManyComments()
{
    DocumentModel model;
    const int count = 50000;
    model.setLines(makeLines(count));

    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < count; i += 2) {
        model.setComment(i, QStringLiteral("翻译批注 %1").arg(i));
    }
    const qint64 ms = timer.nsecsElapsed() / 1000000;
    qInfo() << "[PERF] 添加" << count / 2 << "条批注耗时:" << ms << "ms";
    QVERIFY2(ms < 2000, "2.5 万条批注应 < 2 秒");
    QVERIFY(model.hasCommentAt(0));
    QVERIFY(!model.hasCommentAt(1));
}

void TestDocumentPerformance::insertDeleteStress()
{
    DocumentModel model;
    const int count = 10000;
    model.setLines(makeLines(count));

    QElapsedTimer timer;
    timer.start();
    // 在中间反复插入/删除（模拟编辑）
    for (int i = 0; i < 500; ++i) {
        const int pos = 5000;
        model.insertLine(pos, QStringLiteral("inserted %1").arg(i));
        model.removeLine(pos);
    }
    const qint64 ms = timer.nsecsElapsed() / 1000000;
    qInfo() << "[PERF] 500 次插入+删除耗时:" << ms << "ms";
    QVERIFY2(ms < 2000, "500 次插删应 < 2 秒");
    QCOMPARE(model.lineCount(), count);
}

void TestDocumentPerformance::dataAccessRandom()
{
    DocumentModel model;
    const int count = 200000;
    model.setLines(makeLines(count));

    QElapsedTimer timer;
    timer.start();
    int total = 0;
    for (int i = 0; i < 20000; ++i) {
        total += model.lineText((i * 7919) % count).length();
    }
    const qint64 ms = timer.nsecsElapsed() / 1000000;
    qInfo() << "[PERF] 2 万次随机行访问耗时:" << ms << "ms (字符数=" << total << ")";
    QVERIFY2(ms < 500, "2 万次随机访问应 < 500ms");
}

QTEST_GUILESS_MAIN(TestDocumentPerformance)
#include "tst_performance.moc"
