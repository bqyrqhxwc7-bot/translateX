#include "translationservice.h"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "serviceregistry.h"
#include "translationbackend.h"
#include "translationcache.h"
#include "configservice.h"

TranslationService::TranslationService(QObject *parent)
    : QObject(parent)
    , m_cache(std::make_shared<TranslationCache>())
{
    // 确保内置后端已注册
    registerBuiltinTranslationBackends();
    m_backendId = QStringLiteral("translation.online");

    // 从 ConfigService 恢复持久化配置（VSCode-like：配置统一由 ConfigService 管理）
    ConfigService *cfg = ConfigService::instance();
    const QVariant backendVal = cfg->get(QStringLiteral("translation"), QStringLiteral("backend"));
    if (backendVal.isValid() && !backendVal.toString().isEmpty()) {
        m_backendId = backendVal.toString();
    }
    m_contextRadius = qMax(0, cfg->get(QStringLiteral("translation"), QStringLiteral("contextRadius")).toInt());
    m_strictOutput = cfg->get(QStringLiteral("translation"), QStringLiteral("strictOutput")).toBool();
    m_cacheEnabled = cfg->get(QStringLiteral("translation"), QStringLiteral("cacheEnabled")).toBool();
    m_fallbackEnabled = cfg->get(QStringLiteral("translation"), QStringLiteral("fallbackEnabled")).toBool();
    m_smartChunking = cfg->get(QStringLiteral("translation"), QStringLiteral("smartChunking")).toBool();
    m_sentenceAwareChunking = cfg->get(QStringLiteral("translation"), QStringLiteral("sentenceAwareChunking")).toBool();
    m_maxChunkChars = qMax(256, cfg->get(QStringLiteral("translation"), QStringLiteral("maxChunkChars")).toInt());
    m_qualityGateEnabled = cfg->get(QStringLiteral("translation"), QStringLiteral("qualityGateEnabled")).toBool();

    // 语言与自定义提示词（持久化）
    const QString srcLang = cfg->get(QStringLiteral("translation"), QStringLiteral("sourceLang")).toString();
    if (!srcLang.isEmpty()) {
        m_sourceLang = srcLang;
    }
    const QString tgtLang = cfg->get(QStringLiteral("translation"), QStringLiteral("targetLang")).toString();
    if (!tgtLang.isEmpty()) {
        m_targetLang = tgtLang;
    }
    m_enableCustomPrompt = cfg->get(QStringLiteral("translation"), QStringLiteral("enableCustomPrompt")).toBool();
    m_customPrompt = cfg->get(QStringLiteral("translation"), QStringLiteral("customPrompt")).toString();
    m_customContextPrompt = cfg->get(QStringLiteral("translation"), QStringLiteral("customContextPrompt")).toString();

    // 术语表恢复（JSON 字符串存储）
    const QString glossaryJson = cfg->get(QStringLiteral("translation"), QStringLiteral("glossary")).toString();
    if (!glossaryJson.isEmpty()) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(glossaryJson.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            QVariantMap map;
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                map.insert(it.key(), it.value().toString());
            }
            m_glossary.loadFromMap(map);
        }
    }

    // 监听配置变化（仅 translation section；其余 section 由各后端/服务自己处理）
    connect(cfg, &ConfigService::configChanged, this,
            [this](const QString &section, const QString &key, const QVariant &value) {
        if (section != QStringLiteral("translation")) {
            return;
        }
        if (key == QStringLiteral("backend")) {
            const QString id = value.toString();
            if (!id.isEmpty() && id != m_backendId) {
                m_backendId = id;
                emit backendChanged(m_backendId);
            }
        } else if (key == QStringLiteral("contextRadius")) {
            m_contextRadius = qMax(0, value.toInt());
        } else if (key == QStringLiteral("strictOutput")) {
            m_strictOutput = value.toBool();
        } else if (key == QStringLiteral("cacheEnabled")) {
            m_cacheEnabled = value.toBool();
            if (!m_cacheEnabled && m_cache) {
                m_cache->clearMemory();
            }
        } else if (key == QStringLiteral("fallbackEnabled")) {
            m_fallbackEnabled = value.toBool();
        } else if (key == QStringLiteral("smartChunking")) {
            m_smartChunking = value.toBool();
        } else if (key == QStringLiteral("maxChunkChars")) {
            m_maxChunkChars = qMax(256, value.toInt());
        } else if (key == QStringLiteral("qualityGateEnabled")) {
            m_qualityGateEnabled = value.toBool();
        } else if (key == QStringLiteral("sourceLang")) {
            if (!value.toString().isEmpty()) {
                m_sourceLang = value.toString();
            }
        } else if (key == QStringLiteral("targetLang")) {
            if (!value.toString().isEmpty()) {
                m_targetLang = value.toString();
            }
        } else if (key == QStringLiteral("enableCustomPrompt")) {
            m_enableCustomPrompt = value.toBool();
        } else if (key == QStringLiteral("customPrompt")) {
            m_customPrompt = value.toString();
        } else if (key == QStringLiteral("customContextPrompt")) {
            m_customContextPrompt = value.toString();
        }
        // glossary 变更由 setGlossary/clearGlossary 直接处理，此处无需响应
    });
}

