#include "appguard.h"

#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>
#include <QCoreApplication>
#include <QDebug>

namespace {

QMutex g_logMutex;
QString g_logPath;

QString logDirectory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

QString logFilePathForSession()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    return logDirectory() + QStringLiteral("/Translex-%1.log").arg(stamp);
}

} // namespace

QString AppGuard::logFilePath()
{
    return g_logPath;
}

AppGuard::AppGuard(QObject *parent)
    : QObject(parent)
{
    g_logPath = logFilePathForSession();
    qInfo() << "=== Translex 启动 ===";
    qInfo() << "应用:" << QCoreApplication::applicationName()
            << "版本:" << QCoreApplication::applicationVersion();
    qInfo() << "Qt 版本:" << qVersion();
    qInfo() << "日志文件:" << g_logPath;
}

AppGuard::~AppGuard() = default;

QString AppGuard::serviceId() const
{
    return QStringLiteral("appGuard");
}

QString AppGuard::displayName() const
{
    return QStringLiteral("稳定性服务");
}

QString AppGuard::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap AppGuard::healthCheck() const
{
    if (g_logPath.isEmpty()) {
        return { { QStringLiteral("status"), QStringLiteral("warn") },
                 { QStringLiteral("message"), QStringLiteral("日志未初始化") } };
    }
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("日志：%1").arg(g_logPath) } };
}

void AppGuard::install()
{
    qInstallMessageHandler(&AppGuard::messageHandler);
}

void AppGuard::messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // 过滤 FluentUI 对 Qt 6.5.3 frameless 的固定提示（纯信息性；本应用已解决该兼容问题）
    if (type == QtWarningMsg && message.contains(QStringLiteral("frameless bug"))) {
        return;
    }

    QMutexLocker locker(&g_logMutex);

    if (g_logPath.isEmpty()) {
        g_logPath = logFilePathForSession();
    }

    const QString typeName = [type]() -> QString {
        switch (type) {
        case QtDebugMsg: return QStringLiteral("DEBUG");
        case QtInfoMsg: return QStringLiteral("INFO");
        case QtWarningMsg: return QStringLiteral("WARN");
        case QtCriticalMsg: return QStringLiteral("CRITICAL");
        case QtFatalMsg: return QStringLiteral("FATAL");
        default: return QStringLiteral("?");
        }
    }();

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(stamp, typeName, message);

    QFile file(g_logPath);
    if (file.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << line;
        out.flush();
    }

    // 同时输出到 stderr（调试器可见）
    QByteArray localMsg = line.toUtf8();
    fprintf(stderr, "%s", localMsg.constData());
    fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}

QString AppGuard::currentLog()
{
    QMutexLocker locker(&g_logMutex);
    QFile file(g_logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    return in.readAll();
}
