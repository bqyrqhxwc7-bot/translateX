#include <QtTest>
#include <QDir>
#include <QScopedPointer>

#include "serviceregistry.h"
#include "iservice.h"
#include "itranslationbackend.h"
#include "documentmodel.h"
#include "translationservice.h"
#include "commentservice.h"
#include "documentmanager.h"
#include "chapterservice.h"
#include "findservice.h"
#include "configservice.h"
#include "texttospeechservice.h"
#include "translationhistoryservice.h"
#include "appguard.h"

// 注册表测试（迭代5 插件化 A3）：服务注册/查询/健康度聚合/后端注册。
// 注意：ServiceRegistry 是进程内单例，本测试进程独立，不影响其他测试目标。
// 服务对象必须是类成员（initTestCase 创建、测试类析构时销毁）——
// 注册表保存裸指针，栈对象在测试函数返回后销毁会造成悬挂指针崩溃。
class tst_registry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registerAndQuery();
    void duplicateIdOverwrites();
    void nonServiceIgnored();
    void healthReportAggregates();
    void backendRegistration();

private:
    QScopedPointer<DocumentModel> m_model;
    QScopedPointer<DocumentModel> m_model2;
    QScopedPointer<TranslationService> m_translation;
    QScopedPointer<CommentService> m_comment;
    QScopedPointer<DocumentManager> m_manager;
    QScopedPointer<ChapterService> m_chapter;
    QScopedPointer<FindService> m_find;
    QScopedPointer<TextToSpeechService> m_tts;
    QScopedPointer<TranslationHistoryService> m_history;
    QScopedPointer<AppGuard> m_guard;
};

void tst_registry::initTestCase()
{
    // 必须在 ConfigService 首次 instance() 之前（TranslationService 构造时触发）
    ConfigService::setDataDirectoryForTest(QDir::tempPath() + QStringLiteral("/tst_registry_cfg"));

    ServiceRegistry *registry = ServiceRegistry::instance();

    m_model.reset(new DocumentModel);
    m_model2.reset(new DocumentModel);
    m_translation.reset(new TranslationService);
    m_comment.reset(new CommentService);
    m_manager.reset(new DocumentManager);
    m_chapter.reset(new ChapterService);
    m_find.reset(new FindService);
    m_tts.reset(new TextToSpeechService);
    m_history.reset(new TranslationHistoryService);
    m_guard.reset(new AppGuard);

    registry->registerService(m_model.data());
    registry->registerService(m_translation.data());
    registry->registerService(m_comment.data());
    registry->registerService(m_manager.data());
    registry->registerService(m_chapter.data());
    registry->registerService(m_find.data());
    registry->registerService(ConfigService::instance());
    registry->registerService(m_tts.data());
    registry->registerService(m_history.data());
    registry->registerService(m_guard.data());
}

void tst_registry::registerAndQuery()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    QVERIFY(registry->serviceById(QStringLiteral("documentModel")) == m_model.data());
    QVERIFY(registry->serviceById(QStringLiteral("translation")) == m_translation.data());
    QVERIFY(registry->serviceById(QStringLiteral("comment")) == m_comment.data());
    QVERIFY(registry->serviceById(QStringLiteral("documentManager")) == m_manager.data());
    QVERIFY(registry->serviceById(QStringLiteral("chapter")) == m_chapter.data());
    QVERIFY(registry->serviceById(QStringLiteral("find")) == m_find.data());
    QVERIFY(registry->serviceById(QStringLiteral("config")) == ConfigService::instance());
    QVERIFY(registry->serviceById(QStringLiteral("textToSpeech")) == m_tts.data());
    QVERIFY(registry->serviceById(QStringLiteral("translationHistory")) == m_history.data());
    QVERIFY(registry->serviceById(QStringLiteral("appGuard")) == m_guard.data());

    // 未注册 ID 返回 nullptr
    QVERIFY(registry->serviceById(QStringLiteral("nonexistent")) == nullptr);
}

void tst_registry::duplicateIdOverwrites()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    // 重复 ID 覆盖：查询返回最后一次注册的对象（m_model2 同为类成员，生命周期安全）
    registry->registerService(m_model2.data());
    QVERIFY(registry->serviceById(QStringLiteral("documentModel")) == m_model2.data());
}

void tst_registry::nonServiceIgnored()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    // 未实现 IService 的普通 QObject 注册被忽略（不崩溃、不注册）
    QObject plain;
    registry->registerService(&plain);
    QVERIFY(registry->serviceById(QStringLiteral("")) == nullptr);
}

void tst_registry::healthReportAggregates()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    const QVariantList report = registry->healthReport();
    QVERIFY(!report.isEmpty());

    // 每个条目含 id/displayName/version/status/message
    for (const QVariant &entryVar : report) {
        const QVariantMap entry = entryVar.toMap();
        QVERIFY(!entry.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!entry.value(QStringLiteral("displayName")).toString().isEmpty());
        QVERIFY(!entry.value(QStringLiteral("version")).toString().isEmpty());
        const QString status = entry.value(QStringLiteral("status")).toString();
        QVERIFY(status == QStringLiteral("ok") || status == QStringLiteral("warn")
                || status == QStringLiteral("error"));
    }

    // 翻译服务健康度：后端已注册（内置注册在 TranslationService 构造时完成）
    auto *translation = qobject_cast<IService *>(registry->serviceById(QStringLiteral("translation")));
    QVERIFY(translation != nullptr);
    const QVariantMap translationHealth = translation->healthCheck();
    QCOMPARE(translationHealth.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
}

void tst_registry::backendRegistration()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    // 内置后端（TranslationService 构造时注册）
    QVERIFY(registry->availableBackends().contains(QStringLiteral("translation.ollama")));
    QVERIFY(registry->availableBackends().contains(QStringLiteral("translation.online")));
    QVERIFY(registry->availableBackends().contains(QStringLiteral("translation.network_model")));

    // 创建后端实例
    const auto backend = registry->createBackend(QStringLiteral("translation.ollama"));
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendId(), QStringLiteral("translation.ollama"));

    // 未注册 ID 返回 nullptr
    QVERIFY(registry->createBackend(QStringLiteral("translation.nonexistent")) == nullptr);
}

QTEST_MAIN(tst_registry)
#include "tst_registry.moc"