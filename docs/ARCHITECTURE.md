# Translex 架构文档

## 1. 项目结构

```
Translex/
├── CMakeLists.txt            # 顶层构建：应用 + 测试 + 打包
├── src/
│   ├── main_qml.cpp          # QML 版入口（当前主线）
│   └── services/             # ★ 可插拔服务层（新架构核心，全部 Q_INVOKABLE）
│       ├── documentmodel.*     # 懒加载文档行模型（大文件性能核心，应用级单例）
│       ├── translationservice.* # 翻译编排：上下文/分块/降级/回显拦截/缓存
│       ├── translationbackend.* # 三种后端：Ollama / 云翻译 / 网络大模型
│       ├── commentservice.*    # 批注单一数据源（provider 委托）
│       ├── chapterservice.*    # 章节识别与跳转
│       ├── findservice.*       # 查找替换（大小写/整词/模糊）
│       ├── documentmanager.*   # 文档打开/保存/最近文件（.txt/.trx/.docx/.pdf 分发）
│       ├── trxparser.*         # .trx 格式读写（显示层往返）
│       ├── configservice.*     # 配置（schema 驱动，ui.json/translation*.json）
│       ├── securestorage.*     # 敏感设置加密存储（API Key 等）
│       ├── termglossary.*      # 术语表（翻译一致性）
│       ├── qualitygate.*       # 质量自检（回显拦截）
│       ├── translationcache.*  # 翻译缓存（降本）
│       ├── serviceregistry.*   # 服务注册表（单例：注册/查询/健康度聚合/插件扫描）
│       ├── iservice.*          # IService 接口（serviceId/displayName/健康度/侧边栏面板）
│       └── appguard.*          # 稳定性：全局日志/崩溃诊断
├── qml/                      # QML 界面（FluentUI）
│   ├── Main.qml              # 主窗口（Outlook 式布局：图标栏 + 侧边栏面板 + 内容区 NoStack 切换）
│   ├── IconBarButton.qml     # 图标栏按钮（tooltip / active 指示条）
│   ├── TranslateHomePage.qml # 翻译编辑页（Ribbon + 虚拟化编辑器 + 浮窗）
│   ├── TranslateSettingsPage.qml # 设置页（含服务调试卡片）
│   ├── TranslatePanelContent.qml # 翻译面板主体（Ribbon/浮窗复用）
│   ├── ConfigSectionCard.qml # schema 驱动配置卡片
│   └── panels/               # service 侧边栏面板（sidebarPanels() 动态注册）
│       ├── ChapterPanel.qml  # 章节导航
│       ├── CommentPanel.qml  # 批注列表
│       └── HistoryPanel.qml  # 翻译历史
├── plugins/                  # 示例插件（example_translation_plugin：回显后端 + 面板）
├── docs/                     # 设计文档
│   ├── ARCHITECTURE.md       # 本文件
│   ├── services/             # 服务层设计（见 §3 清单）
│   └── ui/                   # UI 设计（ribbon-toolbar.md、translate-panel.md）
├── tests/                    # 单元测试 + 性能基准（16 个目标，见 §4）
│   ├── CMakeLists.txt        # 共享服务抽为 translex_services 静态库
│   └── tst_*.cpp
├── samples/demo.trx          # .trx 示例文档（含富文本/图片显示层）
├── third_party/FluentUI/     # FluentUI 1.7.7（BSD-3-Clause，git 子模块，本地补丁）
└── .agents/skills/           # Qt 官方 AI 技能（qt-qml 等）
```

## 2. 分层架构

```
┌─────────────────────────────────────────┐
│            QML UI (FluentUI)            │
│  Main.qml（Outlook 布局：图标栏 +        │
│  侧边栏面板 + 内容区 NoStack）            │
│  HomePage / SettingsPage / panels/*      │
├─────────────────────────────────────────┤
│        QML 服务桥 (context properties)   │
│  main_qml.cpp setContextProperty 暴露     │
├─────────────────────────────────────────┤
│            C++ 服务层 (src/services)     │
│  ServiceRegistry（注册/查询/健康度聚合/    │
│                    插件扫描，单例）        │
│  DocumentModel / Translation / Comment / │
│  Chapter / Find / Config / Manager / ... │
├─────────────────────────────────────────┤
│         插件层（L3 动态插件）             │
│  QPluginLoader 扫描 <exe>/plugins/*.dll   │
│  → ITranslationPlugin → 后端 + 侧边栏面板 │
├─────────────────────────────────────────┤
│      Qt6 (Quick+Qml+Network+Concurrent)  │
└─────────────────────────────────────────┘
```

