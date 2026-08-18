#include "texttospeechservice.h"

#include "configservice.h"

#ifdef TRANSLEX_HAS_TTS
#  include <QTextToSpeech>
#endif

TextToSpeechService::TextToSpeechService(QObject *parent)
    : QObject(parent)
{
    m_rate = ConfigService::instance()
                 ->get(QStringLiteral("textToSpeech"), QStringLiteral("rate"))
                 .toDouble();

#ifdef TRANSLEX_HAS_TTS
    const QStringList engines = QTextToSpeech::availableEngines();
    if (!engines.isEmpty()) {
        m_tts = new QTextToSpeech(engines.first(), this);
        m_available = true;
        m_tts->setRate(m_rate);
        connect(m_tts, &QTextToSpeech::stateChanged, this,
                [this](QTextToSpeech::State state) { onEngineStateChanged(int(state)); });
    }
#endif
}

TextToSpeechService::~TextToSpeechService() = default;

QString TextToSpeechService::serviceId() const
{
    return QStringLiteral("textToSpeech");
}

QString TextToSpeechService::displayName() const
{
    return QStringLiteral("朗读服务");
}

QString TextToSpeechService::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap TextToSpeechService::healthCheck() const
{
    if (!m_available) {
        return { { QStringLiteral("status"), QStringLiteral("warn") },
                 { QStringLiteral("message"), QStringLiteral("无 TTS 引擎（功能降级）") } };
    }
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("引擎可用") } };
}

bool TextToSpeechService::available() const
{
    return m_available;
}

bool TextToSpeechService::speaking() const
{
    return m_current >= 0;
}

double TextToSpeechService::rate() const
{
    return m_rate;
}

void TextToSpeechService::setRate(double rate)
{
    m_rate = qBound(0.5, rate, 2.0);
#ifdef TRANSLEX_HAS_TTS
    if (m_tts) {
        m_tts->setRate(m_rate);
    }
#endif
    ConfigService::instance()->set(QStringLiteral("textToSpeech"), QStringLiteral("rate"),
                                   m_rate);
}

bool TextToSpeechService::speakText(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return true;
    }
    if (!m_available) {
        emit unavailable();
        return false;
    }
    m_queue.clear();
    m_queue.append({ -1, text });
    m_current = -1;
    playNext();
    return true;
}

bool TextToSpeechService::speakLines(const QVariantList &items)
{
    if (items.isEmpty()) {
        return true;
    }
    if (!m_available) {
        emit unavailable();
        return false;
    }
    m_queue.clear();
    for (const QVariant &v : items) {
        const QVariantMap m = v.toMap();
        QueueItem item;
        item.line = m.value(QStringLiteral("line")).toInt();
        item.text = m.value(QStringLiteral("text")).toString();
        if (!item.text.trimmed().isEmpty()) {
            m_queue.append(item);
        }
    }
    if (m_queue.isEmpty()) {
        return true;
    }
    m_current = -1;
    playNext();
    return true;
}

void TextToSpeechService::pause()
{
#ifdef TRANSLEX_HAS_TTS
    if (m_tts && m_current >= 0) {
        m_tts->pause();
        m_paused = true;
    }
#endif
}

void TextToSpeechService::resume()
{
#ifdef TRANSLEX_HAS_TTS
    if (m_tts && m_paused) {
        m_tts->resume();
        m_paused = false;
    }
#endif
}

void TextToSpeechService::stop()
{
#ifdef TRANSLEX_HAS_TTS
    if (m_tts) {
        m_tts->stop();
    }
#endif
    m_queue.clear();
    m_paused = false;
    if (m_current >= 0) {
        m_current = -1;
        emit stateChanged();
    }
}

void TextToSpeechService::playNext()
{
#ifdef TRANSLEX_HAS_TTS
    if (!m_tts || m_queue.isEmpty()) {
        return;
    }
    if (m_current + 1 >= m_queue.size()) {
        // 队列播完
        m_queue.clear();
        m_current = -1;
        emit stateChanged();
        emit finished();
        return;
    }
    ++m_current;
    const QueueItem &item = m_queue.at(m_current);
    emit lineStarted(item.line);
    m_tts->say(item.text);
    emit stateChanged();
#endif
}

void TextToSpeechService::onEngineStateChanged(int state)
{
#ifdef TRANSLEX_HAS_TTS
    if (state == int(QTextToSpeech::State::Ready)) {
        // 当前条目播完 → 播下一条
        playNext();
    }
#endif
}