TranslationService::~TranslationService() = default;

void TranslationService::setBackend(const QString &backendId)
{
    if (backendId == m_backendId) {
        return;
    }
    m_backendId = backendId;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("backend"), backendId);
    emit backendChanged(m_backendId);
}

QString TranslationService::backend() const
{
    return m_backendId;
}

QStringList TranslationService::availableBackends() const
{
    return ServiceRegistry::instance()->availableBackends();
}

QString TranslationService::backendDisplayName(const QString &id) const
{
    return ServiceRegistry::instance()->backendDisplayName(id);
}

void TranslationService::setBackendConfig(const QVariantMap &config)
{
    m_backendConfig = config;
}

void TranslationService::setContextRadius(int radius)
{
    m_contextRadius = qMax(0, radius);
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("contextRadius"), m_contextRadius);
}

void TranslationService::setStrictOutput(bool strict)
{
    m_strictOutput = strict;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("strictOutput"), strict);
}

void TranslationService::setCacheEnabled(bool enabled)
{
    m_cacheEnabled = enabled;
    if (!enabled && m_cache) {
        m_cache->clearMemory();
    }
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("cacheEnabled"), enabled);
}

void TranslationService::setFallbackEnabled(bool enabled)
{
    m_fallbackEnabled = enabled;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("fallbackEnabled"), enabled);
}

void TranslationService::setTimeoutMs(int ms)
{
    m_timeoutMs = qMax(0, ms);
}

void TranslationService::setGlossary(const QVariantMap &terms)
{
    m_glossary.loadFromMap(terms);
    persistGlossary();
}

void TranslationService::clearGlossary()
{
    m_glossary.clear();
    // 写空串恢复默认（清除持久化）
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("glossary"), QString());
}

void TranslationService::persistGlossary()
{
    QJsonObject obj;
    const auto terms = m_glossary.terms();
    for (const auto &pair : terms) {
        obj.insert(pair.first, pair.second);
    }
    const QString json = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("glossary"), json);
}

QVariantMap TranslationService::glossary() const
{
    return m_glossary.toMap();
}

void TranslationService::setQualityGateEnabled(bool enabled)
{
    m_qualityGateEnabled = enabled;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("qualityGateEnabled"), enabled);
}

bool TranslationService::qualityGateEnabled() const
{
    return m_qualityGateEnabled;
}

void TranslationService::setSmartChunkingEnabled(bool enabled)
{
    m_smartChunking = enabled;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("smartChunking"), enabled);
}

void TranslationService::setMaxChunkChars(int chars)
{
    m_maxChunkChars = qMax(256, chars);
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("maxChunkChars"), m_maxChunkChars);
}

void TranslationService::setSentenceAwareChunking(bool enabled)
{
    m_sentenceAwareChunking = enabled;
    ConfigService::instance()->set(QStringLiteral("translation"), QStringLiteral("sentenceAwareChunking"), enabled);
}

