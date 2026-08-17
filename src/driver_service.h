#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>

class QLocalServer;
class QLocalSocket;

// 应用内 UI 驱动桥（review agent 模拟用户操作的测试钩子）：
// 仅当环境变量 TRANSLEX_UI_DRIVER=1 时由 main_qml.cpp 创建。
// 通过 QLocalServer（Windows named pipe）接收 JSON 行命令，转发到 QML 侧
// 注册的 UiDriverActions 对象执行业务动作，返回 JSON 结果。
//
// 命令协议（每行一个 JSON）：
//   {"id":1,"cmd":"openFile","args":"C:/x.docx"}
//   {"id":2,"cmd":"setDark","args":true}
//   {"id":3,"cmd":"getState"}
//   {"id":4,"cmd":"translateLine","args":3}
// 回复：{"id":1,"ok":true,"result":...}
//
// 设计见 docs/services/SERVICE-ARCHITECTURE.md §UI 驱动；驱动脚本：
// .opencode/scripts/ui-driver.mjs（review agent 用）
class UiDriverService : public QObject
{
    Q_OBJECT

public:
    explicit UiDriverService(QObject *parent = nullptr);
    ~UiDriverService() override;

    // QML 侧调用：注册操作实现对象（qml/UiDriverActions.qml）
    Q_INVOKABLE void setSink(QObject *sink);

private:
    void onNewConnection();
    void onReadyRead();
    void reply(qintptr socketId, int id, bool ok, const QVariant &result);

    QLocalServer *m_server = nullptr;
    QPointer<QObject> m_sink;
    QHash<qintptr, QLocalSocket *> m_sockets;   // socketDescriptor → socket
};
