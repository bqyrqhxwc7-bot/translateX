#include <QtTest>
#include <atomic>
#include <memory>

#include "services/termglossary.h"
#include "services/qualitygate.h"
#include "services/translationcache.h"
#include "services/translationservice.h"
#include "services/serviceregistry.h"

// 用于验证智能分块的 Mock 后端：记录每次批量请求收到的行号
class MockBackend : public ITranslationBackend
{
public:
    QString backendId() const override { return QStringLiteral("test.mock"); }
    QString displayName() const override { return QStringLiteral("Mock"); }

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        Q_UNUSED(options);
        Q_UNUSED(cancelFlag);
        ++singleCalls;
        TranslationResult r;
        r.success = true;
        r.text = QStringLiteral("译:") + text;
        return r;
    }

    QList<QPair<int, TranslationResult>> translateBatch(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        Q_UNUSED(options);
        Q_UNUSED(cancelFlag);
        batchCalls.append(targetLines);
        QList<QPair<int, TranslationResult>> results;
        for (int ln : targetLines) {
            TranslationResult r;
            r.success = true;
            r.text = QStringLiteral("译:") + sourceLines.at(ln);
            results.append(qMakePair(ln, r));
        }
        return results;
    }

    int singleCalls = 0;          // 单行请求次数
    QList<QList<int>> batchCalls; // 每次批量请求的行号块
};

class TestQuality : public QObject
{
    Q_OBJECT

private slots:
    void glossaryRoundTrip();
    void glossaryConstraintPrompt();
    void glossaryVerify();
    void qualityGateEcho();
    void qualityGateLength();
    void qualityGatePreservesTokens();
    void diskCachePersists();
    void chunkingMergesContiguous();
    void chunkingNonContiguous();
    void chunkingBudgetSplit();
    void glossaryAffectsCacheKey();
};

void TestQuality::glossaryRoundTrip()
{
    TermGlossary g;
    g.setTerm("API", "应用程序接口");
    g.setTerm("Token", "令牌");
    QCOMPARE(g.size(), 2);
    QVERIFY(g.contains("API"));
    QCOMPARE(g.translationFor("API"), QStringLiteral("应用程序接口"));

    QVariantMap map = g.toMap();
    TermGlossary g2;
    g2.loadFromMap(map);
    QCOMPARE(g2.size(), 2);
    QCOMPARE(g2.translationFor("Token"), QStringLiteral("令牌"));

    g.removeTerm("API");
    QCOMPARE(g.size(), 1);
    g.clear();
    QCOMPARE(g.size(), 0);
}

void TestQuality::glossaryConstraintPrompt()
{
    TermGlossary g;
    g.setTerm("API", "应用程序接口");
    const QString prompt = g.buildConstraintPrompt();
    QVERIFY(prompt.contains("API"));
    QVERIFY(prompt.contains("应用程序接口"));
}

void TestQuality::glossaryVerify()
{
    TermGlossary g;
    g.setTerm("API", "应用程序接口");
    g.setTerm("cache", "缓存");

    // 全命中
    QCOMPARE(g.verify("The API uses cache.", "该应用程序接口使用缓存。"), 1.0);
    // 部分命中（API 未按标准译）
    const double score = g.verify("The API uses cache.", "API 使用缓存。");
    QVERIFY(score > 0.0 && score < 1.0);
    QCOMPARE(g.missingTerms("The API uses cache.", "API 使用缓存。"),
             QStringList({ QStringLiteral("API") }));
    // 源文没有术语 → 视为通过
    QCOMPARE(g.verify("hello world", "你好世界"), 1.0);
}

void TestQuality::qualityGateEcho()
{
    // 纯回显 = 未翻译
    const QualityReport report = QualityGate::evaluate("hello world", "hello world", nullptr);
    QVERIFY(!report.passed);

    // 近似回显（细微差异）也应拦截：后端对非源语言文本原样返回时
    QVERIFY(!QualityGate::notJustEcho("The quick brown fox", "The quick browm fox"));   // 1 字符差
    QVERIFY(!QualityGate::notJustEcho("これはテストです", "これはテストです"));          // 非英文原文回显
    QVERIFY(QualityGate::notJustEcho("The quick brown fox", "敏捷的棕色狐狸"));          // 真译文
}

void TestQuality::qualityGateLength()
{
    // 优质英→中译文应通过长度规则（中文更紧凑但不过分）
    QVERIFY(QualityGate::lengthReasonable(
        "The quick brown fox jumps over the lazy dog.",
        "敏捷的棕色狐狸跳过了懒狗。"));
    // 明显过短 → 异常
    QVERIFY(!QualityGate::lengthReasonable(
        "The quick brown fox jumps over the lazy dog.",
        "好"));
    // 明显过长 → 异常
    QVERIFY(!QualityGate::lengthReasonable(
        "hi",
        QString(30, QLatin1Char('长'))));
}