bool TranslationService::endsWithSentenceBoundary(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    const ushort last = t.at(t.size() - 1).unicode();
    // 注意：中文标点必须用码点比较（QLatin1Char 对多字节字面量会截断，永不匹配）
    return last == 0x3002   // 。
        || last == 0xFF01  // ！
        || last == 0xFF1F  // ？
        || last == 0x2026  // …
        || last == 0xFF1B  // ；
        || last == u'.' || last == u'!' || last == u'?' || last == u';';
}

int TranslationService::estimateTokens(const QString &text)
{
    // 中文 1 字 ≈ 1 token；英文/数字 4 字符 ≈ 1 token
    int cjk = 0;
    int other = 0;
    for (const QChar &c : text) {
        if (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF) {
            ++cjk;
        } else if (!c.isSpace()) {
            ++other;
        }
    }
    return cjk + (other + 3) / 4;
}

bool TranslationService::isTargetLanguageText(const QString &text) const
{
    // 目标语言为中文时：CJK 字符占比 ≥ 30% 视为已是目标语言，跳过无意义的中文→中文翻译
    //（模型对中文→中文会回显原文，触发回显检测误报“疑似未翻译”）
    if (m_targetLang != QStringLiteral("zh-CN") && m_targetLang != QStringLiteral("zh")) {
        return false;
    }
    int cjk = 0;
    int nonSpace = 0;
    for (const QChar &c : text) {
        if (c.isSpace()) {
            continue;
        }
        ++nonSpace;
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)) {
            ++cjk;
        }
    }
    return nonSpace > 0 && static_cast<double>(cjk) / nonSpace >= 0.3;
}

QList<QList<int>> TranslationService::buildChunks(
    const QStringList &sourceLines, const QList<int> &targetLines) const
{
    QList<QList<int>> chunks;
    if (!m_smartChunking || targetLines.size() <= 1) {
        for (int line : targetLines) {
            chunks.append(QList<int>{ line });
        }
        return chunks;
    }

    // 按字符预算合并相邻目标行（只合并行号连续的行）
    QList<int> current;
    int currentChars = 0;
    const int budget = m_maxChunkChars;

    auto flush = [&current, &chunks]() {
        if (!current.isEmpty()) {
            chunks.append(current);
            current = QList<int>();
        }
    };

    for (int line : targetLines) {
        const QString text = line >= 0 && line < sourceLines.size() ? sourceLines.at(line) : QString();
        const int len = text.size();
        const bool contiguous = current.isEmpty() || line == current.last() + 1;
        // 分段感知：当前 chunk 末行已是完整句子（句末标点）→ 截断，
        // 避免跨句合并稀释上下文质量（m_sentenceAwareChunking 开关）
        const bool sentenceBoundary = m_sentenceAwareChunking && !current.isEmpty()
            && endsWithSentenceBoundary(sourceLines.value(current.last()));

        if (contiguous && !sentenceBoundary && currentChars + len <= budget) {
            current.append(line);
            currentChars += len;
        } else {
            flush();
            current = QList<int>{ line };
            currentChars = len;
        }
    }
    flush();
    return chunks;
}

TranslationResult TranslationService::postProcess(
    const QString &source, TranslationResult result, int lineNumber)
{
    if (!result.success) {
        return result;
    }

    // 回显检测：始终执行（不输出原文是硬性质量要求，不受质量自检开关影响）。
    // sameLanguage：仅「明确同语言」或「auto 且原文已疑似目标语言」时启用近似检测；
    // 跨语言（auto→ja 等共享汉字场景）只做完全一致拦截，防误杀真实译文。
    const bool sourceLangKnown = !m_sourceLang.isEmpty() && m_sourceLang != QStringLiteral("auto");
    const bool sameLanguage = (sourceLangKnown && m_sourceLang == m_targetLang)
                              || (!sourceLangKnown && isTargetLanguageText(source));
    if (!QualityGate::notJustEcho(source, result.text, sameLanguage)) {
        result.success = false;
        result.errorMessage = QStringLiteral("疑似未翻译（译文与原文相同），请检查后端或模型配置后重试");
        emit qualityWarning(lineNumber, result.errorMessage);
        return result;
    }

    if (!m_qualityGateEnabled) {
        return result;
    }

    // 术语校验 + 质量规则
    const QualityReport report = QualityGate::evaluate(
        source, result.text, m_glossary.size() > 0 ? &m_glossary : nullptr);

    if (!report.passed) {
        // 保留译文但不阻塞；发出质量告警（UI 可提示需人工复核）
        for (const QString &issue : report.issues) {
            emit qualityWarning(lineNumber, issue);
        }
    }
    return result;
}

