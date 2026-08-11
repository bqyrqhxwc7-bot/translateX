#include <QtTest>
#include <QThread>
#include <QSignalSpy>
#include <QDir>

#include "services/serviceregistry.h"
#include "services/translationbackend.h"
#include "services/translationcache.h"
#include "services/translationservice.h"
#include "services/configservice.h"

// 可控的慢速假后端：单条翻译耗时约 120ms，且响应取消标志（供进度/取消测试）
class SlowFakeBackend : public ITranslationBackend
{
public:
    QString backendId() const override { return QStringLiteral("test.slow"); }
    QString displayName() const override { return QStringLiteral("Slow Fake"); }

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override
    {
        for (int i = 0; i < 60; ++i) {
            if (cancelFlag && cancelFlag->load()) {
                TranslationResult canceled;
                canceled.errorMessage = QStringLiteral("canceled");
                return canceled;
            }
            QThread::msleep(2);
        }
        TranslationResult r;
        r.text = QStringLiteral("译文:%1").arg(text);
        r.success = true;
        return r;
    }
};

class TestTranslation : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registryRegistersBuiltins();
    void registryCreatesBackends();
    void cacheRoundTrip();
    void cacheKeyDiffersByText();
    void cacheLruEviction();
    void serviceBackendSelection();
    void serviceAvailableBackends();
    void serviceSyncTranslateOnline(); // 需要网络，标记为可选
    void serviceCacheHit();
    void multiLangTargets();                  // 网络：多目标语言切换
    void autoSourceNonEnglishEchoBlocked();   // 网络：auto+非英文原文回显拦截
    void progressAndCancel();                 // 进度信号 + 取消
};

void TestTranslation::initTestCase()
{
    // 隔离配置目录，避免测试写坏用户真实配置
    ConfigService::setDataDirectoryForTest(QDir::tempPath() + QStringLiteral("/tst_translation_config"));
}

void TestTranslation::registryRegistersBuiltins()
{
    registerBuiltinTranslationBackends();
    ServiceRegistry *registry = ServiceRegistry::instance();

    const QStringList backends = registry->availableBackends();
    QVERIFY(backends.contains(QStringLiteral("translation.ollama")));
    QVERIFY(backends.contains(QStringLiteral("translation.online")));
    QVERIFY(backends.contains(QStringLiteral("translation.network_model")));
}

void TestTranslation::registryCreatesBackends()
{
    ServiceRegistry *registry = ServiceRegistry::instance();
    auto ollama = registry->createBackend(QStringLiteral("translation.ollama"));
    QVERIFY(ollama != nullptr);
    QCOMPARE(ollama->backendId(), QStringLiteral("translation.ollama"));

    auto online = registry->createBackend(QStringLiteral("translation.online"));
    QVERIFY(online != nullptr);
    QCOMPARE(online->backendId(), QStringLiteral("translation.online"));

    auto network = registry->createBackend(QStringLiteral("translation.network_model"));
    QVERIFY(network != nullptr);

    // 未知后端返回空
    QVERIFY(registry->createBackend(QStringLiteral("unknown.backend")) == nullptr);
}

void TestTranslation::cacheRoundTrip()
{
    TranslationCache cache;
    TranslationOptions options;
    options.strictOutput = true;
    options.extra.insert(QStringLiteral("model"), QStringLiteral("test-model"));

    TranslationResult result;
    result.text = QStringLiteral("译文");
    result.success = true;

    const QString key = cache.key(QStringLiteral("hello"), options);
    cache.put(key, result);

    const TranslationResult got = cache.get(key);
    QVERIFY(got.success);
    QCOMPARE(got.text, QStringLiteral("译文"));
    QVERIFY(got.fromCache);
}

void TestTranslation::cacheKeyDiffersByText()
{
    TranslationCache cache;
    TranslationOptions options;
    const QString k1 = cache.key(QStringLiteral("hello"), options);
    const QString k2 = cache.key(QStringLiteral("world"), options);
    QVERIFY(k1 != k2);
    QCOMPARE(k1.size(), 64); // sha256 hex
}

void TestTranslation::cacheLruEviction()
{
    TranslationCache cache;
    // 仅验证 L1 内存 LRU：关闭磁盘缓存，避免被淘汰条目从 L2 磁盘命中
    cache.setDiskCacheEnabled(false);
    TranslationOptions options;

    // 写入超过上限的条目，验证最旧被淘汰
    for (int i = 0; i < 6000; ++i) {
        TranslationResult r;
        r.text = QStringLiteral("r%1").arg(i);
        r.success = true;
        cache.put(cache.key(QString::number(i), options), r);
    }
    QVERIFY(cache.size() <= 5000);

    // 最旧的（0）应被淘汰，较新的应仍在
    QVERIFY(!cache.get(cache.key(QStringLiteral("0"), options)).success);
    QVERIFY(cache.get(cache.key(QStringLiteral("5999"), options)).success);
}

void TestTranslation::serviceBackendSelection()
{
    TranslationService service;
    registerBuiltinTranslationBackends();

    service.setBackend(QStringLiteral("translation.online"));
    QCOMPARE(service.backend(), QStringLiteral("translation.online"));
}

