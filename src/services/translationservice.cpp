#include "translationservice.h"

#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>

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

QVariantList TranslationService::extractTermCandidates(const QStringList &lines,
                                                       int minFreq, int maxCount)
{
    const auto candidates = m_glossary.extractCandidates(lines, minFreq, maxCount);
    QVariantList out;
    out.reserve(candidates.size());
    for (const auto &pair : candidates) {
        QVariantMap item;
        item.insert(QStringLiteral("word"), pair.first);
        item.insert(QStringLiteral("count"), pair.second);
        out.append(item);
    }
    return out;
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
    // 通用目标语言检测：文本是否已基本是目标语言。
    // 命中 → 跳过该行（避免无意义的目标语言→目标语言翻译：模型会回显原文，
    // 触发回显检测误报「疑似未翻译」）。支持 zh/ja/ko/en。
    int cjk = 0, hiragana = 0, katakana = 0, hangul = 0, latin = 0, nonSpace = 0;
    for (const QChar &c : text) {
        if (c.isSpace()) {
            continue;
        }
        ++nonSpace;
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)) {
            ++cjk;
        } else if (u >= 0x3040 && u <= 0x309F) {
            ++hiragana;
        } else if (u >= 0x30A0 && u <= 0x30FF) {
            ++katakana;
        } else if (u >= 0xAC00 && u <= 0xD7AF) {
            ++hangul;
        } else if ((u >= 0x41 && u <= 0x5A) || (u >= 0x61 && u <= 0x7A)) {
            ++latin;
        }
    }
    if (nonSpace <= 0) {
        return false;
    }
    const QString lang = m_targetLang;
    if (lang == QStringLiteral("zh") || lang == QStringLiteral("zh-CN")
        || lang == QStringLiteral("zh-TW")) {
        return static_cast<double>(cjk) / nonSpace >= 0.3;
    }
    if (lang == QStringLiteral("ja")) {
        const double kanaRatio = static_cast<double>(hiragana + katakana) / nonSpace;
        return kanaRatio >= 0.1 || (cjk > 0 && kanaRatio >= 0.05);
    }
    if (lang == QStringLiteral("ko")) {
        return static_cast<double>(hangul) / nonSpace >= 0.3;
    }
    if (lang == QStringLiteral("en")) {
        // 英文：拉丁字母为主且无 CJK（避免把中文/日文行误判为英文）
        return cjk == 0 && static_cast<double>(latin) / nonSpace >= 0.8;
    }
    return false;
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
        result.errorMessage = QStringLiteral(
            "疑似未翻译（译文与原文相同）：请检查源语言/目标语言设置；"
            "若语言正确，请检查后端模型名与 API 配置是否正确");
        // 回显拦截是硬性（不输出原文），但不进「质量自检复核面板」
        //（面板只收集规则自检告警，受 qualityGateEnabled 开关控制；
        //  本失败经 translationFailed 单条提示）
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

// 解析模型输出的术语译文（逐行 "术语 = 译文"/"术语：译文"/"术语 → 译文"，
// 容忍行首序号与引号；键与原术语大小写不敏感匹配）
QVariantMap TranslationService::parseTermSuggestions(const QString &text,
                                                     const QStringList &terms)
{
    QVariantMap out;
    const QStringList outputLines = text.split(QLatin1Char('\n'));
    static const QRegularExpression numPrefixRe(QStringLiteral("^\\d+[.)、]\\s*"));
    for (const QString &rawLine : outputLines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        line.remove(numPrefixRe);
        int sep = -1;
        int sepLen = 0;
        for (const QString &marker : { QStringLiteral(" = "), QStringLiteral("："),
                                       QStringLiteral(": "), QStringLiteral(" → "),
                                       QStringLiteral("=>"), QStringLiteral("=") }) {
            const int idx = line.indexOf(marker);
            if (idx > 0) {
                sep = idx;
                sepLen = marker.size();
                break;
            }
        }
        if (sep < 0) {
            continue;
        }
        const QString key = line.left(sep).trimmed();
        QString value = line.mid(sep + sepLen).trimmed();
        // 剥离成对引号（英文/中文弯引号/直角引号）
        const bool quoted = value.size() >= 2
            && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                || (value.startsWith(QChar(0x201C)) && value.endsWith(QChar(0x201D)))
                || (value.startsWith(QChar(0x300C)) && value.endsWith(QChar(0x300D))));
        if (quoted) {
            value = value.mid(1, value.size() - 2).trimmed();
        }
        if (key.isEmpty() || value.isEmpty()) {
            continue;
        }
        // 自译（模型回显 "翻译 = 翻译"）无意义，跳过
        if (key.compare(value, Qt::CaseInsensitive) == 0) {
            continue;
        }
        for (const QString &term : terms) {
            if (key.compare(term, Qt::CaseInsensitive) == 0) {
                out.insert(term, value);
                break;
            }
        }
    }
    return out;
}

