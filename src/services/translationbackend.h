#pragma once

#include <memory>

#include "itranslationbackend.h"

// ---- 内置后端 1：本地 Ollama ----
class OllamaBackend : public ITranslationBackend
{
    Q_OBJECT

public:
    explicit OllamaBackend(QObject *parent = nullptr);

    QString backendId() const override { return QStringLiteral("translation.ollama"); }
    QString displayName() const override { return QStringLiteral("本地 Ollama"); }
    bool supportsContext() const override { return true; }
    bool supportsStreaming() const override { return true; }

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override;

    void updateConfig(const QVariantMap &config) override;

    QString healthCheck() const override;

    // 扫描可用模型
    QStringList fetchModels(QString *errorMessage = nullptr) const;

private:
    TranslationResult generate(
        const QString &prompt,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) const;

    QString m_endpoint;
    QString m_model;
};

// ---- 内置后端 2：云端在线（免费 MyMemory）----
class OnlineBackend : public ITranslationBackend
{
    Q_OBJECT

public:
    explicit OnlineBackend(QObject *parent = nullptr);

    QString backendId() const override { return QStringLiteral("translation.online"); }
    QString displayName() const override { return QStringLiteral("云端翻译服务"); }
    bool supportsContext() const override { return false; }

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override;
};

// ---- 内置后端 3：网络大模型（OpenAI 兼容 / DeepSeek）----
class NetworkModelBackend : public ITranslationBackend
{
    Q_OBJECT

public:
    explicit NetworkModelBackend(QObject *parent = nullptr);

    QString backendId() const override { return QStringLiteral("translation.network_model"); }
    QString displayName() const override { return QStringLiteral("网络大模型 API"); }
    bool supportsContext() const override { return true; }
    bool supportsStreaming() const override { return true; }

    TranslationResult translate(
        const QString &text,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override;

    QList<QPair<int, TranslationResult>> translateBatch(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) override;

private:
    // 发送一次 chat/completions 请求，返回模型原始输出 content（不含 buildPrompt）
    TranslationResult requestChat(
        const QString &prompt,
        const TranslationOptions &options,
        const std::shared_ptr<std::atomic_bool> &cancelFlag) const;
};

// 在 ServiceRegistry 中注册所有内置后端（应用启动时调用一次）
void registerBuiltinTranslationBackends();