### 设计原则
- **UI 与服务解耦**：QML 只消费服务 API，不包含业务逻辑
- **可插拔**：每个服务是独立 QObject，通过 `qmlRegisterType` 暴露，可单独测试/替换
- **懒加载**：大文件只加载可见窗口，编辑只通知单行

## 3. 服务说明

完整设计见 `docs/services/`（每个服务一份）。概要：

| 服务 | 职责 | 文档 |
| --- | --- | --- |
| `DocumentModel` | 懒加载行模型 + 显示层（plain/rich/image） | `documentmodel.md` |
| `TranslationService` | 翻译门面：上下文/分块/降级/回显拦截/缓存 | `translation-service.md` |
| `TranslationBackend` | Ollama / 云翻译 / 网络大模型 三后端 | `translation-service.md` §6 |
| `CommentService` | 批注单一数据源（provider 委托） | `comment-service.md` |
| `ChapterService` | 章节识别与跳转 | `chapter-service.md` |
| `FindService` | 查找替换（大小写/整词/模糊） | `find-service.md` |
| `DocumentManager` | 打开/保存/最近文件（.txt/.trx/.docx/.pdf） | `document-manager.md` |
| `TrxParser` | .trx 读写（显示层往返） | `file-service.md` |
| `ConfigService` | schema 驱动配置（JSON 声明/读写/加密） | `config-service.md` |
| `SecureStorage` | 敏感设置加密存储 | `securestorage.md` |
| `TermGlossary` | 术语表（翻译一致性） | `translation-service.md` §4.2 |
| `QualityGate` | 质量自检（回显拦截/占位保留） | `translation-service.md` §4.3 |
| `TranslationCache` | 磁盘/内存翻译缓存（降本） | `translation-service.md` §3.1 |
| `ServiceRegistry` | 服务注册表单例：后端注册/服务注册/健康度聚合/插件扫描 | `plugin-development.md` |
| `AppGuard` | 稳定性：日志/崩溃诊断 | （见源码） |

### DocumentModel（核心）
懒加载行模型，解决大文件卡顿核心；**应用级单例**（NoStack 页面重建不丢编辑）。

| API | 说明 |
| --- | --- |
| `setLines(QStringList)` | 批量载入（仅在打开/导入调用一次） |
| `lineText(n)` / `lineCount()` | 按需行访问 |
| `updateLineText(n, text)` | 单行更新（只发 `dataChanged` 单行） |
| `insertLine/removeLine/appendLine` | 编辑操作（带正确的 model 通知） |
| `setLineDisplay/Rich/Images` | 显示层（.trx 富文本/图片，编辑即降级，不参与 undo） |
| `clear()` | 清空 |

**性能特性**：50 万行加载 78ms、10 万行更新 220ms、2 万次随机访问 2ms（见 `tests/tst_performance.cpp`）。

## 4. 测试策略

测试共享源码抽为 `translex_services` 静态库（`tests/CMakeLists.txt`），避免 16 个目标重复编译服务源。

