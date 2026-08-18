#include <QtTest>
#include <QDir>
#include <atomic>
#include <memory>

#include "services/configservice.h"
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

    void updateConfig(const QVariantMap &config) override { lastConfig = config; }
    QString healthCheck() const override { return QString(); }

    QVariantMap lastConfig; // 最近一次 updateConfig 收到的参数（验证配置合并）
    TranslationOptions lastOptions; // 最近一次 translate 收到的选项（验证 extra 传递）

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        Q_UNUSED(cancelFlag);
        ++singleCalls;
        lastOptions = options;
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
    void initTestCase();
    void cleanupTestCase();
    void glossaryRoundTrip();
    void connectionTestUsesConfigServiceSection();
    void glossaryConstraintPrompt();
    void glossaryVerify();
    void qualityGateEcho();
    void qualityGateLength();
    void qualityGatePreservesTokens();
    void diskCachePersists();
    void chunkingMergesContiguous();
    void chunkingNonContiguous();
    void chunkingBudgetSplit();
    void chunkingSentenceBoundary();
    void glossaryAffectsCacheKey();
    void extractCandidatesBasic();
    void extractCandidatesFiltering();
    void emptyTranslationPlaceholder();
};

void TestQuality::initTestCase()
{
    // 配置隔离：防止测试写入（如 setSentenceAwareChunking 落盘）污染真实配置/跨进程泄漏
    ConfigService::setDataDirectoryForTest(
        QDir::tempPath() + QStringLiteral("/tst_quality_config"));
}

void TestQuality::cleanupTestCase()
{
    QDir(QDir::tempPath() + QStringLiteral("/tst_quality_config")).removeRecursively();
}

void TestQuality::connectionTestUsesConfigServiceSection()
{
    // 回归：testBackendConnection 必须合并 ConfigService 中后端 section 的用户配置
    //（apiEndpoint/apiKey/model），仅用 m_backendConfig（运行时覆盖）会导致永远"未配置"。
    ConfigService::instance()->set(
        QStringLiteral("translation.network_model"),
        QStringLiteral("apiEndpoint"), QStringLiteral("http://127.0.0.1:9999/v1"));

    // 注意：TranslationService 构造时会重注册内置后端（覆盖同 id），
    // 因此 mock 必须在 service 构造之后注册
    TranslationService service;
    auto mock = std::make_shared<MockBackend>();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("translation.network_model"),
        [mock]() { return mock; }, QStringLiteral("Mock"));

    QSignalSpy spy(&service, &TranslationService::connectionTested);
    service.testBackendConnection(QStringLiteral("translation.network_model"));
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.takeFirst().at(1).toBool(), true);      // 探测成功
    QCOMPARE(mock->lastConfig.value(QStringLiteral("apiEndpoint")).toString(),
             QStringLiteral("http://127.0.0.1:9999/v1")); // 配置已合并传入后端
    // NetworkModelBackend 不重写 updateConfig（基类空实现），配置必须经 options.extra 传递
    QCOMPARE(mock->lastOptions.extra.value(QStringLiteral("apiEndpoint")).toString(),
             QStringLiteral("http://127.0.0.1:9999/v1"));
}

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
    // 完全一致 = 未翻译（任何场景）
    const QualityReport report = QualityGate::evaluate("hello world", "hello world", nullptr);
    QVERIFY(!report.passed);
    QVERIFY(!QualityGate::notJustEcho("hello world", "hello world"));

    // 近似回显（同语言场景 sameLanguage=true）：细微差异也拦截
    QVERIFY(!QualityGate::notJustEcho("The quick brown fox", "The quick browm fox", true));   // 1 字符差
    QVERIFY(!QualityGate::notJustEcho("私は日本語です", "私は日本語です", true));               // 日文原文回显
    QVERIFY(QualityGate::notJustEcho("The quick brown fox", "敏捷的棕色狐狸", true));          // 真实翻译通过

    // 跨语言场景（auto→ja 等）：不做近似检测，防共享汉字误杀
    // 中→日共享汉字译文必须通过
    QVERIFY(QualityGate::notJustEcho("第一章内容", "第一章の内容"));
    // 近似但非完全一致也放行（跨语言）
    QVERIFY(QualityGate::notJustEcho("The quick brown fox", "The quick browm fox"));
    // 完全一致仍拦截（跨语言）
    QVERIFY(!QualityGate::notJustEcho("hello world", "hello world"));
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
        QString(30, QChar(0x957F))));   // 30 个「长」（不能用 QLatin1Char 多字节字面量，clang 严格）
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

