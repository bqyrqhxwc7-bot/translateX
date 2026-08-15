#include "translationbackend.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QDebug>

#include "serviceregistry.h"

namespace {

// 网络请求通用辅助：同步执行 + 超时 + 取消
struct NetworkCall {
    QNetworkAccessManager manager;
    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer cancelPollTimer;
    QNetworkReply *reply = nullptr;
    bool timedOut = false;
    bool canceled = false;

    NetworkCall(int timeoutMs)
    {
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [this]() {
            timedOut = true;
            if (reply) {
                reply->abort();
            }
            loop.quit();
        });
        cancelPollTimer.setInterval(80);
        QObject::connect(&cancelPollTimer, &QTimer::timeout, &loop, [this]() {
            if (m_cancelFlag && m_cancelFlag->load()) {
                canceled = true;
                if (reply) {
                    reply->abort();
                }
                loop.quit();
            }
        });
        if (timeoutMs > 0) {
            timeoutTimer.start(timeoutMs);
        }
        cancelPollTimer.start();
    }

    void setCancelFlag(const std::shared_ptr<std::atomic_bool> &flag) { m_cancelFlag = flag; }

    // 返回 true 表示正常完成（无超时/取消）
    bool exec()
    {
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        timeoutTimer.stop();
        cancelPollTimer.stop();
        return !timedOut && !canceled;
    }

    ~NetworkCall()
    {
        if (reply) {
            reply->deleteLater();
        }
    }

private:
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
};

QString normalizeEndpoint(const QString &endpoint)
{
    QString e = endpoint.trimmed();
    if (e.isEmpty()) {
        return e;
    }
    if (!e.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !e.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        e = QStringLiteral("http://") + e;
    }
    while (e.endsWith(QLatin1Char('/'))) {
        e.chop(1);
    }
    return e;
}

QString languageDisplayName(const QString &code)
{
    if (code == QLatin1String("zh-CN") || code == QLatin1String("zh")) {
        return QStringLiteral("简体中文");
    }
    if (code == QLatin1String("en")) {
        return QStringLiteral("英文");
    }
    if (code == QLatin1String("ja")) {
        return QStringLiteral("日文");
    }
    if (code == QLatin1String("ko")) {
        return QStringLiteral("韩文");
    }
    if (code == QLatin1String("fr")) {
        return QStringLiteral("法文");
    }
    if (code == QLatin1String("de")) {
        return QStringLiteral("德文");
    }
    if (code == QLatin1String("es")) {
        return QStringLiteral("西班牙文");
    }
    if (code == QLatin1String("ru")) {
        return QStringLiteral("俄文");
    }
    return code;
}

QString buildPrompt(
    const QString &text,
    const TranslationOptions &options,
    bool withContext,
    bool strictOutput)
{
    QString prompt;
    const QString custom = withContext ? options.customContextPrompt() : options.customPrompt();

    if (!custom.trimmed().isEmpty()) {
        if (withContext) {
            prompt = custom.arg(text, options.contextLines.join(QLatin1Char('\n')));
        } else {
            prompt = custom.arg(text);
        }
    } else if (withContext) {
        const QString targetName = languageDisplayName(options.targetLang);
        prompt = QStringLiteral(
                     "你是专业翻译。请结合上下文，将标记为[目标]的这一行翻译成%1。要求：\n"
                     "1. 原文可能是任何语言（英文、拉丁文、法文等），无论何种语言都必须完整翻译成%1。\n"
                     "2. 只输出目标行的%1译文，不要解释，不要附加说明，不要重复或保留原文。\n"
                     "3. 参考上下文统一代词、时态、语气和术语。\n"
                     "4. 如果上下文不足，就按目标行原意自然翻译。\n"
                     "上下文：\n%2\n目标行：\n%3")
                     .arg(targetName)
                     .arg(options.contextLines.join(QLatin1Char('\n')))
                     .arg(text);
    } else if (strictOutput) {
        prompt = QStringLiteral(
                     "你是专业翻译。请将下面的原文翻译成%1。原文可能是任何语言（英文、拉丁文、法文等），"
                     "无论何种语言都必须完整翻译成%1。只输出译文，不要解释，不要附加说明，不要重复或保留原文。\n原文：\n%2")
                     .arg(languageDisplayName(options.targetLang), text);
    } else {
        prompt = QStringLiteral("你是专业翻译。请将下面的原文翻译成%1，无论原文是何种语言（含拉丁文等古典语言）都必须完整翻译：\n%2")
                     .arg(languageDisplayName(options.targetLang), text);
    }

    // 质量：追加术语约束
    const QString glossaryConstraint = options.extra.value(QStringLiteral("glossaryConstraint")).toString();
    if (!glossaryConstraint.trimmed().isEmpty()) {
        prompt = glossaryConstraint + QLatin1Char('\n') + prompt;
    }
    return prompt;
}

