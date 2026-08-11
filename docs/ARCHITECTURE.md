# translateX 架构文档

## 1. 项目结构

```
translateX/
├── CMakeLists.txt            # 顶层构建：应用 + 测试 + 打包
├── src/
│   ├── main.cpp              # Widgets 版入口（旧版，保留兼容）
│   ├── main_qml.cpp          # QML 版入口（当前主线）
│   ├── mainwindow.*          # Widgets 主窗口（迁移源，逐步淘汰）
│   ├── annotatedtexteditor.* # Widgets 编辑器（迁移源）
│   └── services/             # ★ 可插拔服务层（新架构核心）
│       ├── documentmodel.*   # 懒加载文档行模型（大文件性能核心）
│       ├── securestorage.*   # 敏感设置加密存储（API Key 等）
│       └── appguard.*        # 稳定性：全局日志/崩溃诊断
├── qml/                      # QML 界面（FluentUI）
│   ├── Main.qml              # 主窗口 + 导航
│   ├── TranslateHomePage.qml # 翻译编辑页（虚拟化编辑器 + 双模式翻译面板）
│   ├── TranslateSettingsPage.qml
│   ├── TranslatePanelContent.qml # 翻译面板主体（浮动/停靠复用）
│   └── ConfigSectionCard.qml # schema 驱动配置卡片
├── docs/                     # 设计文档
│   ├── ARCHITECTURE.md       # 本文件
│   ├── services/             # 服务层设计（见下方清单）
│   └── ui/translate-panel.md # 翻译面板双模式设计
├── tests/                    # 单元测试 + 性能基准
│   ├── CMakeLists.txt
│   ├── tst_documentmodel.cpp # 文档模型健康度
│   ├── tst_securestorage.cpp # 安全存储
│   └── tst_performance.cpp   # 大文件性能基准
├── third_party/FluentUI/     # FluentUI 1.7.7（BSD-3-Clause）
└── .agents/skills/           # Qt 官方 AI 技能（qt-qml 等）
```

## 2. 分层架构

```
┌─────────────────────────────────────────┐
│            QML UI (FluentUI)            │
│  Main.qml / HomePage / SettingsPage     │
├─────────────────────────────────────────┤
│         QML 服务桥 (TranslateX.Services)│
│  qmlRegisterType → 暴露 C++ 服务         │
├─────────────────────────────────────────┤
│            C++ 服务层 (src/services)     │
│  DocumentModel / SecureStorage / AppGuard│
│  (未来: Translation/Comment/Chapter/Find)│
├─────────────────────────────────────────┤
│         Qt6 (Widgets+Quick+Qml+Network)  │
└─────────────────────────────────────────┘
```

### 设计原则
- **UI 与服务解耦**：QML 只消费服务 API，不包含业务逻辑
- **可插拔**：每个服务是独立 QObject，通过 `qmlRegisterType` 暴露，可单独测试/替换
- **懒加载**：大文件只加载可见窗口，编辑只通知单行

## 3. 服务说明

### DocumentModel（已完成）
懒加载行模型，解决大文件卡顿核心。

| API | 说明 |
| --- | --- |
| `setLines(QStringList)` | 批量载入（仅在打开/导入调用一次） |
| `lineText(n)` / `lineCount()` | 按需行访问 |
| `updateLineText(n, text)` | 单行更新（只发 `dataChanged` 单行） |
| `insertLine/removeLine/appendLine` | 编辑操作（带正确的 model 通知） |
| `setComment/hasCommentAt/commentAt` | 批注管理 |
| `clear()` | 清空 |

**性能特性**：50 万行加载 78ms、10 万行更新 220ms、2 万次随机访问 2ms（见 `tests/tst_performance.cpp`）。

### SecureStorage（已完成）
API Key 等敏感设置的加密存储。

- 机器指纹（主机名+内核+架构）派生密钥
- 随机盐 + XOR 混淆 + 校验哈希
- 写入固定 `%APPDATA%/translateX/secure.ini`，与调用方 QSettings 解耦
- 防篡改：数据损坏或换机器时返回空串（不崩溃）

### AppGuard（已完成）
- 全局 Qt 消息重定向到每日日志文件
- 记录启动信息（应用/Qt 版本）
- `qFatal` 时崩溃前落盘

## 4. 测试策略

```bash
# 构建测试
cmake --build build-vs2026-x64 --config Debug --target tst_documentmodel tst_securestorage tst_performance

# 运行（需 Qt bin 在 PATH）
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 仅性能基准
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

| 测试 | 覆盖 |
| --- | --- |
| `tst_documentmodel` | 行增删改查、批注、越界安全、空模型 |
| `tst_securestorage` | 加解密往返、篡改检测、明文不落盘、盐随机性 |
| `tst_performance` | 大文件加载/更新/批注/插删/随机访问 |

## 5. 可扩展性路线

### 服务层（已实现）
| 服务 | 职责 |
| --- | --- |
| `TranslationService` | 翻译门面：后端抽象 + 缓存/质量/成本 |
| `CommentService` | 批注（单一数据源，模型 provider 委托） |
| `ConfigService` | 配置（VSCode-like：JSON 声明/读写/加密，替代 SettingsService） |
| `DocumentManager` | 文档打开/保存，批注随文档持久化 |
| `ChapterService` | 章节索引 |
| `FindService` | 查找替换（大文件全文） |

### 新增后端/功能的方式
1. 在 `src/services/` 新建 `XxxService`（QObject）
2. 在 `main_qml.cpp` 注册 `qmlRegisterType`
3. QML 中 `import TranslateX.Services 1.0` 使用
4. 在 `tests/` 添加对应 `tst_xxx.cpp`
5. CMakeLists 中 `add_subdirectory` 自动纳入测试

## 6. 发布注意事项

- 打包：`cmake --build build-vs2026-x64 --config Release --target package`（NSIS/ZIP）
- 关闭测试构建：`-DBUILD_TESTING=OFF`
- 日志/配置写入 `%APPDATA%/translateX/`，不在安装目录写文件（避免权限问题）
- FluentUI 为 BSD-3-Clause，项目整体 MIT（见 LICENSE + THIRD_PARTY 说明）
