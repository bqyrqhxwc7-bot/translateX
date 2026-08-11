#include <QtTest>

#include "services/findservice.h"
#include "services/documentmodel.h"

class TestFindService : public QObject
{
    Q_OBJECT

private slots:
    void findBasic();
    void findOptions();
    void findNextPrevious();
    void replaceLine();
    void replaceAll();
    void emptyQuerySafe();
};

void TestFindService::findBasic()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("hello world"), QStringLiteral("foo bar"),
                     QStringLiteral("Hello again"), QStringLiteral("nothing") });
    FindService fs;
    fs.setDocument(&model);

    // 默认大小写不敏感
    QCOMPARE(fs.find(QStringLiteral("hello")), QList<int>({ 0, 2 }));
    // 出现次数（行内多处）
    QCOMPARE(fs.count(QStringLiteral("o")), 6);
}

void TestFindService::findOptions()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("hello world"), QStringLiteral("foo bar"),
                     QStringLiteral("Hello again") });
    FindService fs;
    fs.setDocument(&model);

    // 大小写敏感
    QCOMPARE(fs.find(QStringLiteral("hello"), true, false), QList<int>({ 0 }));
    QCOMPARE(fs.find(QStringLiteral("Hello"), true, false), QList<int>({ 2 }));
    // 整词
    QCOMPARE(fs.find(QStringLiteral("bar"), false, true), QList<int>({ 1 }));
    QCOMPARE(fs.find(QStringLiteral("ba"), false, true), QList<int>());
}

void TestFindService::findNextPrevious()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("hello"), QStringLiteral("world"),
                     QStringLiteral("hello again"), QStringLiteral("bye") });
    FindService fs;
    fs.setDocument(&model);

    QCOMPARE(fs.findNext(QStringLiteral("hello"), 0), 0);
    QCOMPARE(fs.findNext(QStringLiteral("hello"), 1), 2);
    // wrap 回绕
    QCOMPARE(fs.findNext(QStringLiteral("hello"), 3), 0);
    // 无 wrap
    QCOMPARE(fs.findNext(QStringLiteral("hello"), 3, false, false, false), -1);

    QCOMPARE(fs.findPrevious(QStringLiteral("hello"), 2), 2);
    QCOMPARE(fs.findPrevious(QStringLiteral("hello"), 1), 0);
    QCOMPARE(fs.findPrevious(QStringLiteral("hello"), 3, false, false, true), 2);
}

void TestFindService::replaceLine()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("hello world"), QStringLiteral("nothing") });
    FindService fs;
    fs.setDocument(&model);

    QVERIFY(fs.replaceLine(0, QStringLiteral("hello"), QStringLiteral("bye")));
    QCOMPARE(model.lineText(0), QStringLiteral("bye world"));
    // 无匹配 → 不修改
    QVERIFY(!fs.replaceLine(0, QStringLiteral("missing"), QStringLiteral("x")));
    QCOMPARE(model.lineText(0), QStringLiteral("bye world"));
    // 越界安全
    QVERIFY(!fs.replaceLine(99, QStringLiteral("x"), QStringLiteral("y")));
}

void TestFindService::replaceAll()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("hello"), QStringLiteral("world"),
                     QStringLiteral("Hello again") });
    FindService fs;
    fs.setDocument(&model);

    QCOMPARE(fs.replaceAll(QStringLiteral("hello"), QStringLiteral("hi")), 2);
    QCOMPARE(model.lineText(0), QStringLiteral("hi"));
    QCOMPARE(model.lineText(2), QStringLiteral("hi again"));
    QCOMPARE(model.lineText(1), QStringLiteral("world"));
}

void TestFindService::emptyQuerySafe()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("a"), QStringLiteral("b") });
    FindService fs;
    fs.setDocument(&model);

    QCOMPARE(fs.find(QString()), QList<int>());
    QCOMPARE(fs.count(QString()), 0);
    QCOMPARE(fs.findNext(QString(), 0), -1);
    QCOMPARE(fs.findPrevious(QString(), 0), -1);
    QCOMPARE(fs.replaceAll(QString(), QStringLiteral("x")), 0);
    QVERIFY(!fs.replaceLine(0, QString(), QStringLiteral("x")));
}

QTEST_GUILESS_MAIN(TestFindService)
#include "tst_find.moc"
