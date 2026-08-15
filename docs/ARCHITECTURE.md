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
│       ├── documentmanager.*   # 文档打开/保存/最近文件（.txt/.trx 分发）
│       ├── trxparser.*         # .trx 格式读写（显示层往返）
│       ├── configservice.*     # 配置（schema 驱动，ui.json/translation*.json）
│       ├── securestorage.*     # 敏感设置加密存储（API Key 等）
│       ├── termglossary.*      # 术语表（翻译一致性）
│       ├── qualitygate.*       # 质量自检（回显拦截）
│       ├── translationcache.*  # 翻译缓存（降本）
│       ├── serviceregistry.*   # 服务注册表
│       └── appguard.*          # 稳定性：全局日志/崩溃诊断
├── qml/                      # QML 界面（FluentUI）
│   ├── Main.qml              # 主窗口 + 导航
│   ├── TranslateHomePage.qml # 翻译编辑页（Ribbon + 虚拟化编辑器 + 浮窗）
│   ├── TranslateSettingsPage.qml
│   ├── TranslatePanelContent.qml # 翻译面板主体（Ribbon/浮窗复用）
│   └── ConfigSectionCard.qml # schema 驱动配置卡片
├── docs/                     # 设计文档
│   ├── ARCHITECTURE.md       # 本文件
│   ├── services/             # 服务层设计（见 §3 清单）
│   └── ui/                   # UI 设计（ribbon-toolbar.md、translate-panel.md）
├── tests/                    # 单元测试 + 性能基准（11 个目标，见 §4）
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
│  Main.qml / HomePage / SettingsPage     │
├─────────────────────────────────────────┤
│        QML 服务桥 (context properties)   │
│  main_qml.cpp setContextProperty 暴露     │
├─────────────────────────────────────────┤
│            C++ 服务层 (src/services)     │
│  DocumentModel / Translation / Comment / │
│  Chapter / Find / Config / Manager / ... │
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
| `DocumentManager` | 打开/保存/最近文件（.txt/.trx） | `document-manager.md` |
| `TrxParser` | .trx 读写（显示层往返） | `file-service.md` |
| `ConfigService` | schema 驱动配置（JSON 声明/读写/加密） | `config-service.md` |
| `SecureStorage` | 敏感设置加密存储 | `securestorage.md` |
| `TermGlossary` | 术语表（翻译一致性） | `translation-service.md` §4.2 |
| `QualityGate` | 质量自检（回显拦截/占位保留） | `translation-service.md` §4.3 |
| `TranslationCache` | 磁盘/内存翻译缓存（降本） | `translation-service.md` §3.1 |
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

测试共享源码抽为 `translex_services` 静态库（`tests/CMakeLists.txt`），避免 11 个目标重复编译 15 个服务源。

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

## 5. 路线与扩展方式

### 已完成（2026-08-13）
- A1/A2：文档打开/保存/最近文件（.txt）
- A3：.trx 格式闭环（显示层：富文本/图片往返，编辑即降级）
- 浮窗：真独立 Window + 位置记忆 + 启动显示
- 查找：大小写/整词/模糊

### 待办
| 项 | 说明 |
| --- | --- |
| B | docx 导入（需先决策依赖库，见 AGENTS.md） |
| C | pdf 导入/导出 |
| D | 大文件降级（5 万行 / 200MB 上限策略） |

### 新增服务的方式
1. 在 `src/services/` 新建 `XxxService`（QObject，Q_INVOKABLE）
2. 在 `main_qml.cpp` 用 `setContextProperty` 暴露（应用级，NoStack 页面重建不丢状态）
3. QML 中直接调用
4. 在 `tests/` 添加对应 `tst_xxx.cpp` 并加入 `translex_services` 静态库

## 6. 发布注意事项

- 打包：`cmake --build build-vs2026-x64 --config Release --target package`（NSIS/ZIP）
- 关闭测试构建：`-DBUILD_TESTING=OFF`
- 日志/配置写入 `%APPDATA%/sr291/Translex/`，不在安装目录写文件（避免权限问题）
- FluentUI 为 BSD-3-Clause，项目整体 MIT（见 LICENSE + THIRD_PARTY 说明）
