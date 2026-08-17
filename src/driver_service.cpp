#include "driver_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QDebug>

// UI 驱动服务：QLocalServer + JSON 行协议（见头文件注释）。
// 仅在 TRANSLEX_UI_DRIVER=1 时启用，属调试设施，不进入正常发布路径。
UiDriverService::UiDriverService(QObject *parent)
    : QObject(parent)
{
    m_server = new QLocalServer(this);
    // 单实例：若已有实例则退出（驱动场景只跑一个应用）
    if (!m_server->listen(QStringLiteral("translex-ui-driver"))) {
        qWarning("UiDriver: listen failed: %s",
                 qPrintable(m_server->errorString()));
        return;
    }
    connect(m_server, &QLocalServer::newConnection,
            this, &UiDriverService::onNewConnection);
    qInfo("UiDriver: listening on translex-ui-driver (TRANSLEX_UI_DRIVER=1)");
}

UiDriverService::~UiDriverService() = default;

void UiDriverService::setSink(QObject *sink)
{
    m_sink = sink;
}

void UiDriverService::onNewConnection()
{
    QLocalSocket *socket = m_server->nextPendingConnection();
    if (!socket) {
        return;
    }
    m_sockets.insert(socket->socketDescriptor(), socket);
    connect(socket, &QLocalSocket::readyRead, this, &UiDriverService::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
        m_sockets.remove(socket->socketDescriptor());
        socket->deleteLater();
    });
}

void UiDriverService::onReadyRead()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }
    while (socket->canReadLine()) {
        const QByteArray line = socket->readLine().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        qInfo("UiDriver: recv [%s]", line.constData());
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        const int id = obj.value(QLatin1String("id")).toInt();
        const QString cmd = obj.value(QLatin1String("cmd")).toString();
        const QJsonValue args = obj.value(QLatin1String("args"));

        if (cmd.isEmpty() || !m_sink) {
            qInfo("UiDriver: cmd empty=%d sink null=%d", cmd.isEmpty(), !m_sink);
            reply(socket->socketDescriptor(), id, false,
                  QStringLiteral("未知命令或 sink 未注册"));
            continue;
        }

        // 转发到 QML sink（同步调用，GUI 线程内执行）。
        // 注意：QML 函数参数在 meta 系统中暴露为 QVariant，须用 Q_ARG(QVariant)。
        QVariant ret;
        const QByteArray method = cmd.toLatin1();
        bool invoked = false;
        if (args.isUndefined() || args.isNull()) {
            invoked = QMetaObject::invokeMethod(
                m_sink, method.constData(), Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, ret));
        } else {
            invoked = QMetaObject::invokeMethod(
                m_sink, method.constData(), Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args.toVariant()));
        }

        if (!invoked) {
            reply(socket->socketDescriptor(), id, false,
                  QStringLiteral("命令执行失败：%1").arg(cmd));
            continue;
        }
        reply(socket->socketDescriptor(), id, true, ret);
    }
}

void UiDriverService::reply(qintptr socketId, int id, bool ok, const QVariant &result)
{
    QLocalSocket *socket = m_sockets.value(socketId);
    if (!socket) {
        return;
    }
    QJsonObject replyObj;
    replyObj.insert(QLatin1String("id"), id);
    replyObj.insert(QLatin1String("ok"), ok);
    replyObj.insert(QLatin1String("result"), QJsonValue::fromVariant(result));
    socket->write(QJsonDocument(replyObj).toJson(QJsonDocument::Compact) + "\n");
    socket->flush();
}
