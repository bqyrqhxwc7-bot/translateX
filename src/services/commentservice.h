#pragma once

#include <QObject>
#include <QHash>
#include <QVariantMap>
#include <QString>

#include "iservice.h"

// 批注服务（可插拔）：批注数据单一数据源。
// - 供翻译服务写入译文、用户手写批注、第三方插件扩展
// - DocumentModel 通过 provider 委托读取（角色渲染）与行号平移
// - 支持导出/导入 JSON（为 DocumentManager 打开/保存预留）
class CommentService : public QObject, public IService
{
    Q_OBJECT
    Q_INTERFACES(IService)

public:
    explicit CommentService(QObject *parent = nullptr);

    // ---- IService ----
    QString serviceId() const override;
    QString displayName() const override;
    QString serviceVersion() const override;
    QVariantMap healthCheck() const override;
    // 侧边栏面板：批注列表（迭代5 UI 扩展点）
    QString sidebarPanel() const override
    {
        return QStringLiteral("qrc:/qt/qml/Translex/qml/panels/CommentPanel.qml");
    }

    // 读写（空文本视为删除）
    Q_INVOKABLE void setComment(int lineNumber, const QString &text);
    Q_INVOKABLE void removeComment(int lineNumber);
    Q_INVOKABLE QString commentAt(int lineNumber) const;
    Q_INVOKABLE bool hasCommentAt(int lineNumber) const;

    // 统计 / 全量
    Q_INVOKABLE int count() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap allComments() const;   // { "行号": "文本" }

    // 行号平移（DocumentModel 插入/删除行后调用，批注跟随内容移动）
    Q_INVOKABLE void shiftLines(int fromLineNumber, int delta);

    // 持久化（JSON：{ "version": 1, "comments": { "行号": "文本" } }）
    Q_INVOKABLE bool exportToFile(const QString &path) const;
    Q_INVOKABLE bool importFromFile(const QString &path);

signals:
    void commentChanged(int lineNumber);   // 单行批注变化
    void commentsReset();                  // clear / import / shift 后全量变化

private:
    QHash<int, QString> m_comments; // 行号 → 批注文本
};
