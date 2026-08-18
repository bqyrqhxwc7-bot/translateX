#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>

#include "services/appguard.h"
#include "services/documentmodel.h"
#include "services/translationservice.h"
#include "services/configservice.h"
#include "services/commentservice.h"
#include "services/documentmanager.h"
#include "services/chapterservice.h"
#include "services/findservice.h"
#include "services/texttospeechservice.h"
#include "services/translationhistoryservice.h"
#include "services/serviceregistry.h"
#include "driver_service.h"

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

    // 服务注册表（迭代5 插件化 A3）：所有 service 注册到注册表，
    // 供健康度聚合/调试面板/插件扩展；setContextProperty 名字不变（QML 零改动）。
    ServiceRegistry *registry = ServiceRegistry::instance();
    registry->registerService(&guard);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("serviceRegistry", registry);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // 注册核心服务（可插拔服务层）
    qmlRegisterType<DocumentModel>("Translex.Services", 1, 0, "DocumentModel");

    // 翻译服务单例（供 QML 直接调用）
    TranslationService translationService;
    registry->registerService(&translationService);
    engine.rootContext()->setContextProperty("translationService", &translationService);

    // 配置服务单例（VSCode-like：schema 驱动设置 UI、统一读写/持久化/加密）
    ConfigService *configService = ConfigService::instance();
    registry->registerService(configService);
    engine.rootContext()->setContextProperty("configService", configService);

    // 核心文档模型（应用级单例）：NoStack 模式每次导航重建页面，若模型在页面内
    // 创建会随页面销毁导致未保存编辑丢失；提升到应用级后内容跨页面保留。
    DocumentModel documentModel;
    registry->registerService(&documentModel);
    engine.rootContext()->setContextProperty("documentModel", &documentModel);

    // 批注服务（批注单一数据源；DocumentModel 经 provider 委托读取/平移）
    CommentService commentService;
    registry->registerService(&commentService);
    engine.rootContext()->setContextProperty("commentService", &commentService);

    // 文档管理服务（打开/保存/新建，批注随文档持久化）
    DocumentManager documentManager;
    registry->registerService(&documentManager);
    engine.rootContext()->setContextProperty("documentManager", &documentManager);

    // 章节服务（章节索引）与查找替换服务（大文件全文查找）
    ChapterService chapterService;
    registry->registerService(&chapterService);
    engine.rootContext()->setContextProperty("chapterService", &chapterService);
    FindService findService;
    registry->registerService(&findService);
    engine.rootContext()->setContextProperty("findService", &findService);

    // TTS 朗读服务（独立，不依赖其他 service；无 TTS 模块/引擎时优雅降级）
    TextToSpeechService textToSpeechService;
    registry->registerService(&textToSpeechService);
    engine.rootContext()->setContextProperty("textToSpeechService", &textToSpeechService);

    // 翻译历史服务（迭代4b：内存环形缓冲，QML 在翻译回调里 record）
    TranslationHistoryService translationHistoryService;
    registry->registerService(&translationHistoryService);
    engine.rootContext()->setContextProperty("translationHistoryService", &translationHistoryService);

    // 插件动态发现（L3）：扫描 <exe_dir>/plugins/，插件后端自动进入设置页下拉
    registry->scanPluginDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));

    // 暴露主窗口（浮窗 Qt.Tool 经 transientParent 与主窗口关联）：先置空占位，避免 QML
    // 早期求值报"未定义"；load 完成后更新为实际根窗口（FluWindow）
    engine.rootContext()->setContextProperty("mainWindow", (QObject *)nullptr);

    // UI 驱动桥（测试钩子）：仅 TRANSLEX_UI_DRIVER=1 时启用，供 review agent
    // 模拟用户操作（打开文件/切主题/翻译/查状态），见 src/driver_service.h
    UiDriverService uiDriverService;
    if (qEnvironmentVariableIsSet("TRANSLEX_UI_DRIVER")) {
        engine.rootContext()->setContextProperty("uiDriverBridge", &uiDriverService);
    }

    engine.loadFromModule("Translex", "Main");

    const auto roots = engine.rootObjects();
    if (!roots.isEmpty()) {
        engine.rootContext()->setContextProperty("mainWindow", roots.first());
    }

    return app.exec();
}