void TestQuality::chunkingSentenceBoundary()
{
    auto mock = std::make_shared<MockBackend>();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("test.mock"), [mock]() { return mock; }, QStringLiteral("Mock"));

    TranslationService service;
    service.setBackend(QStringLiteral("test.mock"));
    service.setFallbackEnabled(false);
    service.setCacheEnabled(false);
    service.setMaxChunkChars(100000); // 预算充足，仅句边界截断

    // 第 0 行以句号结尾 → 第 1 行必须另起一块（即使预算充足）
    QStringList source{ "第一句。", "第二句", "第三句！", "第四句", "第五句" };
    QList<int> targets{ 0, 1, 2, 3, 4 };
    service.translateBatchSync(source, targets);

    QCOMPARE(mock->batchCalls.size(), 3);
    QCOMPARE(mock->batchCalls[0], QList<int>({ 0 }));
    QCOMPARE(mock->batchCalls[1], QList<int>({ 1, 2 }));
    QCOMPARE(mock->batchCalls[2], QList<int>({ 3, 4 }));

    // 关闭句边界 → 全部合并为一块
    mock->batchCalls.clear();
    service.setSentenceAwareChunking(false);
    service.translateBatchSync(source, targets);
    QCOMPARE(mock->batchCalls.size(), 1);
    QCOMPARE(mock->batchCalls[0], QList<int>({ 0, 1, 2, 3, 4 }));
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

// ---- 迭代4：术语自动提取 ----

void TestQuality::extractCandidatesBasic()
{
    TermGlossary g;
    const QStringList lines{
        QStringLiteral("The API provides the API endpoint for the API client."),
        QStringLiteral("The client uses the API client endpoint."),
        QStringLiteral("中文行不参与提取"),
    };
    const auto candidates = g.extractCandidates(lines, 3, 20);
    // API 出现 4 次、client 3 次、endpoint 2 次（< minFreq 3 被过滤）
    QCOMPARE(candidates.size(), 2);
    // 返回最高频的实际书写形式（大小写不敏感归组，保留原文形式）
    QCOMPARE(candidates[0].first, QStringLiteral("API"));
    QCOMPARE(candidates[0].second, 4);
    QCOMPARE(candidates[1].first, QStringLiteral("client"));
    QCOMPARE(candidates[1].second, 3);
}

void TestQuality::extractCandidatesFiltering()
{
    TermGlossary g;
    // 停用词（the/and/for）与已收录术语被过滤（大小写不敏感：术语表存 API，正文 api 也过滤）；
    // maxCount 截断
    g.setTerm(QStringLiteral("API"), QStringLiteral("应用程序接口"));
    const QStringList lines{
        QStringLiteral("The API and the client for the server."),
        QStringLiteral("api client server client server"),
    };
    const auto candidates = g.extractCandidates(lines, 2, 2);
    // 候选：client(3) server(3)；api/API 已在术语表被排除；the/and/for 停用词排除
    QCOMPARE(candidates.size(), 2);
    QCOMPARE(candidates[0].first, QStringLiteral("client"));
    QCOMPARE(candidates[0].second, 3);
    QCOMPARE(candidates[1].first, QStringLiteral("server"));
    QCOMPARE(candidates[1].second, 3);

    // minFreq/maxCount 边界：minFreq 0 → 视为 1；maxCount 0 → 视为 1
    const auto edge = g.extractCandidates(lines, 0, 0);
    QCOMPARE(edge.size(), 1);
}

// 空译文占位：不注入提示词、不参与质量校验（review fd8d1f3 🟡8）
void TestQuality::emptyTranslationPlaceholder()
{
    TermGlossary g;
    g.setTerm(QStringLiteral("API"), QString());
    g.setTerm(QStringLiteral("client"), QStringLiteral("客户端"));
    // 提示词只含已填译文的术语
    const QString prompt = g.buildConstraintPrompt();
    QVERIFY(!prompt.contains(QStringLiteral("API")));
    QVERIFY(prompt.contains(QStringLiteral("client")));
    // 校验：空译文项不参与（源文含 API 也不计入 missing）
    QCOMPARE(g.verify(QStringLiteral("API client"), QStringLiteral("客户端")), 1.0);
    QVERIFY(g.missingTerms(QStringLiteral("API client"), QStringLiteral("客户端")).isEmpty());
}

QTEST_GUILESS_MAIN(TestQuality)
#include "tst_quality.moc"