void TestTranslation::serviceAvailableBackends()
{
    TranslationService service;
    registerBuiltinTranslationBackends();
    QVERIFY(service.availableBackends().size() >= 3);
}

void TestTranslation::serviceSyncTranslateOnline()
{
    // 在线翻译需要网络；无网时跳过（不失败）
    TranslationService service;
    registerBuiltinTranslationBackends();
    service.setBackend(QStringLiteral("translation.online"));
    service.setTimeoutMs(15000);
    service.setCacheEnabled(false);

    const TranslationResult result = service.translateSync(QStringLiteral("hello world"));
    if (result.success) {
        QVERIFY(!result.text.trimmed().isEmpty());
    } else {
        QSKIP("网络不可用，跳过在线翻译断言");
    }
}

void TestTranslation::serviceCacheHit()
{
    TranslationService service;
    registerBuiltinTranslationBackends();
    service.setBackend(QStringLiteral("translation.online"));
    service.setCacheEnabled(true);
    service.setTimeoutMs(15000);

    // 第一次真实请求（可能失败/成功都接受），第二次应命中缓存
    const TranslationResult first = service.translateSync(QStringLiteral("cache test phrase"));
    if (!first.success) {
        QSKIP("网络不可用，跳过缓存命中断言");
    }
    const TranslationResult second = service.translateSync(QStringLiteral("cache test phrase"));
    QVERIFY(second.success);
    QVERIFY2(second.fromCache || second.text == first.text,
             "第二次翻译应命中缓存或与首次一致");
}

void TestTranslation::multiLangTargets()
{
    TranslationService service;
    service.setBackend(QStringLiteral("translation.online"));
    service.setCacheEnabled(false);
    service.setTimeoutMs(20000);
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("sourceLang"), QStringLiteral("en"));

    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("targetLang"), QStringLiteral("zh-CN"));
    const auto zh = service.translateSync(QStringLiteral("The cat likes fish."));
    QVERIFY2(zh.success, qPrintable(zh.errorMessage));

    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("targetLang"), QStringLiteral("ja"));
    const auto ja = service.translateSync(QStringLiteral("The cat likes fish."));
    QVERIFY2(ja.success, qPrintable(ja.errorMessage));

    // 不同目标语言结果应不同（语言切换生效）
    QVERIFY(ja.text != zh.text);
}

void TestTranslation::autoSourceNonEnglishEchoBlocked()
{
    TranslationService service;
    service.setBackend(QStringLiteral("translation.online"));
    service.setCacheEnabled(false);
    service.setTimeoutMs(20000);
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("sourceLang"), QStringLiteral("auto"));
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("targetLang"), QStringLiteral("zh-CN"));

    // 日文原文 + auto → 免费云端无法识别 → 回显应被质量门拦截（失败而非输出原文）
    const auto r = service.translateSync(QStringLiteral("これはテストです。猫が好きです。"));
    QVERIFY(!r.success);
    QVERIFY(r.errorMessage.contains(QStringLiteral("疑似未翻译")));
}

void TestTranslation::progressAndCancel()
{
    registerBuiltinTranslationBackends();
    ServiceRegistry::instance()->registerBackend(
        QStringLiteral("test.slow"),
        []() { return std::shared_ptr<ITranslationBackend>(new SlowFakeBackend); },
        QStringLiteral("Slow Fake"));

    TranslationService service;
    service.setBackend(QStringLiteral("test.slow"));
    service.setCacheEnabled(false);
    service.setFallbackEnabled(false);
    service.setSmartChunkingEnabled(false);   // 每行一个块，便于中途取消

    const QStringList src{ QStringLiteral("one"), QStringLiteral("two"),
                           QStringLiteral("three"), QStringLiteral("four"),
                           QStringLiteral("five") };
    const QList<int> lines{ 0, 1, 2, 3, 4 };

    QSignalSpy startedSpy(&service, &TranslationService::translationStarted);
    QSignalSpy progressSpy(&service, &TranslationService::translationProgress);
    QSignalSpy canceledSpy(&service, &TranslationService::translationCanceled);
    QSignalSpy finishedSpy(&service, &TranslationService::batchFinished);

    service.translateLines(lines, src);

    QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() >= 1, 2000);
    QCOMPARE(startedSpy.first().at(0).toInt(), 5);
    QVERIFY(service.translationActive());

    // 等至少 2 行进度后取消
    QTRY_VERIFY_WITH_TIMEOUT(progressSpy.count() >= 2, 4000);
    service.cancelTranslation();

    QTRY_VERIFY_WITH_TIMEOUT(canceledSpy.count() >= 1, 4000);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 1, 4000);
    QVERIFY(!service.translationActive());

    // 取消前未完成全部（进度 < 5）
    const int done = progressSpy.last().at(0).toInt();
    QVERIFY(done < 5);

    // 取消后不再推进进度
    QTest::qWait(400);
    const int countAfter = progressSpy.count();
    QTest::qWait(300);
    QCOMPARE(progressSpy.count(), countAfter);
}

QTEST_GUILESS_MAIN(TestTranslation)
#include "tst_translation.moc"