QString stripTags(const QString &text)
{
    QString t = text.trimmed();
    t.remove(QStringLiteral("<translation>"));
    t.remove(QStringLiteral("</translation>"));
    return t.trimmed();
}

} // namespace

// ==================== OllamaBackend ====================

OllamaBackend::OllamaBackend(QObject *parent)
    : ITranslationBackend(parent)
{
}

TranslationResult OllamaBackend::generate(
    const QString &prompt,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag) const
{
    TranslationResult result;
    const QString endpoint = normalizeEndpoint(options.apiEndpoint());
    if (endpoint.isEmpty()) {
        result.errorMessage = QStringLiteral("未配置 Ollama 端点。");
        return result;
    }

    const QString model = options.model().isEmpty()
        ? QStringLiteral("qwen3:14b-q4_K_M")
        : options.model();

    QUrl url(endpoint + QStringLiteral("/api/generate"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Translex/1.0"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("stream"), true);
    payload.insert(QStringLiteral("think"), !options.disableThinking());
    payload.insert(QStringLiteral("prompt"), prompt);
    QJsonObject opts;
    opts.insert(QStringLiteral("temperature"), options.temperature);
    payload.insert(QStringLiteral("options"), opts);

    NetworkCall call(options.timeoutMs > 0 ? options.timeoutMs : 0);
    call.setCancelFlag(cancelFlag);
    QElapsedTimer timer;
    timer.start();
    call.reply = call.manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QByteArray streamingBuffer;
    QString responseText;
    bool receivedAnyChunk = false;

    auto consumeStreaming = [&](bool flushRemaining) {
        while (true) {
            const int nl = streamingBuffer.indexOf('\n');
            if (nl < 0) {
                break;
            }
            const QByteArray chunk = streamingBuffer.left(nl);
            streamingBuffer.remove(0, nl + 1);
            const QJsonDocument doc = QJsonDocument::fromJson(chunk.trimmed());
            if (doc.isObject()) {
                QString chunkText = doc.object().value(QStringLiteral("response")).toString();
                if (chunkText.isEmpty()) {
                    chunkText = doc.object().value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString();
                }
                if (!chunkText.isEmpty()) {
                    responseText.append(chunkText);
                    receivedAnyChunk = true;
                }
            }
        }
        if (flushRemaining && !streamingBuffer.trimmed().isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(streamingBuffer.trimmed());
            if (doc.isObject()) {
                QString chunkText = doc.object().value(QStringLiteral("response")).toString();
                if (chunkText.isEmpty()) {
                    chunkText = doc.object().value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString();
                }
                if (!chunkText.isEmpty()) {
                    responseText.append(chunkText);
                    receivedAnyChunk = true;
                }
            }
            streamingBuffer.clear();
        }
    };

    QObject::connect(call.reply, &QNetworkReply::readyRead, &call.loop, [&]() {
        streamingBuffer.append(call.reply->readAll());
        consumeStreaming(false);
    });

    const bool ok = call.exec();
    result.elapsedMs = timer.elapsed();

    if (call.canceled || (cancelFlag && cancelFlag->load())) {
        result.errorMessage = QStringLiteral("翻译已取消。");
        return result;
    }
    if (call.timedOut) {
        result.errorMessage = QStringLiteral("本地 Ollama 翻译超时。");
        return result;
    }
    if (!ok || call.reply->error() != QNetworkReply::NoError) {
        result.errorMessage = QStringLiteral("无法连接本地 Ollama：%1").arg(call.reply->errorString());
        // 尝试提取 Ollama 错误详情（{"error":"..."}）
        const QJsonDocument errDoc = QJsonDocument::fromJson(call.reply->readAll());
        if (errDoc.isObject()) {
            const QString msg = errDoc.object().value(QStringLiteral("error")).toString();
            if (!msg.isEmpty()) {
                result.errorMessage = QStringLiteral("无法连接本地 Ollama：%1").arg(msg);
            }
        }
        return result;
    }

    streamingBuffer.append(call.reply->readAll());
    consumeStreaming(true);

    if (!receivedAnyChunk) {
        const QJsonDocument doc = QJsonDocument::fromJson(streamingBuffer);
        if (doc.isObject()) {
            responseText = doc.object().value(QStringLiteral("response")).toString();
        }
    }

    result.text = responseText.trimmed();
    result.success = !result.text.isEmpty();
    if (!result.success) {
        result.errorMessage = QStringLiteral("本地 Ollama 返回了空响应。");
    }
    return result;
}

TranslationResult OllamaBackend::translate(
    const QString &text,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    TranslationResult result;
    if (text.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("空文本。");
        return result;
    }

    const bool withContext = !options.contextLines.isEmpty();
    const QString prompt = buildPrompt(text, options, withContext, options.strictOutput);
    result = generate(prompt, options, cancelFlag);

    if (result.success) {
        result.text = stripTags(result.text);
        if (result.text.isEmpty()) {
            result.success = false;
            result.errorMessage = QStringLiteral("模型未返回可用译文。");
        }
    }
    return result;
}

void OllamaBackend::updateConfig(const QVariantMap &config)
{
    if (config.contains(QStringLiteral("apiEndpoint"))) {
        m_endpoint = config.value(QStringLiteral("apiEndpoint")).toString();
    }
    if (config.contains(QStringLiteral("model"))) {
        m_model = config.value(QStringLiteral("model")).toString();
    }
}

QString OllamaBackend::healthCheck() const
{
    return QString(); // 由 TranslationService 在需要时调用 fetchModels 判断
}

QStringList OllamaBackend::fetchModels(QString *errorMessage) const
{
    QStringList models;
    const QString endpoint = normalizeEndpoint(m_endpoint);
    if (endpoint.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置 Ollama 端点。");
        }
        return models;
    }

    QUrl url(endpoint + QStringLiteral("/api/tags"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Translex/1.0"));

    NetworkCall call(5000);
    call.reply = call.manager.get(request);
    if (!call.exec() || call.reply->error() != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = call.reply->errorString();
        }
        return models;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(call.reply->readAll());
    if (doc.isObject()) {
        const QJsonArray modelList = doc.object().value(QStringLiteral("models")).toArray();
        for (const QJsonValue &v : modelList) {
            models.append(v.toObject().value(QStringLiteral("name")).toString());
        }
    }
    return models;
}

// ==================== OnlineBackend ====================

OnlineBackend::OnlineBackend(QObject *parent)
    : ITranslationBackend(parent)
{
}

TranslationResult OnlineBackend::translate(
    const QString &text,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    TranslationResult result;
    if (text.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("空文本。");
        return result;
    }

    QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), text);
    // MyMemory 不支持 auto 源语言；按选项指定（默认 en → zh-CN）
    QString sourceLang = options.sourceLang;
    if (sourceLang.isEmpty() || sourceLang == QStringLiteral("auto")) {
        // MyMemory 不支持 auto 源语言，回退英文
        sourceLang = QStringLiteral("en");
    }
    QString targetLang = options.targetLang.isEmpty() ? QStringLiteral("zh-CN") : options.targetLang;
    query.addQueryItem(QStringLiteral("langpair"), QStringLiteral("%1|%2").arg(sourceLang, targetLang));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Translex/1.0"));
    request.setRawHeader("Accept", "application/json");

    const int timeoutMs = options.timeoutMs > 0 ? qMin(options.timeoutMs, 30000) : 30000;
    NetworkCall call(timeoutMs);
    call.setCancelFlag(cancelFlag);
    QElapsedTimer timer;
    timer.start();
    call.reply = call.manager.get(request);

    const bool ok = call.exec();
    result.elapsedMs = timer.elapsed();

    if (call.canceled || (cancelFlag && cancelFlag->load())) {
        result.errorMessage = QStringLiteral("翻译已取消。");
        return result;
    }
    if (call.timedOut) {
        result.errorMessage = QStringLiteral("翻译请求超时。请稍后重试。");
        return result;
    }
    if (!ok || call.reply->error() != QNetworkReply::NoError) {
        result.errorMessage = call.reply->errorString();
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(call.reply->readAll());
    if (!doc.isObject()) {
        result.errorMessage = QStringLiteral("翻译服务返回了无效数据。");
        return result;
    }

    QString translated = doc.object().value(QStringLiteral("responseData")).toObject()
                             .value(QStringLiteral("translatedText")).toString().trimmed();
    if (options.strictOutput) {
        translated = stripTags(translated);
    }

    result.text = translated;
    result.success = !translated.isEmpty();
    if (!result.success) {
        result.errorMessage = QStringLiteral("翻译结果为空。");
    }
    return result;
}

// ==================== NetworkModelBackend ====================

NetworkModelBackend::NetworkModelBackend(QObject *parent)
    : ITranslationBackend(parent)
{
}

TranslationResult NetworkModelBackend::requestChat(
    const QString &prompt,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag) const
{
    TranslationResult result;
    QString endpoint = options.apiEndpoint();
    if (endpoint.isEmpty()) {
        result.errorMessage = QStringLiteral("未配置网络大模型 API 地址。");
        return result;
    }
    if (!endpoint.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !endpoint.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        endpoint = QStringLiteral("https://") + endpoint;
    }

    QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty()) {
        result.errorMessage = QStringLiteral("网络大模型 API 地址无效。");
        return result;
    }
    // 规范化：endpoint 视为 base URL，追加 OpenAI 兼容的 /chat/completions。
    // 兼容 https://host、https://host/、https://host/v1 等常见写法；
    // 已含完整 /chat/completions 路径则不重复追加。
    QString path = url.path();
    if (!path.endsWith(QStringLiteral("/chat/completions"))) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        url.setPath(path + QStringLiteral("chat/completions"));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Translex/1.0"));
    if (!options.apiKey().isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(options.apiKey()).toUtf8());
    }

    QJsonObject payload;
    // 模型名 trim：配置里可能带尾随空白（如粘贴的制表符），会导致 API 拒绝
    const QString model = options.model().trimmed();
    payload.insert(QStringLiteral("model"), model.isEmpty()
        ? QStringLiteral("deepseek-chat")
        : model);
    payload.insert(QStringLiteral("temperature"), options.temperature);
    payload.insert(QStringLiteral("stream"), false);
    // 翻译任务不需要推理：对 DeepSeek 关闭 thinking mode（默认开启且 effort=high，
    // 会对每行/批量逐条做长链思考，导致响应极慢甚至超时）。仅对 DeepSeek 生效，
    // 避免其他 OpenAI 兼容服务拒绝未知字段。
    if (url.host().contains(QStringLiteral("deepseek"), Qt::CaseInsensitive)) {
        payload.insert(QStringLiteral("thinking"), QJsonObject{
            { QStringLiteral("type"), QStringLiteral("disabled") } });
    }

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMessage.insert(QStringLiteral("content"), QStringLiteral("你是专业翻译，请输出高质量译文。"));
    messages.append(systemMessage);
    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), prompt);
    messages.append(userMessage);
    payload.insert(QStringLiteral("messages"), messages);

    const int timeoutMs = options.timeoutMs > 0 ? qMin(options.timeoutMs, 90000) : 90000;
    NetworkCall call(timeoutMs);
    call.setCancelFlag(cancelFlag);
    QElapsedTimer timer;
    timer.start();
    call.reply = call.manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    const bool ok = call.exec();
    result.elapsedMs = timer.elapsed();

    if (call.canceled || (cancelFlag && cancelFlag->load())) {
        result.errorMessage = QStringLiteral("翻译已取消。");
        qWarning() << "[network_model] canceled after" << result.elapsedMs << "ms";
        return result;
    }
    if (call.timedOut) {
        result.errorMessage = QStringLiteral("翻译请求超时。请稍后重试。");
        qWarning() << "[network_model] timeout after" << result.elapsedMs << "ms";
        return result;
    }
    if (!ok || call.reply->error() != QNetworkReply::NoError) {
        result.errorMessage = QStringLiteral("网络大模型请求失败：%1").arg(call.reply->errorString());
        qWarning() << "[network_model] request error:" << call.reply->errorString();
        // 尝试提取服务端具体错误（OpenAI 兼容：{"error":{"message":...}} 或 {"error":"..."}）
        const QJsonDocument errDoc = QJsonDocument::fromJson(call.reply->readAll());
        if (errDoc.isObject()) {
            QString msg;
            const QJsonValue errVal = errDoc.object().value(QStringLiteral("error"));
            if (errVal.isObject()) {
                msg = errVal.toObject().value(QStringLiteral("message")).toString();
            } else if (errVal.isString()) {
                msg = errVal.toString();
            }
            if (!msg.isEmpty()) {
                result.errorMessage = QStringLiteral("网络大模型请求失败：%1").arg(msg);
                qWarning() << "[network_model] server error:" << msg;
            }
        }
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(call.reply->readAll());
    if (!doc.isObject()) {
        result.errorMessage = QStringLiteral("网络大模型返回了无效数据。");
        return result;
    }

    QString content = doc.object().value(QStringLiteral("choices")).toArray()
                          .first().toObject().value(QStringLiteral("message")).toObject()
                          .value(QStringLiteral("content")).toString().trimmed();
    result.text = content;
    result.success = !content.isEmpty();
    if (!result.success) {
        result.errorMessage = QStringLiteral("网络大模型返回了空结果。");
    }
    return result;
}