std::shared_ptr<ITranslationBackend> TranslationService::currentBackend() const
{
    auto backend = ServiceRegistry::instance()->createBackend(m_backendId);
    if (backend) {
        // 后端参数：优先 ConfigService 中该后端 section（含用户配置），
        // m_backendConfig 作为运行时覆盖（兼容旧路径）
        QVariantMap cfg = ConfigService::instance()->values(m_backendId);
        for (auto it = m_backendConfig.constBegin(); it != m_backendConfig.constEnd(); ++it) {
            cfg.insert(it.key(), it.value());
        }
        backend->updateConfig(cfg);
    }
    return backend;
}

std::shared_ptr<ITranslationBackend> TranslationService::fallbackBackend() const
{
    // 降级链：网络大模型 → 云端在线（免费）
    if (m_backendId != QStringLiteral("translation.online")) {
        auto online = ServiceRegistry::instance()->createBackend(QStringLiteral("translation.online"));
        if (online) {
            online->updateConfig(m_backendConfig);
            return online;
        }
    }
    return nullptr;
}

TranslationOptions TranslationService::buildOptions(
    const QStringList &sourceLines, int lineNumber) const
{
    TranslationOptions options;
    options.contextLines = withContextLines(sourceLines, lineNumber);
    options.strictOutput = m_strictOutput;
    options.timeoutMs = m_timeoutMs;
    options.sourceLang = m_sourceLang;
    options.targetLang = m_targetLang;

    // 后端参数：合并 ConfigService 中当前后端 section 的用户配置
    //（apiEndpoint/apiKey/model 等，apiKey 为 secret 已解密），
    // m_backendConfig 仅作为运行时覆盖（兼容旧路径，优先）
    QVariantMap extra = m_backendConfig;
    if (!m_backendId.isEmpty()) {
        const QVariantMap cfg = ConfigService::instance()->values(m_backendId);
        for (auto it = cfg.constBegin(); it != cfg.constEnd(); ++it) {
            if (!extra.contains(it.key())) {
                extra.insert(it.key(), it.value());
            }
        }
    }
    options.extra = extra;

    // 自定义提示词（%1 原文，%2 上下文）
    if (m_enableCustomPrompt) {
        if (!m_customPrompt.trimmed().isEmpty()) {
            options.extra.insert(QStringLiteral("customPrompt"), m_customPrompt);
        }
        if (!m_customContextPrompt.trimmed().isEmpty()) {
            options.extra.insert(QStringLiteral("customContextPrompt"), m_customContextPrompt);
        }
    }
    // 质量：注入术语约束（后端 buildPrompt 会读取并追加到提示词）
    if (m_glossary.size() > 0) {
        options.extra.insert(QStringLiteral("glossaryConstraint"), m_glossary.buildConstraintPrompt());
    }
    return options;
}

QStringList TranslationService::withContextLines(
    const QStringList &sourceLines, int lineNumber) const
{
    QStringList lines;
    if (m_contextRadius <= 0) {
        return lines;
    }
    for (int offset = -m_contextRadius; offset <= m_contextRadius; ++offset) {
        const int line = lineNumber + offset;
        if (offset == 0) {
            continue;
        }
        if (line >= 0 && line < sourceLines.size()) {
            lines.append(sourceLines.at(line));
        }
    }
    return lines;
}

