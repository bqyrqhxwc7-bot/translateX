#pragma once

#include <QString>
#include <QVariantMap>

// 服务基类接口（迭代5 插件化 A3）：所有 service 实现此接口，
// 经 ServiceRegistry 统一注册/查询/健康度聚合。
// 注意：纯虚接口不继承 QObject（service 类本身是 QObject，避免多重继承），
// 注册表用 qobject_cast<IService*>(obj) 转换（service 类需 Q_INTERFACES(IService)）。
// 见 docs/services/iteration5-plugin-ui-agent.md §1
class IService
{
public:
    virtual ~IService() = default;

    // 服务标识（稳定、唯一，如 "translation"）
    virtual QString serviceId() const = 0;
    // 服务名（显示用）
    virtual QString displayName() const = 0;
    // 服务版本
    virtual QString serviceVersion() const = 0;

    // 健康检查：返回 { status: "ok"/"warn"/"error", message, detail }
    // status=ok 表示正常；warn 表示可用但有降级；error 表示不可用
    virtual QVariantMap healthCheck() const = 0;

    // 侧边栏面板 QML 组件 URL（可选；空=无面板）。
    // 插件 UI 扩展点：注册了面板的 service 会在最左图标栏出现入口，
    // 点击后侧边栏加载对应 QML 组件（见迭代5 §2.1）。
    virtual QString sidebarPanel() const { return QString(); }
};

#define TranslexService_iid "org.translex.IService/1.0"
Q_DECLARE_INTERFACE(IService, TranslexService_iid)