```powershell
# 构建 + 运行全部测试（需 Qt bin 在 PATH）
$env:PATH = "D:/Software/Qt/6.5.3/msvc2019_64/bin;" + $env:PATH
cmake --build build-vs2026-x64 --config Debug
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 仅性能基准
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

| 测试 | 覆盖 |
| --- | --- |
| `tst_documentmodel` | 行增删改查、批注、越界安全、空模型 |
| `tst_securestorage` | 加解密往返、篡改检测、明文不落盘、盐随机性 |
| `tst_performance` | 大文件加载/更新/批注/插删/随机访问 |
| `tst_translation` | 注册表/缓存/门面 |
| `tst_quality` | 术语表/质量门/磁盘缓存/智能分块 |
| `tst_configservice` | 配置声明/读写/持久化/加密 |
| `tst_comment` | CommentService + provider 集成 |
| `tst_documentmanager` | 打开/保存/最近文件 |
| `tst_chapter` | 章节索引 |
| `tst_find` | 查找/替换/模糊 |
| `tst_trx` | .trx 往返/降级/损坏处理 |
| `tst_docx` | docx 解析/图文混排/往返 |
| `tst_pdf` | PDF 导入/每行一页导出/完整往返/加密拒读 |
| `tst_texttospeech` | TTS 服务（降级路径/配置往返） |
| `tst_history` | 翻译历史（记录/顺序/上限/清空） |
| `tst_registry` | 服务注册/按 ID 查询/重复 ID 覆盖/健康度聚合/后端注册 |

## 5. 路线与扩展方式

### 已完成（2026-08-13 至 2026-08-18）
- A1/A2：文档打开/保存/最近文件（.txt）
- A3：.trx 格式闭环（显示层：富文本/图片往返，编辑即降级）
- 浮窗：真独立 Window + 位置记忆 + 启动显示
- 查找：大小写/整词/模糊
- B：docx 导入（QuaZip + 内嵌图片 base64，图文混排）
- C：pdf 导入/导出（每行一页 + ToUnicode CMap 重建，完整往返）
- D：大文件降级（5 万行 / 200MB 受限模式）
- 迭代5 插件化（L3）：IService 健康度 + ServiceRegistry 服务注册/健康度聚合 + Translex_sdk + 示例插件（`translation.echo` 回显后端 + 侧边栏面板）+ `tst_registry`
- 迭代5 UI：Outlook 式主窗口（图标栏 / 侧边栏面板 / 内容区 NoStack 切换）+ 设置页调试卡片 + 窗口按钮修复（FluentUI 本地补丁 `z: 1`）+ 浮窗跟随主窗口（最小化隐藏/恢复显示、退出放行关闭）

### 待办
| 项 | 说明 |
| --- | --- |
| E | 术语表 UI（C++ 层已就绪） |
| F | docx 导出 |
| G | .trx 图片 external 降级 |

### 新增服务的方式
1. 在 `src/services/` 新建 `XxxService`（QObject + `Q_INTERFACES(IService)`，Q_INVOKABLE）
2. 在 `main_qml.cpp` 用 `registry->registerService(...)` 注册，再 `setContextProperty` 暴露（应用级，NoStack 页面重建不丢状态）
3. QML 中直接调用；健康度经 `ServiceRegistry::healthReport()` 聚合，设置页「调试」卡片自动展示
4. 在 `tests/` 添加对应 `tst_xxx.cpp` 并加入 `translex_services` 静态库
5. 若需图标栏入口：`IService::sidebarPanel()` 返回面板 QML URL，`sidebarPanels()` 自动注册

### 插件（第三方，L3 动态插件）
详见 [`docs/services/plugin-development.md`](services/plugin-development.md)：实现 `ITranslationPlugin`（Q_PLUGIN_METADATA + Q_INTERFACES），DLL 放入 `<exe>/plugins/`，启动时 `scanPluginDirectory` 加载；可提供自定义翻译后端 + 侧边栏面板。

## 6. 发布注意事项

- 打包：`cmake --build build-vs2026-x64 --config Release --target package`（NSIS/ZIP）
- 关闭测试构建：`-DBUILD_TESTING=OFF`（注意：BUILD_TESTING 可能被缓存为 OFF，需要测试时重新配置显式加 `-DBUILD_TESTING=ON`）
- 日志/配置写入 `%APPDATA%/sr291/Translex/`，不在安装目录写文件（避免权限问题）
- 插件目录 `<exe>/plugins/` 需随发布部署（示例插件构建后自动拷贝 DLL + 面板 QML）
- 窗口按钮修复依赖 FluentUI 子模块本地补丁（`FluWindow.qml` `loader_app_bar` 加 `z: 1`，appBar 悬浮层高于内容层）——**有意补丁，不提交子模块**（见 HANDOVER.md §7）
- FluentUI 为 BSD-3-Clause，项目整体 MIT（见 LICENSE + THIRD_PARTY 说明）
