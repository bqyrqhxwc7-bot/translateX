#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QPair>
#include <QList>
#include <memory>
#include <atomic>

// 翻译选项（UI 传入，后端消费）
struct TranslationOptions {
    QString sourceLang = QStringLiteral("en");
    QString targetLang = QStringLiteral("zh-CN");
    QStringList contextLines;      // 上下文行（供上下文翻译）
    bool strictOutput = true;      // 仅保留译文
    double temperature = 0.2;
    int timeoutMs = 0;             // 0 = 后端默认
    QVariantMap extra;             // 后端特定参数（模型名等）

    // 从 extra 读取便捷方法
    QString model() const { return extra.value(QStringLiteral("model")).toString(); }
    QString customPrompt() const { return extra.value(QStringLiteral("customPrompt")).toString(); }
    QString customContextPrompt() const { return extra.value(QStringLiteral("customContextPrompt")).toString(); }
    bool disableThinking() const { return extra.value(QStringLiteral("disableThinking"), true).toBool(); }
    QString apiEndpoint() const { return extra.value(QStringLiteral("apiEndpoint")).toString(); }
    QString apiKey() const { return extra.value(QStringLiteral("apiKey")).toString(); }
};

// 翻译结果
struct TranslationResult {
    QString text;
    bool success = false;
    QString errorMessage;
    qint64 elapsedMs = 0;
    bool fromCache = false;
};

// 翻译后端接口（第三方插件实现此接口）
// 注意：接口类不使用 Q_OBJECT（纯虚接口、无信号槽），
// 便于作为抽象基类被第三方插件继承，也避免 AUTOMOC 额外处理。
class ITranslationBackend : public QObject
{
public:
    explicit ITranslationBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~ITranslationBackend() override = default;

    // 后端标识（稳定唯一，如 "translation.ollama"）
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool supportsContext() const { return false; }
    virtual bool supportsStreaming() const { return false; }

    // 单条翻译（同步返回；长耗时由调用方放入线程池）
    virtual TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) = 0;

    // 批量翻译（默认逐条循环；后端可覆盖做合并/流式优化）
    virtual QList<QPair<int, TranslationResult>> translateBatch(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag)
    {
        Q_UNUSED(sourceLines);
        QList<QPair<int, TranslationResult>> results;
        for (int lineNumber : targetLines) {
            if (cancelFlag && cancelFlag->load()) {
                break;
            }
            const QString text = lineNumber >= 0 && lineNumber < sourceLines.size()
                ? sourceLines.at(lineNumber)
                : QString();
            results.append(qMakePair(lineNumber, translate(text, options, cancelFlag)));
        }
        return results;
    }

    // 健康检查：空串=正常
    virtual QString healthCheck() const { return QString(); }

    // 配置更新（后端自行决定是否使用）
    virtual void updateConfig(const QVariantMap &config) { Q_UNUSED(config); }
};
