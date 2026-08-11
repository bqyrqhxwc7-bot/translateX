#include <QtTest>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>

#include "services/configservice.h"

class TestConfigService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void builtinSections();
    void defaultValues();
    void setGetRoundTrip();
    void configChangedSignal();
    void valuesFillsDefaults();
    void secretEncrypted();
    void isUserSet();
    void numberBoolNormalize();
    void scanPluginDirectory();

private:
    QTemporaryDir m_temp;
};

void TestConfigService::initTestCase()
{
    QVERIFY(m_temp.isValid());
    // 隔离到临时目录，避免污染真实用户配置；必须在首次 instance() 前调用
    ConfigService::setDataDirectoryForTest(m_temp.path());
    QVERIFY(ConfigService::instance() != nullptr);
}

void TestConfigService::builtinSections()
{
    const QStringList secs = ConfigService::instance()->sections();
    QVERIFY(secs.contains(QStringLiteral("translation")));
    QVERIFY(secs.contains(QStringLiteral("translation.ollama")));
    QVERIFY(secs.contains(QStringLiteral("translation.network_model")));

    // translation 段应有翻译选项/成本/质量配置项
    const QList<ConfigItem> items = ConfigService::instance()->items(QStringLiteral("translation"));
    QVERIFY(items.size() >= 8);
    bool hasBackend = false;
    bool hasSecret = false;
    for (const ConfigItem &item : items) {
        if (item.key == QStringLiteral("backend")) {
            hasBackend = true;
        }
        if (item.key == QStringLiteral("apiKey") && item.type == QStringLiteral("secret")) {
            hasSecret = true;
        }
    }
    QVERIFY(hasBackend);
    // network_model 段的 apiKey 应为 secret 类型
    const QList<ConfigItem> nmItems =
        ConfigService::instance()->items(QStringLiteral("translation.network_model"));
    for (const ConfigItem &item : nmItems) {
        if (item.key == QStringLiteral("apiKey")) {
            QCOMPARE(item.type, QStringLiteral("secret"));
        }
    }
    Q_UNUSED(hasSecret);
}

void TestConfigService::defaultValues()
{
    ConfigService *cfg = ConfigService::instance();
    QCOMPARE(cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).toInt(), 2);
    QVERIFY(cfg->get(QStringLiteral("translation"), QStringLiteral("smartChunking")).toBool());
    QCOMPARE(cfg->get(QStringLiteral("translation"), QStringLiteral("maxChunkChars")).toInt(), 14000);
    QCOMPARE(cfg->get(QStringLiteral("translation.ollama"), QStringLiteral("endpoint")).toString(),
             QStringLiteral("http://localhost:11434"));
    QCOMPARE(cfg->get(QStringLiteral("translation.network_model"), QStringLiteral("model")).toString(),
             QStringLiteral("deepseek-chat"));
}

void TestConfigService::setGetRoundTrip()
{
    ConfigService *cfg = ConfigService::instance();
    cfg->set(QStringLiteral("translation"), QStringLiteral("contextRadius"), 4);
    QCOMPARE(cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).toInt(), 4);
    QVERIFY(cfg->isUserSet(QStringLiteral("translation"), QStringLiteral("contextRadius")));

    // 持久化落盘验证：config.ini 中应出现该键值
    QFile ini(m_temp.filePath(QStringLiteral("config.ini")));
    QVERIFY(ini.exists());
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QByteArray content = ini.readAll();
    QVERIFY(content.contains("contextRadius"));
}

void TestConfigService::configChangedSignal()
{
    ConfigService *cfg = ConfigService::instance();
    QSignalSpy spy(cfg, &ConfigService::configChanged);
    cfg->set(QStringLiteral("translation"), QStringLiteral("strictOutput"), false);
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("translation"));
    QCOMPARE(args.at(1).toString(), QStringLiteral("strictOutput"));
    QCOMPARE(args.at(2).toBool(), false);
}

void TestConfigService::valuesFillsDefaults()
{
    const QVariantMap all = ConfigService::instance()->values(QStringLiteral("translation.ollama"));
    QVERIFY(all.contains(QStringLiteral("endpoint")));
    QVERIFY(all.contains(QStringLiteral("model")));
    QCOMPARE(all.value(QStringLiteral("endpoint")).toString(), QStringLiteral("http://localhost:11434"));
}

void TestConfigService::secretEncrypted()
{
    const QString secretValue = QStringLiteral("sk-test-secret-98765");
    ConfigService *cfg = ConfigService::instance();
    cfg->set(QStringLiteral("translation.network_model"), QStringLiteral("apiKey"), secretValue);

    // 读回应一致
    QCOMPARE(cfg->get(QStringLiteral("translation.network_model"), QStringLiteral("apiKey")).toString(),
             secretValue);

    // 落盘不得出现明文
    QFile ini(m_temp.filePath(QStringLiteral("config.ini")));
    QVERIFY(ini.open(QIODevice::ReadOnly));
    const QByteArray content = ini.readAll();
    QVERIFY(!content.contains(secretValue.toUtf8()));
}

void TestConfigService::isUserSet()
{
    ConfigService *cfg = ConfigService::instance();
    // 上面 set 过的为 true
    QVERIFY(cfg->isUserSet(QStringLiteral("translation"), QStringLiteral("contextRadius")));
    // 从未设置过的为 false
    QVERIFY(!cfg->isUserSet(QStringLiteral("translation.ollama"), QStringLiteral("endpoint")));
}

void TestConfigService::numberBoolNormalize()
{
    ConfigService *cfg = ConfigService::instance();
    // 以字符串形式写入 number/bool，读回应规范化类型
    cfg->set(QStringLiteral("translation"), QStringLiteral("contextRadius"), QVariant(QStringLiteral("3")));
    QCOMPARE(cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).toInt(), 3);
    QVERIFY(cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).typeId() == QMetaType::Double
            || cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).typeId() == QMetaType::Int);

    cfg->set(QStringLiteral("translation"), QStringLiteral("smartChunking"), QVariant(QStringLiteral("0")));
    QCOMPARE(cfg->get(QStringLiteral("translation"), QStringLiteral("smartChunking")).toBool(), false);
}

void TestConfigService::scanPluginDirectory()
{
    // 模拟第三方插件：<dir>/plugins/myplugin/config.json
    QDir pluginRoot(m_temp.path() + QStringLiteral("/plugins"));
    QVERIFY(pluginRoot.mkpath(QStringLiteral("myplugin")));
    QFile cfg(pluginRoot.filePath(QStringLiteral("myplugin/config.json")));
    QVERIFY(cfg.open(QIODevice::WriteOnly));
    cfg.write(R"json({
  "id": "test.plugin",
  "displayName": "测试插件",
  "settings": [
    { "key": "greeting", "displayName": "问候", "type": "string", "default": "hi" }
  ]
})json");
    cfg.close();

    ConfigService::instance()->scanConfigDirectory(pluginRoot.path());
    QVERIFY(ConfigService::instance()->sections().contains(QStringLiteral("test.plugin")));
    QCOMPARE(ConfigService::instance()->get(QStringLiteral("test.plugin"), QStringLiteral("greeting")).toString(),
             QStringLiteral("hi"));
}

QTEST_GUILESS_MAIN(TestConfigService)
#include "tst_configservice.moc"
