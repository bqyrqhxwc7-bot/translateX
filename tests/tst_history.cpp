// 翻译历史服务测试（迭代4b）：record 顺序/字段、环形上限、clear、entryAdded 信号
#include <QtTest>

#include "services/translationhistoryservice.h"

class TestTranslationHistory : public QObject
{
    Q_OBJECT

private slots:
    void recordAppendsNewestFirst();
    void recordFields();
    void ringBufferCapsAt500();
    void clearEmitsAndEmpties();
    void entryAddedSignal();
};

void TestTranslationHistory::recordAppendsNewestFirst()
{
    TranslationHistoryService svc;
    svc.record(0, QStringLiteral("a"), QStringLiteral("甲"), true);
    svc.record(1, QStringLiteral("b"), QStringLiteral("乙"), true);
    const QVariantList entries = svc.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).toMap().value(QStringLiteral("line")).toInt(), 1);
    QCOMPARE(entries.at(1).toMap().value(QStringLiteral("line")).toInt(), 0);
    QCOMPARE(svc.count(), 2);
}

void TestTranslationHistory::recordFields()
{
    TranslationHistoryService svc;
    svc.record(3, QStringLiteral("hello world"), QStringLiteral("你好世界"), false);
    const QVariantMap e = svc.entries().at(0).toMap();
    QCOMPARE(e.value(QStringLiteral("line")).toInt(), 3);
    QCOMPARE(e.value(QStringLiteral("source")).toString(), QStringLiteral("hello world"));
    QCOMPARE(e.value(QStringLiteral("translated")).toString(), QStringLiteral("你好世界"));
    QCOMPARE(e.value(QStringLiteral("success")).toBool(), false);
    QVERIFY(!e.value(QStringLiteral("time")).toString().isEmpty());
}

void TestTranslationHistory::ringBufferCapsAt500()
{
    TranslationHistoryService svc;
    for (int i = 0; i < 520; ++i) {
        svc.record(i, QStringLiteral("s%1").arg(i), QStringLiteral("t%1").arg(i), true);
    }
    QCOMPARE(svc.count(), 500);
    const QVariantList entries = svc.entries();
    QCOMPARE(entries.size(), 500);
    // 最新在前：第 519 条在最前，最旧的第 20 条被覆盖
    QCOMPARE(entries.at(0).toMap().value(QStringLiteral("line")).toInt(), 519);
    QCOMPARE(entries.at(499).toMap().value(QStringLiteral("line")).toInt(), 20);
}

void TestTranslationHistory::clearEmitsAndEmpties()
{
    TranslationHistoryService svc;
    svc.record(0, QStringLiteral("a"), QStringLiteral("甲"), true);
    QSignalSpy spy(&svc, &TranslationHistoryService::entryAdded);
    svc.clear();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(svc.count(), 0);
    QVERIFY(svc.entries().isEmpty());
}

void TestTranslationHistory::entryAddedSignal()
{
    TranslationHistoryService svc;
    QSignalSpy spy(&svc, &TranslationHistoryService::entryAdded);
    svc.record(0, QStringLiteral("a"), QStringLiteral("甲"), true);
    svc.record(1, QStringLiteral("b"), QStringLiteral("乙"), false);
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(TestTranslationHistory)
#include "tst_history.moc"