TranslationResult NetworkModelBackend::translate(
    const QString &text,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    const bool withContext = !options.contextLines.isEmpty();
    const QString prompt = buildPrompt(text, options, withContext, options.strictOutput);
    TranslationResult result = requestChat(prompt, options, cancelFlag);
    if (result.success && options.strictOutput) {
        result.text = stripTags(result.text);
    }
    // 诊断：记录模型实际返回（便于排查回显/未翻译/翻译异常）
    qInfo() << "[network_model] input:" << text.left(80)
            << "| output:" << result.text.left(80);
    return result;
}

QList<QPair<int, TranslationResult>> NetworkModelBackend::translateBatch(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const TranslationOptions &options,
    const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    // 单行直接走默认路径
    if (targetLines.size() <= 1) {
        return ITranslationBackend::translateBatch(sourceLines, targetLines, options, cancelFlag);
    }

    // 真正的批量：一次请求翻译多行（模型按序输出 JSON 数组），
    // 大幅减少请求数与等待时间（原来逐行串行，每行一次 HTTP 请求）
    QStringList items;
    for (int i = 0; i < targetLines.size(); ++i) {
        const int ln = targetLines.at(i);
        const QString text = (ln >= 0 && ln < sourceLines.size()) ? sourceLines.at(ln) : QString();
        items.append(QStringLiteral("%1. %2").arg(i + 1).arg(text));
    }
    const QString targetName = languageDisplayName(options.targetLang);
    const QString batchPrompt = QStringLiteral(
        "你是专业翻译。将下面 %1 行原文逐行分别翻译成%2（每行可能是任何语言，含拉丁文等古典语言，都必须完整翻译成%2）。"
        "严格按顺序只输出一个 JSON 数组：第 i 项是第 i 行的译文。不要输出任何其他文字、不要 Markdown 代码块、不要解释。\n行列表：\n%3")
        .arg(targetLines.size())
        .arg(targetName, items.join(QLatin1Char('\n')));

    const TranslationResult resp = requestChat(batchPrompt, options, cancelFlag);
    if (!resp.success) {
        // 批量请求失败 → 逐行兜底（translateBatchSync 后续仍会逐行重试）
        return ITranslationBackend::translateBatch(sourceLines, targetLines, options, cancelFlag);
    }

    // 解析 JSON 数组（容忍 Markdown 代码块包裹）
    QString raw = resp.text.trimmed();
    if (raw.startsWith(QLatin1String("```"))) {
        const int firstNl = raw.indexOf(QLatin1Char('\n'));
        raw = raw.mid(firstNl >= 0 ? firstNl + 1 : 3);
        if (raw.endsWith(QLatin1String("```"))) {
            raw.chop(3);
        }
        raw = raw.trimmed();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    const QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    QList<QPair<int, TranslationResult>> results;
    for (int i = 0; i < targetLines.size(); ++i) {
        const int ln = targetLines.at(i);
        TranslationResult r;
        if (i < arr.size() && arr.at(i).isString()) {
            r.text = arr.at(i).toString().trimmed();
            r.success = !r.text.isEmpty();
            if (!r.success) {
                r.errorMessage = QStringLiteral("批量返回第 %1 项为空。").arg(i + 1);
            }
        } else {
            r.errorMessage = QStringLiteral("批量响应解析失败（第 %1 项）。").arg(i + 1);
        }
        results.append(qMakePair(ln, r));
    }
    qInfo() << "[network_model] batch:" << targetLines.size() << "lines, parsed:" << arr.size();
    return results;
}

// ==================== 注册 ====================

void registerBuiltinTranslationBackends()
{
    ServiceRegistry *registry = ServiceRegistry::instance();

    registry->registerBackend(
        QStringLiteral("translation.ollama"),
        []() { return std::shared_ptr<ITranslationBackend>(new OllamaBackend); },
        QStringLiteral("本地 Ollama"));

    registry->registerBackend(
        QStringLiteral("translation.online"),
        []() { return std::shared_ptr<ITranslationBackend>(new OnlineBackend); },
        QStringLiteral("云端翻译服务"));

    registry->registerBackend(
        QStringLiteral("translation.network_model"),
        []() { return std::shared_ptr<ITranslationBackend>(new NetworkModelBackend); },
        QStringLiteral("网络大模型 API"));
}
