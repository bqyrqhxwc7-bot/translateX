#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "services/appguard.h"
#include "services/documentmodel.h"
#include "services/translationservice.h"
#include "services/configservice.h"
#include "services/commentservice.h"
#include "services/documentmanager.h"
#include "services/chapterservice.h"
#include "services/findservice.h"

#ifdef FLUENTUI_BUILD_STATIC_LIB
#  include <QtQml/qqmlextensionplugin.h>
#  if (QT_VERSION > QT_VERSION_CHECK(6, 2, 0))
Q_IMPORT_QML_PLUGIN(FluentUIPlugin)
#  endif
#  include <FluentUI.h>
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Translex"));
    app.setApplicationVersion(QStringLiteral(TRANSLEX_VERSION));
    app.setOrganizationName(QStringLiteral("sr291"));
    app.setOrganizationDomain(QStringLiteral("local.translex"));

    // 稳定性：安装全局日志与崩溃诊断
    AppGuard guard(&app);
    AppGuard::install();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // 注册核心服务（可插拔服务层）
    qmlRegisterType<DocumentModel>("Translex.Services", 1, 0, "DocumentModel");

    // 翻译服务单例（供 QML 直接调用）
    TranslationService translationService;
    engine.rootContext()->setContextProperty("translationService", &translationService);

    // 配置服务单例（VSCode-like：schema 驱动设置 UI、统一读写/持久化/加密）
    ConfigService *configService = ConfigService::instance();
    engine.rootContext()->setContextProperty("configService", configService);

    // 核心文档模型（应用级单例）：NoStack 模式每次导航重建页面，若模型在页面内
    // 创建会随页面销毁导致未保存编辑丢失；提升到应用级后内容跨页面保留。
    DocumentModel documentModel;
    engine.rootContext()->setContextProperty("documentModel", &documentModel);

    // 批注服务（批注单一数据源；DocumentModel 经 provider 委托读取/平移）
    CommentService commentService;
    engine.rootContext()->setContextProperty("commentService", &commentService);

    // 文档管理服务（打开/保存/新建，批注随文档持久化）
    DocumentManager documentManager;
    engine.rootContext()->setContextProperty("documentManager", &documentManager);

    // 章节服务（章节索引）与查找替换服务（大文件全文查找）
    ChapterService chapterService;
    engine.rootContext()->setContextProperty("chapterService", &chapterService);
    FindService findService;
    engine.rootContext()->setContextProperty("findService", &findService);

    // 暴露主窗口（浮窗 Qt.Tool 经 transientParent 与主窗口关联）：先置空占位，避免 QML
    // 早期求值报"未定义"；load 完成后更新为实际根窗口（FluWindow）
    engine.rootContext()->setContextProperty("mainWindow", (QObject *)nullptr);
    engine.loadFromModule("Translex", "Main");

    const auto roots = engine.rootObjects();
    if (!roots.isEmpty()) {
        engine.rootContext()->setContextProperty("mainWindow", roots.first());
    }

    return app.exec();
}
