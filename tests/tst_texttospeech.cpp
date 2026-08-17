#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "configservice.h"
#include "texttospeechservice.h"

// TTS 朗读服务测试（迭代3，设计见 docs/services/text-to-speech.md）：
//   - 降级路径：无 TTS 模块（TRANSLEX_HAS_TTS 未定义）或无引擎环境都必须 graceful
//   - 配置往返：rate 写回 ConfigService → 新实例读回一致
//   - 空队列直接成功、不发信号
class TstTextToSpeech : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // 隔离配置目录，避免污染真实用户配置
        QTemporaryDir *dir = new QTemporaryDir;
        m_dataDir = dir->path();
        delete dir;
        QVERIFY2(!m_dataDir.isEmpty(), "无法创建临时目录");
        ConfigService::setDataDirectoryForTest(m_dataDir);
        ConfigService::instance()->set(QStringLiteral("textToSpeech"), QStringLiteral("rate"), 1.2);
    }

    void rateRoundTrip()
    {
        TextToSpeechService svc;
        QCOMPARE(svc.rate(), 1.2); // 从 ConfigService 读回

        svc.setRate(1.7);
        QCOMPARE(svc.rate(), 1.7);
        QCOMPARE(ConfigService::instance()
                     ->get(QStringLiteral("textToSpeech"), QStringLiteral("rate"))
                     .toDouble(),
                 1.7);
    }

    void rateClamped()
    {
        TextToSpeechService svc;
        svc.setRate(5.0);
        QCOMPARE(svc.rate(), 2.0); // 上限 2.0
        svc.setRate(-1.0);
        QCOMPARE(svc.rate(), 0.5); // 下限 0.5
    }

    void emptySpeak()
    {
        TextToSpeechService svc;
        QVERIFY(svc.speakText(QString()));              // 空文本：直接成功
        QVERIFY(svc.speakText(QStringLiteral("  ")));   // 空白文本：直接成功
        QVERIFY(svc.speakLines({}));                    // 空列表：直接成功
        QVERIFY(!svc.speaking());
    }

    void gracefulDegrade()
    {
        // 无 TTS 模块（未编译 TRANSLEX_HAS_TTS）或运行时无引擎：speak* 返回 false、
        // available=false、发 unavailable 信号，不崩溃。有引擎环境同样通过。
        TextToSpeechService svc;
        QSignalSpy unavailableSpy(&svc, &TextToSpeechService::unavailable);

        const bool retText = svc.speakText(QStringLiteral("hello"));
        const bool retLines = svc.speakLines(QVariantList{
            QVariantMap{{QStringLiteral("line"), 3}, {QStringLiteral("text"), QStringLiteral("hi")}}});

        if (!svc.available()) {
            QVERIFY(!retText);
            QVERIFY(!retLines);
            QCOMPARE(unavailableSpy.count(), 2);
            QVERIFY(!svc.speaking());
            svc.pause();   // 无引擎时 pause/resume/stop 应安全
            svc.resume();
            svc.stop();
            QVERIFY(!svc.speaking());
        } else {
            // 有引擎环境：speak 成功且进入 speaking 状态（异步，不等完成）
            QVERIFY(retText);
            QVERIFY(retLines);
            QVERIFY(svc.speaking());
            svc.stop();
            QVERIFY(!svc.speaking());
        }
    }

private:
    QString m_dataDir;
};

QTEST_GUILESS_MAIN(TstTextToSpeech)

#include "tst_texttospeech.moc"