#pragma once

#include <QObject>
#include <QVariantList>

#ifdef TRANSLEX_HAS_TTS
class QTextToSpeech;
#endif

// TTS 朗读服务（迭代3）：独立 service，不依赖翻译/文档等其他服务。
// 只做"文本 → 语音"一件事；行文本/译文/选区的组装由 QML 层完成。
// 设计见 docs/services/text-to-speech.md：
//   - 跨平台：Qt6TextToSpeech（Windows=SAPI / macOS=AVSpeech / Linux=speech-dispatcher）
//   - 无模块（TRANSLEX_HAS_TTS 未定义）或无引擎（availableEngines 空）时优雅降级：
//     speak* 返回 false + unavailable() 信号，不崩溃
//   - 语音跟随系统默认；语速 0.5~2.0（ConfigService textToSpeech.rate 持久化）
class TextToSpeechService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool speaking READ speaking NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)

public:
    explicit TextToSpeechService(QObject *parent = nullptr);
    ~TextToSpeechService() override;

    // 单段朗读（选区/单行）：立即替换当前播放。无引擎返回 false。
    Q_INVOKABLE bool speakText(const QString &text);

    // 逐行朗读：items = [{line:int, text:str}, ...]；逐条播放，lineStarted 带行号。
    // 空列表直接返回 true。无引擎返回 false。
    Q_INVOKABLE bool speakLines(const QVariantList &items);

    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();

    Q_INVOKABLE double rate() const;
    Q_INVOKABLE void setRate(double rate);

    bool speaking() const;
    bool available() const;

signals:
    void stateChanged();               // speaking/available 变化
    void lineStarted(int lineNumber);  // speakLines 逐行开始（-1 = 纯文本项）
    void finished();                   // 队列全部播完
    void unavailable();                // 无 TTS 引擎（speak* 失败时）

private:
    void playNext();
    void onEngineStateChanged(int state);

    struct QueueItem {
        int line = -1;
        QString text;
    };

#ifdef TRANSLEX_HAS_TTS
    QTextToSpeech *m_tts = nullptr;
#endif
    QList<QueueItem> m_queue;
    int m_current = -1;      // 当前播放的队列下标（-1 = 空闲）
    double m_rate = 1.0;
    bool m_available = false;
    bool m_paused = false;
};