bool TranslationService::termSuggestionAvailable() const
{
    // 仅网络大模型后端（配置了 apiEndpoint/apiKey）支持术语建议；
    // 本地 Ollama / 云端在线 / echo 不提供
    if (m_backendId != QStringLiteral("translation.network_model")) {
        return false;
    }
    const QVariantMap cfg = ConfigService::instance()->values(m_backendId);
    return !cfg.value(QStringLiteral("apiEndpoint")).toString().isEmpty()
        && !cfg.value(QStringLiteral("apiKey")).toString().isEmpty();
}

void TranslationService::suggestTermTranslations(
    const QStringList &terms, const QStringList &contextLines)
{
    if (terms.isEmpty()) {
        emit termSuggestionsReady({}, false, QStringLiteral("没有待建议的术语"));
        return;
    }
    // 快照参数（异步期间不读 this 成员）
    const QStringList snapshot = terms;
    const QStringList context = contextLines;
    const QString backendId = m_backendId;
    const QVariantMap config = m_backendConfig;
    const QString targetLang = m_targetLang;

    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, snapshot, targetLang]() {
        const QVariantMap result = watcher->result();
        QString err;
        if (result.isEmpty()) {
            err = QStringLiteral("术语译文建议失败：大模型未返回可用结果，请检查 API 配置与网络");
            // 目标语言与术语同语言（如英文术语 + 目标英文）→ 模型自译被过滤，提示检查设置
            if (targetLang == QStringLiteral("en")) {
                err += QStringLiteral("；当前目标语言为英文，若文档/术语为英文请检查目标语言设置");
            }
        }
        emit termSuggestionsReady(result, !result.isEmpty(), err);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run(
        [this, snapshot, context, backendId, config, targetLang]() {
            QVariantMap out;
            auto backend = ServiceRegistry::instance()->createBackend(backendId);
            if (!backend) {
                return out;
            }
            QVariantMap cfg = ConfigService::instance()->values(backendId);
            for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
                cfg.insert(it.key(), it.value());
            }
            backend->updateConfig(cfg);

            // 目标语言显示名（提示词用）
            static const QHash<QString, QString> langNames = {
                { QStringLiteral("zh"), QStringLiteral("中文") },
                { QStringLiteral("en"), QStringLiteral("英文") },
                { QStringLiteral("ja"), QStringLiteral("日文") },
                { QStringLiteral("ko"), QStringLiteral("韩文") },
                { QStringLiteral("fr"), QStringLiteral("法文") },
                { QStringLiteral("de"), QStringLiteral("德文") },
                { QStringLiteral("ru"), QStringLiteral("俄文") },
                { QStringLiteral("es"), QStringLiteral("西班牙文") },
            };
            const QString langName = langNames.value(targetLang, targetLang);

            // 为每个术语收集 1 条出现行作为上下文（最多 30 条，截断 3000 字符）
            QStringList contextPool;
            for (const QString &term : snapshot) {
                if (contextPool.size() >= 30) {
                    break;
                }
                for (const QString &line : context) {
                    if (line.contains(term, Qt::CaseInsensitive)) {
                        contextPool.append(line);
                        break;
                    }
                }
            }
            QString ctxText = contextPool.join(QLatin1Char('\n'));
            if (ctxText.size() > 3000) {
                ctxText = ctxText.left(3000);
            }

            QString prompt;
            prompt += QStringLiteral(
                          "你是技术文档术语翻译助手。请根据下面的文档上下文，为每个术语给出%1的标准译文。\n")
                          .arg(langName);
            prompt += QStringLiteral(
                "要求：只输出逐行「术语 = 译文」，不要序号、不要解释、不要其他文字；"
                "不确定的也给出最可能的译文；专有名词按其通行译法。\n");
            prompt += QStringLiteral("术语列表：\n") + snapshot.join(QLatin1Char('\n'))
                + QLatin1Char('\n');
            if (!ctxText.isEmpty()) {
                prompt += QStringLiteral("文档上下文（节选）：\n") + ctxText + QLatin1Char('\n');
            }
            prompt += QStringLiteral("输出：\n");

            TranslationOptions options;
            options.strictOutput = m_strictOutput;
            options.timeoutMs = m_timeoutMs;
            options.sourceLang = m_sourceLang;
            options.targetLang = targetLang;
            options.extra = cfg;
            const TranslationResult result = backend->translate(prompt, options, nullptr);
            if (!result.success) {
                return out;
            }
            return parseTermSuggestions(result.text, snapshot);
        }));
}