void TestQuality::qualityGatePreservesTokens()
{
    // 数字应保留
    QVERIFY(QualityGate::preservesTokens("Version 3.14 released", "版本 3.14 已发布"));
    // 完全丢失数字 → 失败
    QVERIFY(!QualityGate::preservesTokens("Version 3.14 released", "版本已发布"));
    // 代码标识符（驼峰）应保留
    QVERIFY(QualityGate::preservesTokens("Call loadFile() to open", "调用 loadFile() 打开"));
    QVERIFY(!QualityGate::preservesTokens("Call loadFile() to open", "调用函数打开"));
    // 纯英文自然语言不含需保留 token（不误报）
    QVERIFY(QualityGate::preservesTokens("hello world", "你好世界"));
}

void TestQuality::diskCachePersists()
{
    TranslationCache cache;
    TranslationOptions options;
    TranslationResult result;
    result.text = QStringLiteral("磁盘缓存译文");
    result.success = true;

    const QString key = cache.key(QStringLiteral("disk test phrase"), options);
    cache.put(key, result);

    // 新实例从磁盘读取（模拟跨会话）
    TranslationCache cache2;
    const TranslationResult got = cache2.get(key);
    QVERIFY2(got.success, "磁盘缓存应能跨实例读取");
    QCOMPARE(got.text, QStringLiteral("磁盘缓存译文"));
    QVERIFY(got.fromCache);
}

void TestQuality::chunkingMergesContiguous()
{
    auto mock = std::make_shared<MockBackend>();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("test.mock"), [mock]() { return mock; }, QStringLiteral("Mock"));

    TranslationService service;
    service.setBackend(QStringLiteral("test.mock"));
    service.setFallbackEnabled(false);
    service.setCacheEnabled(false);
    service.setMaxChunkChars(100000); // 预算充足 → 全部合并

    QStringList source{ "alpha", "beta", "gamma", "delta", "epsilon" };
    QList<int> targets{ 0, 1, 2, 3, 4 };
    const auto results = service.translateBatchSync(source, targets);

    QCOMPARE(results.size(), 5);
    // 5 条连续短行 → 1 个批量请求
    QCOMPARE(mock->batchCalls.size(), 1);
    QCOMPARE(mock->batchCalls.first(), QList<int>({ 0, 1, 2, 3, 4 }));
    QCOMPARE(mock->singleCalls, 0); // 全部由批量完成，未走单行
    // 行序保留
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(results[i].first, i);
        QVERIFY(results[i].second.success);
    }
}

void TestQuality::chunkingNonContiguous()
{
    auto mock = std::make_shared<MockBackend>();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("test.mock"), [mock]() { return mock; }, QStringLiteral("Mock"));

    TranslationService service;
    service.setBackend(QStringLiteral("test.mock"));
    service.setFallbackEnabled(false);
    service.setCacheEnabled(false);
    service.setMaxChunkChars(100000);

    QStringList source{ "alpha", "beta", "gamma", "delta", "epsilon" };
    QList<int> targets{ 0, 2, 4 }; // 不连续 → 各自成块
    const auto results = service.translateBatchSync(source, targets);

    QCOMPARE(results.size(), 3);
    QCOMPARE(mock->batchCalls.size(), 3);
    QCOMPARE(mock->batchCalls[0], QList<int>({ 0 }));
    QCOMPARE(mock->batchCalls[1], QList<int>({ 2 }));
    QCOMPARE(mock->batchCalls[2], QList<int>({ 4 }));
}

void TestQuality::chunkingBudgetSplit()
{
    auto mock = std::make_shared<MockBackend>();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("test.mock"), [mock]() { return mock; }, QStringLiteral("Mock"));

    TranslationService service;
    service.setBackend(QStringLiteral("test.mock"));
    service.setFallbackEnabled(false);
    service.setCacheEnabled(false);
    service.setMaxChunkChars(256); // 每行 200 字符 → 两行超预算 → 逐行成块

    QStringList source;
    for (int i = 0; i < 5; ++i) {
        source << QString(200, QLatin1Char(static_cast<char>('a' + i)));
    }
    QList<int> targets{ 0, 1, 2, 3, 4 };
    const auto results = service.translateBatchSync(source, targets);

    QCOMPARE(results.size(), 5);
    QCOMPARE(mock->batchCalls.size(), 5); // 每块 1 次
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(mock->batchCalls[i], QList<int>({ i }));
    }
}

void TestQuality::glossaryAffectsCacheKey()
{
    TranslationCache cache;
    TranslationOptions withGlossary;
    withGlossary.extra.insert(QStringLiteral("glossaryConstraint"), QStringLiteral("“API” 一律译为 “应用程序接口”"));
    TranslationOptions withoutGlossary;
    const QString k1 = cache.key(QStringLiteral("hello API"), withGlossary);
    const QString k2 = cache.key(QStringLiteral("hello API"), withoutGlossary);
    QVERIFY(k1 != k2); // 术语表变化应改变缓存键，避免旧缓存复用
}

QTEST_GUILESS_MAIN(TestQuality)
#include "tst_quality.moc"