TranslationResult TranslationService::translateSync(const QString &text)
{
    QStringList single{ text };
    TranslationOptions options = buildOptions(single, 0);
    options.contextLines.clear(); // 单行无上下文

    auto backend = currentBackend();

    // 缓存查找（命中也要过质量门，防旧缓存回显原文）
    if (m_cacheEnabled && m_cache) {
        const QString cacheKey = m_cache->key(text, options);
        const TranslationResult cached = m_cache->get(cacheKey);
        if (cached.success) {
            const TranslationResult checked = postProcess(text, cached);
            if (checked.success) {
                return checked;
            }
            // 回显缓存 → 视为未命中，重新请求
        }
    }

    TranslationResult result = backend ? backend->translate(text, options, nullptr) : TranslationResult{};

    // 降级
    if (!result.success && m_fallbackEnabled) {
        auto fallback = fallbackBackend();
        if (fallback && fallback->backendId() != backend->backendId()) {
            result = fallback->translate(text, options, nullptr);
        }
    }

    // 质量：术语校验 + 规则自检
    result = postProcess(text, result);

    // 写缓存
    if (m_cacheEnabled && m_cache && result.success) {
        m_cache->put(m_cache->key(text, options), result);
    }
    if (!result.success && !result.errorMessage.isEmpty()) {
        emit translationFailed(-1, result.errorMessage);
    }
    return result;
}

QList<QPair<int, TranslationResult>> TranslationService::translateBatchSync(
    const QStringList &sourceLines, const QList<int> &targetLines)
{
    QList<QPair<int, TranslationResult>> results;
    const int total = targetLines.size();
    int done = 0;
    auto backend = currentBackend();

    // 成本：智能分块（合并连续相邻行 → 一次批量请求）
    const QList<QList<int>> chunks = buildChunks(sourceLines, targetLines);

    for (const QList<int> &chunk : chunks) {
        if (chunk.isEmpty() || m_cancelRequested.load()) {
            break;   // 取消：提前终止
        }

        // 1) 缓存过滤（命中直接复用，其余进入待处理）
        QHash<int, TranslationResult> chunkResults;
        QList<int> pending;
        for (int lineNumber : chunk) {
            if (lineNumber < 0 || lineNumber >= sourceLines.size()) {
                continue;
            }
            const QString text = sourceLines.at(lineNumber);
            const TranslationOptions options = buildOptions(sourceLines, lineNumber);
            if (m_cacheEnabled && m_cache) {
                const TranslationResult cached = m_cache->get(m_cache->key(text, options));
                if (cached.success) {
                    // 缓存命中也要过质量门（防旧缓存回显原文）
                    const TranslationResult checked = postProcess(text, cached, lineNumber);
                    if (checked.success) {
                        chunkResults.insert(lineNumber, checked);
                        continue;
                    }
                    // 回显缓存 → 视为未命中，继续请求
                }
            }
            pending.append(lineNumber);
        }

        // 2) 批量请求（后端可覆盖 translateBatch 合并多行，真正降请求数）
        if (!pending.isEmpty() && backend && !m_cancelRequested.load()) {
            const TranslationOptions options = buildOptions(sourceLines, pending.first());
            const auto batch = backend->translateBatch(sourceLines, pending, options, m_cancelFlag);
            for (const auto &pair : batch) {
                chunkResults.insert(pair.first, pair.second);
            }
        }

        // 3) 逐行兜底：批量缺失/失败 → 单行重试 + 降级链 + 质量自检
        for (int lineNumber : pending) {
            if (m_cancelRequested.load()) {
                break;
            }
            const QString text = sourceLines.at(lineNumber);
            const TranslationOptions options = buildOptions(sourceLines, lineNumber);

            TranslationResult result = chunkResults.value(lineNumber);
            if (!result.success) {
                if (backend) {
                    result = backend->translate(text, options, m_cancelFlag);
                }
                if (!result.success && m_fallbackEnabled) {
                    auto fallback = fallbackBackend();
                    if (fallback && (!backend || fallback->backendId() != backend->backendId())) {
                        result = fallback->translate(text, options, m_cancelFlag);
                    }
                }
                chunkResults.insert(lineNumber, result);
            }

            // 质量：术语校验 + 规则自检（结果写回 chunkResults，
            // 否则第 4 步发信号时仍取到未经拦截的原始回显结果）
            result = postProcess(text, result, lineNumber);
            chunkResults.insert(lineNumber, result);

            if (m_cacheEnabled && m_cache && result.success) {
                m_cache->put(m_cache->key(text, options), result);
            }
        }

        // 4) 按块内原始顺序输出 + 发信号（含进度）
        for (int lineNumber : chunk) {
            if (m_cancelRequested.load()) {
                break;
            }
            if (!chunkResults.contains(lineNumber)) {
                continue;
            }
            const TranslationResult result = chunkResults.value(lineNumber);
            results.append(qMakePair(lineNumber, result));
            emit lineTranslated(lineNumber, result.text, result.success);
            if (!result.success && !result.errorMessage.isEmpty()) {
                emit translationFailed(lineNumber, result.errorMessage);
            }
            ++done;
            emit translationProgress(done, total);
        }
    }

    return results;
}

