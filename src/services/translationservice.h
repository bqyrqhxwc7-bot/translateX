#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <atomic>
#include <memory>

#include "itranslationbackend.h"
#include "termglossary.h"
#include "qualitygate.h"
#include "iservice.h"

class TranslationCache;

// 翻译门面：QML 可调用。负责后端选择、线程池异步、缓存、降级链、
// 智能分块（成本）、术语表+质量自检（质量）。
class TranslationService : public QObject, public IService
{
    Q_OBJECT
    Q_INTERFACES(IService)

public:
    explicit TranslationService(QObject *parent = nullptr);
    ~TranslationService() override;

    // ---- IService ----
    QString serviceId() const override;
    QString displayName() const override;
    QString serviceVersion() const override;
    QVariantMap healthCheck() const override;

    // ---- 配置 ----
    Q_INVOKABLE void setBackend(const QString &backendId);
    Q_INVOKABLE QString backend() const;
    Q_INVOKABLE QStringList availableBackends() const;
    Q_INVOKABLE QString backendDisplayName(const QString &id) const;

    // 后端参数（模型/端点/key 等），随选项传给后端
    Q_INVOKABLE void setBackendConfig(const QVariantMap &config);

    // 翻译选项
    Q_INVOKABLE void setContextRadius(int radius);
    Q_INVOKABLE void setStrictOutput(bool strict);
    Q_INVOKABLE void setCacheEnabled(bool enabled);
    Q_INVOKABLE void setFallbackEnabled(bool enabled);
    Q_INVOKABLE void setTimeoutMs(int ms);

    // 质量：术语表
    Q_INVOKABLE void setGlossary(const QVariantMap &terms);
    Q_INVOKABLE void clearGlossary();
    Q_INVOKABLE QVariantMap glossary() const;
    // 术语自动提取（迭代4，2026-08-19 增强）：从行文本提取高频词候选
    //（英文/技术标识符/中文 n-gram，频率降序，过滤停用词/已有术语），
    // 返回 [{word, count}]（见 docs/services/translation-service.md §4.2.1）
    Q_INVOKABLE QVariantList extractTermCandidates(const QStringList &lines,
                                                   int minFreq = 3, int maxCount = 20);
    // 术语建议译文（异步，2026-08-19）：仅当当前后端是配置了 API 的网络大模型时可用；
    // 请求模型根据文档上下文猜测术语译文，结果经 termSuggestionsReady 信号返回
    Q_INVOKABLE void suggestTermTranslations(const QStringList &terms,
                                             const QStringList &contextLines);
    // 当前后端是否支持术语建议（网络大模型后端且已配置 apiEndpoint/apiKey）
    Q_INVOKABLE bool termSuggestionAvailable() const;
    // 解析模型输出的术语译文（逐行 "术语 = 译文" 等；键与原术语大小写不敏感匹配）；
    // 静态工具（供测试）
    static QVariantMap parseTermSuggestions(const QString &text, const QStringList &terms);
    // 质量：自检开关
    Q_INVOKABLE void setQualityGateEnabled(bool enabled);
    Q_INVOKABLE bool qualityGateEnabled() const;

    // 成本：智能分块开关 + 单块最大字符数
    Q_INVOKABLE void setSmartChunkingEnabled(bool enabled);
    Q_INVOKABLE void setMaxChunkChars(int chars);
    // 质量：句边界分块（分块以完整句子为界，避免跨句合并）
    Q_INVOKABLE void setSentenceAwareChunking(bool enabled);

    // 目标语言预检测：文本是否已基本是目标语言（当前支持中文→中文跳过），
    // 避免把已是目标语言的行拿去翻译（模型回显 → 误报“疑似未翻译”）
    Q_INVOKABLE bool isTargetLanguageText(const QString &text) const;

    // ---- 翻译入口（异步）----
    Q_INVOKABLE void translateLines(const QList<int> &lineNumbers, const QStringList &sourceLines);

    // ---- 进度 / 取消 ----
    Q_INVOKABLE void cancelTranslation();
    Q_INVOKABLE bool translationActive() const;

    // ---- 连接测试（异步；结果经 connectionTested 信号返回）----
    Q_INVOKABLE void testBackendConnection(const QString &backendId);

    // ---- 同步辅助（供测试/内部）----
    TranslationResult translateSync(const QString &text);
    QList<QPair<int, TranslationResult>> translateBatchSync(
        const QStringList &sourceLines, const QList<int> &targetLines);

signals:
    void lineTranslated(int lineNumber, const QString &text, bool success);
    void batchFinished(int total, int succeeded, int failed);
    void backendChanged(const QString &backendId);
    // 质量告警（需人工复核）
    void qualityWarning(int lineNumber, const QString &issue);
    // 翻译失败（含具体原因，UI 可展示，如后端错误/模型不存在等）
    void translationFailed(int lineNumber, const QString &errorMessage);
    // ---- 进度 / 取消 ----
    void translationStarted(int total);
    void translationProgress(int done, int total);
    void translationCanceled();
    // 连接测试结果（backendId, ok, message）
    void connectionTested(const QString &backendId, bool ok, const QString &message);
    // 术语建议译文（terms→suggestions 映射；ok=false 时 errorMessage 说明原因）
    void termSuggestionsReady(const QVariantMap &suggestions, bool ok,
                              const QString &errorMessage);

private:
    QStringList withContextLines(const QStringList &sourceLines, int lineNumber) const;
    TranslationOptions buildOptions(const QStringList &sourceLines, int lineNumber) const;

    std::shared_ptr<ITranslationBackend> currentBackend() const;
    std::shared_ptr<ITranslationBackend> fallbackBackend() const;
    std::shared_ptr<ITranslationBackend> backendForId(const QString &id) const;

    // 成本：估算文本 token 数（中英文近似：中文 1 字≈1 token，英文 4 字符≈1 token）
    static int estimateTokens(const QString &text);
    // 质量：文本是否以句末标点结尾（分段感知分块用）
    static bool endsWithSentenceBoundary(const QString &text);
    // 成本：按 token 预算合并相邻目标行为块（减少请求数）
    QList<QList<int>> buildChunks(const QStringList &sourceLines, const QList<int> &targetLines) const;

    // 质量：术语提示注入 + 结果自检（lineNumber 用于告警定位，默认 -1 表示未知）
    TranslationResult postProcess(const QString &source, TranslationResult result, int lineNumber = -1);

    // 持久化术语表到配置（JSON 字符串）
    void persistGlossary();

    QString m_backendId;
    QVariantMap m_backendConfig;
    int m_contextRadius = 2;
    bool m_strictOutput = true;
    QString m_sourceLang = QStringLiteral("en");
    QString m_targetLang = QStringLiteral("zh-CN");
    bool m_enableCustomPrompt = false;
    QString m_customPrompt;
    QString m_customContextPrompt;
    bool m_cacheEnabled = true;
    bool m_fallbackEnabled = true;
    bool m_smartChunking = true;
    bool m_sentenceAwareChunking = true;
    bool m_qualityGateEnabled = true;
    int m_maxChunkChars = 14000;
    int m_timeoutMs = 0;
    TermGlossary m_glossary;
    std::shared_ptr<TranslationCache> m_cache;

    // 翻译取消标志（translateLines 启动时重置；cancelTranslation 置位；
    // 后端请求经 m_cancelFlag 同步取消）
    std::atomic_bool m_cancelRequested{false};
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
    // 是否正在翻译（translateLines 开始置 true，完成/取消后置 false）
    std::atomic_bool m_translating{false};
};
