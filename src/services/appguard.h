#pragma once

#include <QObject>
#include <QString>

#include "iservice.h"

// 稳定性服务：统一日志 + 崩溃诊断 + 启动信息。
// - 将所有 Qt 消息（qWarning/qCritical/qFatal）重定向到本地日志文件
// - 记录启动信息（版本、Qt 版本、平台），便于排查
class AppGuard : public QObject, public IService
{
    Q_OBJECT
    Q_INTERFACES(IService)

public:
    explicit AppGuard(QObject *parent = nullptr);
    ~AppGuard() override;

    // ---- IService ----
    QString serviceId() const override;
    QString displayName() const override;
    QString serviceVersion() const override;
    QVariantMap healthCheck() const override;

    // 初始化全局消息处理器（应用启动时调用一次）
    static void install();
    // 日志文件完整路径
    static QString logFilePath();
    // 当前日志内容（供设置界面/诊断显示）
    static QString currentLog();
    // QML 可调用的日志路径（静态方法无法 Q_INVOKABLE）
    Q_INVOKABLE QString logFile() const { return logFilePath(); }

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message);
};