QStringList TranslationService::withContextLines(
    const QStringList &sourceLines, int lineNumber) const
{    QStringList lines;
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

    // 降级（记录主后端真实错误：模型名/网络问题应在最终错误中透出，
    // 否则降级结果被回显拦截时用户只看到「与原文相同」而不知配置错误）
    QString primaryError;
    if (!result.success && m_fallbackEnabled) {
        primaryError = result.errorMessage;
        auto fallback = fallbackBackend();
        if (fallback && fallback->backendId() != backend->backendId()) {
            result = fallback->translate(text, options, nullptr);
        }
    } else if (!result.success) {
        primaryError = result.errorMessage;
    }

    // 质量：术语校验 + 规则自检
    result = postProcess(text, result);

    // 主后端曾失败且最终仍失败（含回显拦截）→ 透出主后端真实错误
    if (!result.success && !primaryError.isEmpty()) {
        result.errorMessage = primaryError + QStringLiteral("（降级后也未成功）");
    }

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
        bool batchAnySuccess = false;
        if (!pending.isEmpty() && backend && !m_cancelRequested.load()) {
            const TranslationOptions options = buildOptions(sourceLines, pending.first());
            const auto batch = backend->translateBatch(sourceLines, pending, options, m_cancelFlag);
            for (const auto &pair : batch) {
                if (pair.second.success) {
                    batchAnySuccess = true;
                }
                chunkResults.insert(pair.first, pair.second);
            }
        }

        // 3) 逐行兜底：仅当批量「部分成功」时重试失败行（个别失败 → 单行重试合理）；
        //    批量全失败（超时/后端整体慢/配置错误）→ 不逐行重试——逐行重试会
        //    让每行再等 30-60 秒，15 行卡 7 分钟以上，用户看到「翻译卡住不动」
        for (int lineNumber : pending) {
            if (m_cancelRequested.load()) {
                break;
            }
            const QString text = sourceLines.at(lineNumber);
            const TranslationOptions options = buildOptions(sourceLines, lineNumber);

            TranslationResult result = chunkResults.value(lineNumber);
            QString primaryError;
            if (!result.success && batchAnySuccess) {
                if (backend) {
                    result = backend->translate(text, options, m_cancelFlag);
                }
                if (!result.success && m_fallbackEnabled) {
                    primaryError = result.errorMessage;   // 记住主后端真实错误
                    auto fallback = fallbackBackend();
                    if (fallback && (!backend || fallback->backendId() != backend->backendId())) {
                        result = fallback->translate(text, options, m_cancelFlag);
                    }
                } else if (!result.success) {
                    primaryError = result.errorMessage;
                }
                chunkResults.insert(lineNumber, result);
            } else if (!result.success && !batchAnySuccess && m_fallbackEnabled) {
                // 批量全失败：只降级一次（不逐行重试主后端）
                primaryError = result.errorMessage;
                auto fallback = fallbackBackend();
                if (fallback && (!backend || fallback->backendId() != backend->backendId())) {
                    result = fallback->translate(text, options, m_cancelFlag);
                }
                chunkResults.insert(lineNumber, result);
            }

            // 质量：术语校验 + 规则自检（结果写回 chunkResults，
            // 否则第 4 步发信号时仍取到未经拦截的原始回显结果）
            result = postProcess(text, result, lineNumber);
            // 主后端曾失败且最终仍失败（含回显拦截）→ 透出主后端真实错误
            if (!result.success && !primaryError.isEmpty()) {
                result.errorMessage = primaryError + QStringLiteral("（降级后也未成功）");
            }
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

QString TranslationService::serviceId() const
{
    return QStringLiteral("translation");
}

QString TranslationService::displayName() const
{
    return QStringLiteral("翻译服务");
}

QString TranslationService::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap TranslationService::healthCheck() const
{
    if (m_backendId.isEmpty()) {
        return { { QStringLiteral("status"), QStringLiteral("error") },
                 { QStringLiteral("message"), QStringLiteral("未选择翻译后端") } };
    }
    const QStringList backends = ServiceRegistry::instance()->availableBackends();
    if (!backends.contains(m_backendId)) {
        return { { QStringLiteral("status"), QStringLiteral("error") },
                 { QStringLiteral("message"), QStringLiteral("当前后端未注册：%1").arg(m_backendId) } };
    }
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"),
               QStringLiteral("后端 %1 已注册，可用后端 %2 个").arg(m_backendId).arg(backends.size()) } };
}