void TranslationService::translateLines(
    const QList<int> &lineNumbers, const QStringList &sourceLines)
{
    // 重置取消状态，标记翻译进行中
    m_cancelRequested = false;
    m_cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_translating = true;
    const int total = lineNumbers.size();
    emit translationStarted(total);

    // 快照参数（避免捕获 this 成员在异步期间变化）
    const QStringList snapshot = sourceLines;
    const QList<int> lines = lineNumbers;

    auto *watcher = new QFutureWatcher<QList<QPair<int, TranslationResult>>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, lines]() {
        const auto results = watcher->result();
        int succeeded = 0;
        int failed = 0;
        for (const auto &pair : results) {
            if (pair.second.success) {
                ++succeeded;
            } else {
                ++failed;
            }
        }
        const bool wasCanceled = m_cancelRequested.load();
        m_translating = false;
        emit batchFinished(lines.size(), succeeded, failed);
        if (wasCanceled) {
            emit translationCanceled();
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([this, snapshot, lines]() {
        return translateBatchSync(snapshot, lines);
    }));
}

void TranslationService::cancelTranslation()
{
    m_cancelRequested = true;
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }
}

bool TranslationService::translationActive() const
{
    return m_translating.load();
}

void TranslationService::testBackendConnection(const QString &backendId)
{
    // 快照后端配置（异步期间不读 this 成员）
    const QVariantMap config = m_backendConfig;
    auto *watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, backendId]() {
        const auto result = watcher->result();
        emit connectionTested(backendId, result.first, result.second);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([this, backendId, config]() {
        QPair<bool, QString> out{ false, QStringLiteral("后端不可用") };
        const auto backend = backendForId(backendId);
        if (!backend) {
            out.second = QStringLiteral("后端未注册");
            return out;
        }
        // 与 currentBackend() 相同的配置合并：ConfigService 中该后端 section
        //（apiEndpoint/apiKey/model 用户配置）为基准，m_backendConfig 仅作运行时覆盖
        QVariantMap cfg = ConfigService::instance()->values(backendId);
        for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
            cfg.insert(it.key(), it.value());
        }
        backend->updateConfig(cfg);
        // 优先用后端自带 healthCheck；空实现（如 Ollama）走最小翻译探测
        const QString health = backend->healthCheck();
        if (!health.isEmpty()) {
            out.first = true;
            out.second = health;
            return out;
        }
        TranslationOptions opts;
        opts.sourceLang = m_sourceLang;
        opts.targetLang = m_targetLang;
        opts.strictOutput = true;
        opts.timeoutMs = 8000;
        // 关键：NetworkModelBackend 不重写 updateConfig（基类空实现），
        // 配置必须经 options.extra 传入（apiEndpoint/apiKey/model）
        opts.extra = cfg;
        const TranslationResult r = backend->translate(QStringLiteral("hello"), opts, nullptr);
        if (r.success) {
            out.first = true;
            out.second = QStringLiteral("连接正常");
        } else {
            out.second = r.errorMessage.isEmpty() ? QStringLiteral("连接失败") : r.errorMessage;
        }
        return out;
    }));
}

std::shared_ptr<ITranslationBackend> TranslationService::backendForId(const QString &id) const
{
    return ServiceRegistry::instance()->createBackend(id);
}
